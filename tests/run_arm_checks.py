#!/usr/bin/env python3
"""Compile Cortex-M7 checks, verify constant storage and link a newlib-nano consumer."""
import argparse
from collections import Counter
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from audit.compare_arm_probes import encodings


ROOT = Path(__file__).resolve().parent.parent
FLAGS = ["-std=c++17", "-mcpu=cortex-m7", "-mthumb", "-mfpu=fpv5-d16",
         "-mfloat-abi=hard", "-fno-exceptions", "-fno-rtti", "-Wall", "-Wextra",
         "-Werror", "-pedantic-errors", "-fdiagnostics-color=never",
         "-Ilib/telemetry", "-Ilib/delegate"]


def function_body(disassembly, name):
    match = re.search(rf"^[0-9a-fA-F]+ <{re.escape(name)}>:\n(.*?)"
                      r"(?=^[0-9a-fA-F]+ <|^Disassembly of section |\Z)",
                      disassembly, re.MULTILINE | re.DOTALL)
    if match is None:
        raise RuntimeError(f"ARM codegen: missing disassembly for {name}")
    return match.group(1)


def normalized_instructions(disassembly, name):
    """Instruction stream with addresses and self-relative labels removed."""
    result = []
    for line in function_body(disassembly, name).splitlines():
        parts = line.split("\t")
        if len(parts) < 3 or not parts[0].strip().endswith(":"):
            continue
        opcode = parts[2].strip()
        if not opcode:
            continue
        operands = parts[3].strip() if len(parts) > 3 else ""
        operands = re.sub(r"\b[0-9a-fA-F]+ <[^>]+\+0x([0-9a-fA-F]+)>",
                          r"<self+0x\1>", operands)
        result.append((opcode, operands))
    return result


def check_static_field_dispatch(disassembly):
    direct_native = "telemetry_probe_field_read_native"
    native = ("telemetry_probe_read_inferred_known",
              "telemetry_probe_static_read_native")
    direct_converted = "telemetry_probe_field_read_converted"
    converted = "telemetry_probe_static_read_converted"
    for name in (*native, converted):
        body = function_body(disassembly, name)
        if re.search(r"\bblx\b|\bbx\s+(?!lr\b)", body) or "Scalar" in body:
            raise RuntimeError(f"IndexCodegen: {name} retained lookup/type-erased dispatch")
        if any(symbol in body for symbol in (
                "telemetry_probe_index", "telemetry_probe_catalogs",
                "telemetry_probe_group0_fields", "telemetry_probe_group1_fields")):
            raise RuntimeError(f"IndexCodegen: {name} retained catalog metadata access")
    if normalized_instructions(disassembly, native[0]) != normalized_instructions(
            disassembly, native[1]):
        raise RuntimeError("IndexCodegen: inferred and explicit static native reads diverged")

    # A compiler may preserve the same direct getter tail call as Field::read,
    # or inline it further. The static-ID API must never be worse: if its body
    # differs, it must have removed calls and stack work completely.
    for direct, static in ((direct_native, native[1]),
                           (direct_converted, converted)):
        if normalized_instructions(disassembly, direct) != normalized_instructions(
                disassembly, static):
            body = function_body(disassembly, static)
            if (re.search(r"\b(?:push|vpush)\b|\bsub(?:\.w)?\s+sp\b", body)
                    or re.search(r"\bbl\b", body)
                    or "R_ARM_THM_CALL" in body or "R_ARM_THM_JUMP24" in body):
                raise RuntimeError(
                    f"IndexCodegen: {static} differs from direct Field read and adds work")
    native_body = function_body(disassembly, native[1])
    if (normalized_instructions(disassembly, direct_native)
            != normalized_instructions(disassembly, native[1])
            and "vldr" not in native_body):
        raise RuntimeError("IndexCodegen: optimized static F32 read lost its native load")
    if "vcvt.u32.f32" not in function_body(disassembly, converted):
        raise RuntimeError("IndexCodegen: static converted read lost its required checked cast")

    # Static-ID writes must add no instructions to the same known Field call.
    for direct, static in (
            ("telemetry_probe_field_write_float", "telemetry_probe_static_write_float"),
            ("telemetry_probe_field_write_u16", "telemetry_probe_static_write_u16"),
            ("telemetry_probe_field_write_readonly", "telemetry_probe_static_write_readonly")):
        if normalized_instructions(disassembly, direct) != normalized_instructions(
                disassembly, static):
            raise RuntimeError(f"IndexCodegen: {static} differs from direct Field write")


def check_command_dispatch(disassembly):
    target = "CommandProbeDevice::configure(float, CommandProbeMode)"
    for name in ("command_table_call_known", "command_table_call_runtime"):
        body = function_body(disassembly, name)
        if target not in body:
            raise RuntimeError(f"CommandTableCodegen: {name} lost its direct target")
        if re.search(r"\bblx\b", body) or "Scalar" in body:
            raise RuntimeError(f"CommandTableCodegen: {name} reintroduced type erasure")
        if re.search(r"\b(?:push|vpush)\b|\bsub(?:\.w)?\s+sp\b", body):
            raise RuntimeError(f"CommandTableCodegen: {name} unexpectedly uses stack storage")
    local = normalized_instructions(disassembly, "command_table_call_known")
    global_ = normalized_instructions(disassembly, "command_table_call_global")
    if global_ != local:
        # -Os may coalesce identical bodies with a tail branch to the local wrapper.
        real = [i for i in global_ if i[0] != "nop"]
        if not (len(real) == 1 and real[0][0] in ("b", "b.w", "b.n")
                and "<command_table_call_known>" in real[0][1]):
            raise RuntimeError("CommandTableCodegen: global routing adds work to the local call")
    erased = function_body(disassembly, "command_table_execute_erased")
    if not re.search(r"\bblx\b|\bbx\s+(?:ip|r(?:1[0-2]|[0-9]))\b", erased):
        raise RuntimeError("CommandTableCodegen: erased path lost its indirect dispatch probe")


def check_native_field_tables(disassembly):
    def resolved(name, seen=()):
        if name in seen:
            raise RuntimeError("FieldTableCodegen: cyclic alias")
        instructions = normalized_instructions(disassembly, name)
        real = [entry for entry in instructions if entry[0] != "nop"]
        # GCC -Os merges identical exported wrappers using one tail branch.
        if len(real) == 1 and real[0][0] in ("b", "b.w", "b.n"):
            alias = re.search(r"<((?:table_direct|table_local|table_global)_\w+)>", real[0][1])
            if alias:
                return resolved(alias.group(1), (*seen, name))
        return instructions

    for operation in ("read", "converted", "write", "int", "u16", "enum"):
        direct, local, global_ = ("table_" + route + "_" + operation
                                  for route in ("direct", "local", "global"))
        if resolved(direct) != resolved(local) or resolved(local) != resolved(global_):
            raise RuntimeError(f"FieldTableCodegen: {operation} direct/local/global instructions differ")
        for name in (local, global_):
            body = function_body(disassembly, name)
            if "Scalar" in body or re.search(r"\bblx\b", body):
                raise RuntimeError(f"FieldTableCodegen: {name} contains erased dispatch")


def check_native_command_conversions(disassembly):
    def resolved(name, seen=()):
        if name in seen:
            raise RuntimeError("Native command conversion: cyclic alias")
        instructions = normalized_instructions(disassembly, name)
        real = [entry for entry in instructions if entry[0] != "nop"]
        if len(real) == 1 and real[0][0] in ("b", "b.w", "b.n"):
            alias = re.search(r"<(command_conversion_\w+)>", real[0][1])
            if alias:
                return resolved(alias.group(1), (*seen, name))
        return instructions

    direct = resolved("command_conversion_direct")
    local = resolved("command_conversion_local")
    if resolved("command_conversion_global") != local:
        raise RuntimeError("Native command conversion: global routing changes the local instruction stream")
    if local != direct:
        # GCC 13 can put the direct success block AFTER the error return and
        # invert its final bhi/bls. GCC 14 emits identical streams. Check the
        # operation multiset (and literal values) here, not branch placement:
        # this gate establishes no extra operations, not equal board cycles.
        def operations(instructions):
            complements = {"bls": "bhi", "bcs": "bcc", "beq": "bne",
                           "bge": "blt", "bgt": "ble", "bpl": "bmi", "bvs": "bvc"}
            result = []
            for opcode, operands in instructions:
                if opcode.startswith("nop"):
                    continue
                bare = opcode.split(".")[0]
                if bare in complements or bare in complements.values():
                    result.append((complements.get(bare, bare), "conditional branch"))
                else:
                    # PC displacements and objdump address comments depend on
                    # block placement. Other operands and literal values stay.
                    operands = operands.split("@")[0].strip()
                    operands = re.sub(r"\[pc, #[0-9]+\]", "[pc, literal]", operands)
                    result.append((opcode, operands))
            return Counter(result)
        if operations(local) != operations(direct):
            raise RuntimeError("Native command conversion: differs from direct checked-call operations")
    for route in ("direct", "local", "global", "runtime"):
        body = function_body(disassembly, "command_conversion_" + route)
        if "Scalar" in body or re.search(r"\bblx\b|\bbx\s+(?!lr\b)", body):
            raise RuntimeError(f"Native command conversion: {route} reintroduced erased dispatch")
        if re.search(r"\b(?:push|vpush)\b|\bsub(?:\.w)?\s+sp\b", body):
            raise RuntimeError(f"Native command conversion: {route} introduced stack storage")
    runtime = function_body(disassembly, "command_conversion_runtime")
    if "CommandProbeDevice::configure(float, CommandProbeMode)" not in runtime:
        raise RuntimeError("Native command conversion: runtime route lost its direct target")


def check_command_scaling(disassembly):
    expected = {
        "command_scale_10": {1},
        "command_scale_32": {1, 17},
        "command_scale_100": {1, 17, 33, 49, 65, 81, 97},
    }
    for name, targets in expected.items():
        body = function_body(disassembly, name)
        table_size = int(name.rsplit("_", 1)[1])
        actual = {int(value) for value in re.findall(
            r"CommandScaleOwner::matching<(\d+)u>", body)}
        if actual != targets:
            raise RuntimeError(
                f"CommandDispatchScalingCodegen: {name} emitted targets {sorted(actual)}, "
                f"expected {sorted(targets)}")
        if ("CommandScaleOwner::other" in body or "Scalar" in body
                or re.search(r"\bblx\b|\bbx\s+(?!lr\b)", body)):
            raise RuntimeError(
                f"CommandDispatchScalingCodegen: {name} emitted a mismatched or erased target")
        if re.search(r"\b(?:push|vpush)\b|\bsub(?:\.w)?\s+sp\b", body):
            raise RuntimeError(
                f"CommandDispatchScalingCodegen: {name} unexpectedly uses stack storage")
        # One comparison rejects an out-of-range position; every remaining
        # index comparison belongs to one definition with matching arity only.
        index_compares = len(re.findall(r"\bcmp(?:\.w)?\s+r0,", body))
        if index_compares != len(targets) + 1:
            raise RuntimeError(
                f"CommandDispatchScalingCodegen: {name} emitted {index_compares} index "
                f"comparisons for {len(targets)} matching definitions")
        if not re.search(
                rf"\bcmp(?:\.w)?\s+r0,\s*#{table_size - 1}(?:\D|$)", body):
            raise RuntimeError(
                f"CommandDispatchScalingCodegen: {name} lost its range comparison")


def check_owner_slots(disassembly):
    def resolved(name, seen=()):
        if name in seen:
            raise RuntimeError("OwnerSlotCodegen: cyclic wrapper alias")
        instructions = normalized_instructions(disassembly, name)
        real = [entry for entry in instructions if entry[0] != "nop"]
        if len(real) == 1 and real[0][0] in ("b", "b.w", "b.n"):
            alias = re.search(r"<(slot_\w+)>", real[0][1])
            if alias:
                return resolved(alias.group(1), (*seen, name))
        return instructions

    for operation in ("read", "write", "call"):
        manual = resolved("slot_manual_" + operation)
        for route in ("local", "global"):
            name = "slot_" + route + "_" + operation
            if resolved(name) != manual:
                raise RuntimeError(f"OwnerSlotCodegen: {name} differs from explicit pointer check")
            body = function_body(disassembly, name)
            if "Scalar" in body or re.search(r"\bblx\b", body):
                raise RuntimeError(f"OwnerSlotCodegen: {name} retained erased dispatch")
        for kind in ("direct", "free"):
            name = "slot_" + kind + "_table_" + operation
            if resolved(name) != resolved("slot_" + kind + "_manual_" + operation):
                raise RuntimeError(f"OwnerSlotCodegen: {name} added work to an ordinary binding")
            body = function_body(disassembly, name)
            if "slotProbeOwner" in body or re.search(r"\b(?:cmp|cbz|cbnz|blx)\b", body):
                raise RuntimeError(f"OwnerSlotCodegen: {name} added a slot or presence check")


def check_function_slots(disassembly, prefix="function_slot_", operations=("read", "write", "call", "convert")):
    def resolved(name, seen=()):
        if name in seen:
            raise RuntimeError("FunctionSlotCodegen: cyclic wrapper alias")
        instructions = normalized_instructions(disassembly, name)
        real = [entry for entry in instructions if entry[0] != "nop"]
        if len(real) == 1 and real[0][0] in ("b", "b.w", "b.n"):
            alias = re.search(r"<(" + re.escape(prefix) + r"\w+)>", real[0][1])
            if alias:
                return resolved(alias.group(1), (*seen, name))
        return instructions

    for operation in operations:
        manual = resolved(prefix + "manual_" + operation)
        for route in ("local", "global"):
            name = prefix + route + "_" + operation
            if resolved(name) != manual:
                raise RuntimeError(f"FunctionSlotCodegen: {name} differs from explicit function check")
            body = function_body(disassembly, name)
            if any(symbol in body for symbol in ("Scalar", "functionProbeFields", "functionProbeCommands", "lateProbeFields", "lateProbeCommands")):
                raise RuntimeError(f"FunctionSlotCodegen: {name} retained erased dispatch or table lookup")


def check_probe(name, headers, symbols, disassembly):
    if "file format elf32-littlearm" not in headers:
        raise RuntimeError(f"{name}: expected a little-endian ARM object")
    sections = re.findall(r"^\s*\d+\s+(\S+)\s+([0-9a-fA-F]+)\s", headers, re.MULTILINE)
    if not sections:
        raise RuntimeError(f"{name}: no object sections found")
    for section, size in sections:
        if int(size, 16) == 0:
            continue
        if section.startswith((".init_array", ".preinit_array", ".ctors")):
            raise RuntimeError(f"{name}: startup initialization in {section}")
        # Probe sources deliberately keep mutable owners external, so every
        # defined object here must be read-only. Include section suffixes.
        if section.startswith((".data", ".bss", ".sdata", ".sbss")):
            raise RuntimeError(f"{name}: unexpected writable storage in {section}")
    if "_GLOBAL__sub_I" in symbols:
        raise RuntimeError(f"{name}: dynamic initialization function found")
    if "scalarFinite" in symbols:
        raise RuntimeError(f"{name}: finite-value check was outlined instead of inlined")
    expected = {}
    if name == "IndexCodegen":
        expected = {"telemetry_probe_index": 8, "telemetry_probe_catalogs": 24,
                    "telemetry_probe_group0_fields": 4 * 96,
                    "telemetry_probe_group1_fields": 3 * 96}
        check_static_field_dispatch(disassembly)
    elif name == "BorrowedFieldCodegen":
        expected = {"telemetry_probe_borrowed_field": 96}
    elif name == "CommandTableCodegen":
        expected = {"telemetry_probe_command_table": 4,
                    "telemetry_probe_command_count": 4}
        check_command_dispatch(disassembly)
    elif name == "CommandDispatchScalingCodegen":
        check_command_scaling(disassembly)
    elif name == "FieldTableCodegen":
        expected = {"field_table_probe": 3 * 96}
        check_native_field_tables(disassembly)
    elif name == "OwnerSlotCodegen":
        check_owner_slots(disassembly)
    elif name == "FunctionSlotCodegen":
        expected = {"functionProbeFields": 2 * 96}
        check_function_slots(disassembly)
    elif name == "LateBoundCodegen":
        expected = {"lateProbeFields": 6 * 96}
        # Reuse the native callback comparison for each fixed slot strategy.
        # No runtime switch chooses a strategy in the generated wrappers.
        for kind in ("context", "ref", "owned"):
            check_function_slots(disassembly, "late_" + kind + "_", ("read", "write", "call"))
            for route in ("manual", "local", "global"):
                for operation in ("read", "write", "call"):
                    label = f"late_{kind}_{route}_{operation}"
                    body = function_body(disassembly, label)
                    if re.search(r"\b(?:push|pop|vpush|vpop|sp|bl|blx)\b", body):
                        raise RuntimeError(f"LateBoundCodegen: {label} created a stack frame or non-tail call")
    if expected:
        entries = [line.split() for line in symbols.splitlines()]
        for symbol, size in expected.items():
            matches = [entry for entry in entries if entry and entry[-1] == symbol]
            if len(matches) != 1:
                raise RuntimeError(f"{name}: missing or ambiguous symbol {symbol}")
            entry = matches[0]
            if (len(entry) < 6 or entry[-4] != "O" or not entry[-3].startswith(".rodata")
                    or int(entry[-2], 16) != size):
                raise RuntimeError(f"{name}: {symbol} changed size or left read-only storage")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=os.environ.get("ARM_CXX", "arm-none-eabi-g++"))
    parser.add_argument("--objdump", help="defaults to the compiler's sibling arm-none-eabi-objdump")
    parser.add_argument("--build-dir", type=Path, required=True)
    args = parser.parse_args()
    compiler = shutil.which(args.cxx)
    if compiler is None:
        raise RuntimeError(f"ARM compiler not found: {args.cxx}")
    objdump = args.objdump or str(Path(compiler).with_name(
        "arm-none-eabi-objdump" + (".exe" if os.name == "nt" else "")))
    archiver = str(Path(compiler).with_name(
        "arm-none-eabi-ar" + (".exe" if os.name == "nt" else "")))
    output = args.build_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    factory_report = {}

    def run(command, label, rejection=None):
        result = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True,
                                encoding="utf-8", errors="replace", timeout=180)
        (output / (label + ".log")).write_text(result.stdout, encoding="utf-8")
        if rejection is not None:
            valid = result.returncode != 0 and re.search(rejection, result.stdout, re.IGNORECASE)
        else:
            valid = result.returncode == 0
        if not valid:
            print(result.stdout, file=sys.stderr)
            raise RuntimeError(f"{label} failed (exit {result.returncode}); see {output}")
        return result.stdout

    print(run([compiler, "--version"], "compiler-version").splitlines()[0], flush=True)
    target = run([compiler, "-dumpmachine"], "compiler-target").strip()
    if target != "arm-none-eabi":
        raise RuntimeError(f"Expected arm-none-eabi, got {target}")
    sources = [ROOT / "lib/telemetry/abi/TelemetryAbi.cpp",
               ROOT / "lib/telemetry/serialization/TelemetryJson.cpp",
               ROOT / "lib/telemetry/serialization/TelemetryCommandJson.cpp",
               ROOT / "app/demo/DemoCatalog.cpp"]
    sources += sorted(source for source in (ROOT / "tests").glob("*.cpp")
                      if not source.name.endswith("CompileFail.cpp"))
    for optimization in ("-O2", "-Os"):
        flags = [compiler, *FLAGS, optimization]
        probes = 0
        for source in sources:
            label = source.stem + optimization
            obj = output / (label + ".o")
            run(flags + ["-c", str(source), "-o", str(obj)], label + "-compile")
            if source.stem.endswith("Codegen"):
                headers = run([objdump, "-h", str(obj)], label + "-sections")
                symbols = run([objdump, "-t", str(obj)], label + "-symbols")
                disassembly = run([objdump, "-dr", "-C", str(obj)],
                                  label + "-disassembly")
                check_probe(source.stem, headers, symbols, disassembly)
                probes += 1
        # Enum positions must emit precisely the numeric-position instruction
        # words, including literal pools. In particular U64 enum positions must
        # not introduce a runtime 64-bit operation on this 32-bit target.
        for probe in ("FieldTableCodegen", "CommandTableCodegen"):
            label = "enum-position-" + probe.removesuffix("Codegen") + optimization
            obj = output / (label + ".o")
            run(flags + ["-DTELEMETRY_ENUM_POSITION_PROBE", "-c", f"tests/{probe}.cpp",
                         "-o", str(obj)], label + "-compile")
            assembly = run([objdump, "-dr", "-C", str(obj)], label + "-disassembly")
            original = output / (probe + optimization + "-disassembly.log")
            if encodings(original) != encodings(output / (label + "-disassembly.log")):
                raise RuntimeError(f"{probe}: enum positions changed instruction encodings")
            if probe == "FieldTableCodegen":
                check_native_field_tables(assembly)
            else:
                check_command_dispatch(assembly)
        label = "command-native-conversion" + optimization
        obj = output / (label + ".o")
        run(flags + ["-DTELEMETRY_COMMAND_CONVERSION_PROBE", "-DTELEMETRY_ENUM_POSITION_PROBE",
                     "-c", "tests/CommandTableCodegen.cpp", "-o", str(obj)], label + "-compile")
        assembly = run([objdump, "-dr", "-C", str(obj)], label + "-disassembly")
        check_native_command_conversions(assembly)
        print(f"{optimization}: enum positions instruction-identical; native double/int command "
              "conversion has no extra operations versus direct checked call, no Scalar", flush=True)
        # nosys supplies link-only stubs. Their expected warnings do not
        # establish board behavior; this executable is deliberately not run.
        executable = output / ("consumer" + optimization + ".elf")
        run(flags + ["tests/EmbeddedLinkCheck.cpp", "lib/telemetry/abi/TelemetryAbi.cpp",
                     "lib/telemetry/serialization/TelemetryJson.cpp",
                     "--specs=nano.specs", "--specs=nosys.specs", "-Wl,-u,_printf_float",
                     "-o", str(executable)], "consumer" + optimization + "-link")
        abi_object = output / ("TelemetryAbi" + optimization + ".o")
        json_object = output / ("TelemetryJson" + optimization + ".o")
        command_object = output / ("TelemetryCommandJson" + optimization + ".o")
        modules = (("core", "TelemetryAbiLinkCheck", abi_object),
                   ("json", "TelemetryJsonAbiLinkCheck", json_object),
                   ("command", "TelemetryCommandAbiLinkCheck", command_object))
        for module, source, library_object in modules:
            mismatch = output / (source + "-mismatch" + optimization + ".o")
            run(flags + ["-DTELEMETRY_FORCE_CACHELINE=64", "-c", f"tests/{source}.cpp",
                         "-o", str(mismatch)], f"abi-{module}-mismatch{optimization}-compile")
            archive = output / (f"libTelemetry-{module}" + optimization + ".a")
            run([archiver, "rcs", str(archive), str(library_object)],
                f"abi-{module}-archive" + optimization)
            matching = output / (source + optimization + ".o")
            link_tail = ["--specs=nano.specs", "--specs=nosys.specs"]
            if module in ("json", "command"):
                link_tail.append("-Wl,-u,_printf_float")
            run(flags + [str(matching), str(archive), *link_tail,
                         "-o", str(output / (f"abi-{module}-match" + optimization + ".elf"))],
                f"abi-{module}-match" + optimization + "-link")
            run(flags + [str(mismatch), str(archive), *link_tail,
                         "-o", str(output / (f"abi-{module}-mismatch" + optimization + ".elf"))],
                f"abi-{module}-mismatch" + optimization + "-link",
                r"undefined reference|AbiTag|requireTelemetryAbi|schemaCrcAbi")
        print(f"{optimization}: {len(sources)} sources compiled, {probes} read-only probes checked, "
              "newlib-nano consumer and independent core/JSON/command ABI archives linked, mixed ABI rejected",
              flush=True)
        # Same translation unit, target and compiler: compare a hand-bound
        # descriptor with the inferred native-function path. Counts are bytes,
        # not measured cycles. A runtime Field wrapper remains the old path.
        manual = output / ("FactoryCodegen-manual" + optimization + ".o")
        run(flags + ["-DTELEMETRY_FACTORY_MANUAL", "-c", "tests/FactoryCodegen.cpp", "-o", str(manual)],
            "factory-manual" + optimization + "-compile")
        objects = {"manual": manual, "inferred": output / ("FactoryCodegen" + optimization + ".o")}
        record = {}
        for variant, obj in objects.items():
            symbols = run([objdump, "-t", str(obj)], "factory-" + variant + optimization + "-symbols")
            record[variant] = {line.split()[-1]: int(line.split()[-2], 16)
                              for line in symbols.splitlines()
                              if line.split() and line.split()[-1].startswith("factory_")
                              and " F " in line}
        for name in ("factory_known_read", "factory_free_read", "factory_runtime_read"):
            if name not in record["manual"] or name not in record["inferred"]:
                raise RuntimeError("Missing factory codegen symbol " + name)
            if record["inferred"][name] > record["manual"][name]:
                raise RuntimeError("Inferred factory grew the read wrapper: " + name)
        factory_report[optimization] = record
        print(f"{optimization}: factory read wrappers no larger than manual; free read "
              f'{record["manual"]["factory_free_read"]} -> {record["inferred"]["factory_free_read"]} bytes', flush=True)
    (output / "factory-codegen.json").write_text(json.dumps(factory_report, indent=2) + "\n", encoding="utf-8")
    for case in range(1, 19):
        if case in (1, 2, 3, 4, 13, 14):
            diagnostic = "position must be non-negative"
        elif 5 <= case <= 12:
            diagnostic = "outside (?:FieldTable|CommandTable)"
        else:
            diagnostic = "position must be an integer or enum"
        run([compiler, *FLAGS, f"-DTELEMETRY_POSITION_FAIL_CASE={case}",
             "-fsyntax-only", "tests/TelemetryPositionCompileFail.cpp"],
            f"position-rejection-{case}", diagnostic)
    print("18 invalid positions rejected on ARM32 before index narrowing", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        sys.exit(str(error))
