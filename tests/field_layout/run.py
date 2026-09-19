#!/usr/bin/env python3
"""Build isolated A/B/C telemetry Field overlays and compare ARM layout/codegen."""
import argparse
import difflib
import hashlib
import json
import os
from pathlib import Path
import re
import runpy
import shutil
import struct
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
HOST_CHECKS = runpy.run_path(str(ROOT / "tests/run_checks.py"))
COMMON = ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic-errors", "-fdiagnostics-color=never"]
ARM = ["-mcpu=cortex-m7", "-mthumb", "-mfpu=fpv5-d16", "-mfloat-abi=hard", "-fno-exceptions", "-fno-rtti"]
VARIANTS = {"A": 0, "B": 1, "C": 2, "A32": 3, "B32": 4, "B64": 5, "Current": 6}
METRICS = ("magic", "version", "size", "alignment", "get", "set", "type", "flags",
           "declaredType", "id", "name", "unit", "getter_size", "setter_size", "field_type_size", "count")


def digest(path):
    return hashlib.sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError(f"Expected exactly one source anchor: {old[:90]!r}")
    return text.replace(old, new, 1)


def prepare(output, variants=("A", "B", "C")):
    manifest = json.loads((HERE / "baseline.json").read_text(encoding="utf-8"))

    def pinned(relative):
        result = subprocess.run(["git", "show", manifest["commit"] + ":" + relative], cwd=ROOT,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
        if result.returncode:
            raise RuntimeError(f"Cannot read pinned baseline {relative}: {result.stderr.decode(errors='replace')}")
        return result.stdout

    baseline_files = {}
    for relative, expected in manifest["files"].items():
        data = pinned(relative)
        if hashlib.sha256(data.replace(b"\r\n", b"\n")).hexdigest() != expected:
            raise RuntimeError(f"Pinned source hash mismatch: {relative}")
        baseline_files[relative] = data
    original = baseline_files["lib/telemetry/TelemetryCatalog.h"].decode("utf-8").replace("\r\n", "\n")
    first = original.index("struct Field {\n")
    last = original.index("\nstruct Catalog {", first)
    field = original[first:last]
    methods = field[field.index("    // declaredType is the value contract"):]
    if not methods.endswith("};\n"):
        raise RuntimeError("Unexpected stable Field terminator")
    methods = methods[:-3]
    candidates = {"A": original}
    reordered = "struct Field {\n" + (HERE / "FieldB.members.inc").read_text(encoding="utf-8") + "\n" + methods + "};\n"
    candidates["B"] = original[:first] + reordered + original[last:]
    c_methods = methods.replace("declaredType", "type")
    c_methods = replace_once(c_methods, "type.acceptsConverted_(converted)", "acceptsConverted_(converted)")
    aligned = replace_once((HERE / "FieldC.inc").read_text(encoding="utf-8"),
                           "    // @STABLE_METHODS@", c_methods.rstrip())
    candidates["C"] = original[:first] + aligned + "\n" + original[last:]
    for source in ("A", "B"):
        candidates[source + "32"] = replace_once(candidates[source], "struct Field {", "struct alignas(32) Field {")
    candidates["B64"] = replace_once(candidates["B"], "struct Field {", "struct alignas(64) Field {")
    candidates["Current"] = (ROOT / "lib/telemetry/TelemetryCatalog.h").read_text(encoding="utf-8")
    for variant in variants:
        header = candidates[variant]
        directory = output / variant
        if variant == "Current":
            shutil.copytree(ROOT / "lib", directory / "lib", dirs_exist_ok=True)
        else:
            for relative, data in baseline_files.items():
                destination = directory / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(data)
        target = directory / "lib/telemetry/TelemetryCatalog.h"
        target.write_text(header, encoding="utf-8", newline="\n")
        difference = "".join(difflib.unified_diff(original.splitlines(keepends=True), header.splitlines(keepends=True),
                                                 fromfile="stable/TelemetryCatalog.h", tofile=variant + "/TelemetryCatalog.h"))
        (directory / "Field.diff").write_text(difference, encoding="utf-8")
        test_dir = directory / "tests"
        test_dir.mkdir(exist_ok=True)
        rejection_source = ((ROOT / "tests/TelemetryReadCompileFail.cpp").read_bytes() if variant == "Current"
                            else pinned("tests/TelemetryReadCompileFail.cpp"))
        (test_dir / "TelemetryReadCompileFail.cpp").write_bytes(rejection_source)
        for suite in HOST_CHECKS["SUITES"]:
            text = ((ROOT / "tests" / (suite + ".cpp")).read_text(encoding="utf-8") if variant == "Current"
                    else pinned("tests/" + suite + ".cpp").decode("utf-8").replace("\r\n", "\n"))
            if variant in ("B", "C", "B32", "B64") and suite in ("TelemetryCheck", "TelemetryEnumCheck"):
                text = replace_once(text, "std::is_aggregate_v<Field> && std::is_trivially_copyable_v<Field>",
                                    "!std::is_aggregate_v<Field> && std::is_trivially_copyable_v<Field>")
            if variant in ("C", "A32", "B32") and suite == "TelemetryCheck":
                text = replace_once(text, "sizeof(Field) == 80", "sizeof(Field) == 96")
            if variant == "B64" and suite == "TelemetryCheck":
                text = replace_once(text, "sizeof(Field) == 80", "sizeof(Field) == 128")
            if variant == "C" and suite == "TelemetryCheck":
                text = replace_once(text, "    Field changedType[] = {a[0]};\n    changedType[0].declaredType = ScalarType::U32;",
                    '    const Field changedType[] = {{a[0].id, a[0].name, a[0].unit, ScalarType::U32, a[0].get, a[0].set}};')
                text = replace_once(text, '    Field changedUnit[] = {a[0]};\n    changedUnit[0].unit = "V";',
                    '    const Field changedUnit[] = {{a[0].id, a[0].name, "V", a[0].declaredType, a[0].get, a[0].set}};')
            if variant == "C" and suite == "TelemetryWriteCheck":
                text = replace_once(text, "    Field readonly[] = {rows[0]};\n    readonly[0].set = nullptr;",
                    '    const Field readonly[] = {{rows[0].id, rows[0].name, rows[0].unit, rows[0].declaredType, rows[0].get, nullptr}};')
            (test_dir / (suite + ".cpp")).write_text(text, encoding="utf-8", newline="\n")
    return manifest


def run(command, log, env=None, rejection=None):
    result = subprocess.run([str(part) for part in command], cwd=ROOT, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, encoding="utf-8", errors="replace", timeout=300)
    log.parent.mkdir(parents=True, exist_ok=True)
    log.write_text(result.stdout, encoding="utf-8")
    valid = result.returncode == 0 if rejection is None else (
        result.returncode != 0 and re.search(rejection, result.stdout, re.IGNORECASE))
    if not valid:
        raise RuntimeError(f"{log.name} failed (exit {result.returncode}):\n{result.stdout[-8000:]}")
    return result.stdout


def includes(directory):
    return ["-I" + str(directory / "lib/telemetry"), "-I" + str(directory / "lib/delegate")]


def sections(text):
    return {name: int(size, 16) for name, size in
            re.findall(r"^\s*\d+\s+(\S+)\s+([0-9a-fA-F]+)\s", text, re.MULTILINE)}


def symbols(text):
    result = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) == 4:
            result[parts[0]] = {"kind": parts[1], "address": int(parts[2], 16), "size": int(parts[3], 16)}
    return result


def instruction_counts(text):
    result, current = {}, None
    for line in text.splitlines():
        match = re.match(r"^[0-9a-fA-F]+ <(.+)>:$", line)
        if match:
            current = match[1]
            result[current] = 0
        elif current and re.match(r"^\s*[0-9a-fA-F]+:\s+[a-z][a-z0-9.]*\b", line):
            result[current] += 1
    return result


def arm_checks(args, output):
    compiler = shutil.which(args.arm_cxx)
    if compiler is None:
        raise RuntimeError(f"ARM compiler not found: {args.arm_cxx}")
    tool_dir = Path(compiler).parent
    suffix = ".exe" if os.name == "nt" else ""
    tools = {name: tool_dir / ("arm-none-eabi-" + name + suffix) for name in ("objdump", "objcopy", "nm")}
    version = run([compiler, "--version"], output / "arm-compiler.log").splitlines()[0]
    if run([compiler, "-dumpmachine"], output / "arm-target.log").strip() != "arm-none-eabi":
        raise RuntimeError("Expected an arm-none-eabi compiler")
    results = {"compiler": version, "flags": COMMON + ARM, "variants": {}}
    for variant in args.variants:
        number = VARIANTS[variant]
        directory = output / variant
        common = [compiler, *COMMON, *ARM, *includes(directory), f"-DTELEMETRY_LAYOUT_VARIANT={number}"]
        metrics_obj = directory / "Layout.o"
        run(common + ["-O2", "-c", HERE / "Layout.cpp", "-o", metrics_obj], directory / "layout-compile.log")
        raw = directory / "layout.bin"
        run([tools["objcopy"], "--dump-section", f".rodata.layout_metrics={raw}", metrics_obj], directory / "layout-extract.log")
        words = struct.unpack("<" + "I" * len(METRICS), raw.read_bytes())
        metrics = dict(zip(METRICS, words))
        if metrics["magic"] != 0x4c41594f or metrics["version"] != 1:
            raise RuntimeError("Unrecognized ARM layout data")
        for key in ("type", "flags"):
            if metrics[key] == 0xffffffff:
                metrics[key] = None
        result = {"layout": metrics, "optimizations": {}}
        for optimization in ("O2", "Os"):
            build = directory / optimization
            build.mkdir(exist_ok=True)
            flags = common + ["-" + optimization]
            objects = {}
            for name, source in (("Probe", HERE / "Probe.cpp"), ("Json", directory / "lib/telemetry/TelemetryJson.cpp")):
                obj = build / (name + ".o")
                run(flags + ["-c", source, "-o", obj], build / (name + "-compile.log"))
                header = run([tools["objdump"], "-h", obj], build / (name + "-sections.log"))
                sizes = sections(header)
                if not sizes or "file format elf32-littlearm" not in header:
                    raise RuntimeError("Missing ARM object sections")
                if any(size for key, size in sizes.items() if key.startswith((".init_array", ".preinit_array", ".ctors"))):
                    raise RuntimeError(f"{variant}/{name}: unexpected startup initialization")
                totals = {label: sum(size for key, size in sizes.items() if key.startswith(label))
                          for label in (".text", ".rodata", ".data", ".bss")}
                if totals[".data"] or totals[".bss"]:
                    raise RuntimeError(f"{variant}/{name}: unexpected writable storage")
                sym = symbols(run([tools["nm"], "-S", "--defined-only", "--format=posix", obj], build / (name + "-symbols.log")))
                run([tools["objdump"], "-dr", "-C", obj], build / (name + ".asm"))
                instructions = instruction_counts(run([tools["objdump"], "-d", "--no-show-raw-insn", obj], build / (name + "-instructions.log")))
                functions = {key: {"bytes": item["size"], "instructions": instructions.get(key)}
                             for key, item in sym.items() if key.startswith("layout_") and item["kind"] in ("T", "W")}
                if name == "Probe":
                    table = sym.get("layout_fields")
                    if not table or table["kind"] != "R" or table["size"] != metrics["size"] * 1024:
                        raise RuntimeError(f"{variant}: wrong field table storage/extent")
                    if len(functions) != 21:
                        raise RuntimeError(f"{variant}: expected 21 probe functions, found {len(functions)}")
                objects[name] = {"sections": totals, "functions": functions,
                                 "sha256": hashlib.sha256(obj.read_bytes()).hexdigest()}
            run(flags + [ROOT / "tests/EmbeddedLinkCheck.cpp", directory / "lib/telemetry/TelemetryJson.cpp",
                         "--specs=nano.specs", "--specs=nosys.specs", "-Wl,-u,_printf_float",
                         "-o", build / "consumer.elf"], build / "consumer-link.log")
            result["optimizations"][optimization] = objects
            print(f"ARM {variant} {optimization}: size={metrics['size']}, align={metrics['alignment']}, "
                  f"probe text={objects['Probe']['sections']['.text']}, rodata={objects['Probe']['sections']['.rodata']}", flush=True)
        results["variants"][variant] = result
    (output / "arm-results.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    write_report(results, output / "ARM_RESULTS.md")


def write_report(results, target):
    variants = results["variants"]
    lines = ["# " + "/".join(variants) + " ARM object measurements", "", results["compiler"], "",
             "No board execution or cycle measurements. Sizes below are bytes.", "",
             "| Variant | sizeof | alignof | get | set | type | flags | declaredType | id | name | unit |",
             "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
    for name, result in variants.items():
        layout = result["layout"]
        lines.append("| " + name + " | " + " | ".join(str(layout[key]) if layout[key] is not None else "absent"
            for key in ("size", "alignment", "get", "set", "type", "flags", "declaredType", "id", "name", "unit")) + " |")
    lines += ["", "## Object sections", "", "| Variant | Optimization | Object | .text | .rodata | .data | .bss |",
              "|---|---|---|---:|---:|---:|---:|"]
    for name, result in variants.items():
        for opt, objects in result["optimizations"].items():
            for obj, record in objects.items():
                lines.append(f"| {name} | {opt} | {obj} | " + " | ".join(str(x) for x in record["sections"].values()) + " |")
    for opt in ("O2", "Os"):
        lines += ["", f"## {opt} probe bodies", "", "Cells show bytes / static instruction count. Function bytes include literal pools; instruction counts exclude them.",
                  "Counts cover all branches, not the instructions executed by one call, and are not cycle counts.",
                  "Aliases may have no separate disassembly count. Helper functions are included in object .text totals above.", "",
                  "| Function | " + " | ".join(variants) + " |", "|---|" + "---:|" * len(variants)]
        names = next(iter(variants.values()))["optimizations"][opt]["Probe"]["functions"]
        for name in sorted(names):
            cells = []
            for variant in variants:
                record = variants[variant]["optimizations"][opt]["Probe"]["functions"][name]
                cells.append(f"{record['bytes']} / {record['instructions']}")
            lines.append(f"| {name} | " + " | ".join(cells) + " |")
    target.write_text("\n".join(lines) + "\n", encoding="utf-8")


def host_checks(args, output):
    environment = os.environ.copy()
    environment.setdefault("ASAN_OPTIONS", "detect_leaks=1:detect_stack_use_after_return=1")
    environment.setdefault("UBSAN_OPTIONS", "halt_on_error=1")
    build_flags = ["-O2"] if not args.sanitize else ["-O1", "-g", "-fno-omit-frame-pointer",
        "-fsanitize=address,undefined,float-cast-overflow", "-fsanitize-address-use-after-scope", "-fno-sanitize-recover=all"]
    mode = args.std + ("-sanitized" if args.sanitize else "-release")
    documents = {}
    report = {"compiler": run([args.host_cxx, "--version"], output / ("host-compiler-" + mode + ".log")).splitlines()[0],
              "mode": mode, "variants": {}}
    for variant in args.variants:
        number = VARIANTS[variant]
        directory = output / variant
        build = directory / ("host-" + mode)
        build.mkdir(exist_ok=True)
        flags = [args.host_cxx, *[f for f in COMMON if not f.startswith("-std=")], "-std=" + args.std,
                 *includes(directory), f"-DTELEMETRY_LAYOUT_VARIANT={number}"]
        counts = {}
        for suite in (*HOST_CHECKS["SUITES"], "Contracts"):
            source = HERE / "Contracts.cpp" if suite == "Contracts" else directory / "tests" / (suite + ".cpp")
            exe = build / (suite + (".exe" if os.name == "nt" else ""))
            run(flags + build_flags + [source, directory / "lib/telemetry/TelemetryJson.cpp", "-o", exe], build / (suite + "-compile.log"), environment)
            result = run([exe], build / (suite + "-run.log"), environment)
            if suite == "Contracts":
                documents[variant] = [json.loads(line) for line in result.splitlines()]
            else:
                match = re.search(r"(\d+)/(\d+) .*checks passed", result)
                if not match or match[1] != match[2] or "SKIP" in result:
                    raise RuntimeError(f"{variant}/{suite}: missing, failed or skipped coverage")
                counts[suite] = int(match[1])
        for case, diagnostic in HOST_CHECKS["REJECTIONS"].items():
            run(flags + [f"-DTELEMETRY_READ_FAIL_CASE={case}", "-fsyntax-only", directory / "tests/TelemetryReadCompileFail.cpp"],
                build / f"reject-{case}.log", environment, diagnostic)
        if variant == "C":
            for case in range(1, 6):
                run(flags + [f"-DLAYOUT_FAIL_CASE={case}", "-fsyntax-only", HERE / "ImmutableCompileFail.cpp"],
                    build / f"immutable-reject-{case}.log", environment, r"deleted|const|read.only")
        report["variants"][variant] = {"checks": counts, "rejections": len(HOST_CHECKS["REJECTIONS"]),
                                       "immutable_rejections": 5 if variant == "C" else 0}
        print(f"Host {variant} {mode}: {sum(counts.values())} checks, 35 rejections, constructor/schema contracts passed", flush=True)
    if any(document != next(iter(documents.values())) for document in documents.values()):
        raise RuntimeError("Schema or value documents differ between variants")
    report["identical_json"] = True
    (output / ("host-results-" + mode + ".json")).write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("arm", "host", "all"), default="all")
    parser.add_argument("--arm-cxx", default=os.environ.get("ARM_CXX", "arm-none-eabi-g++"))
    parser.add_argument("--host-cxx", default=os.environ.get("CXX", "g++"))
    parser.add_argument("--std", choices=("c++17", "c++20"), default="c++17")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--variants", nargs="+", choices=tuple(VARIANTS), default=["A", "B", "C"])
    parser.add_argument("--build-dir", type=Path, required=True)
    args = parser.parse_args()
    if len(args.variants) != len(set(args.variants)):
        parser.error("Select each variant only once")
    output = args.build_dir.resolve()
    if output == ROOT or output in ROOT.parents or output.is_relative_to(ROOT / "lib") or output.is_relative_to(ROOT / "tests"):
        raise RuntimeError("Build output must be separate from source directories")
    output.mkdir(parents=True, exist_ok=True)
    baseline = prepare(output, args.variants)
    provenance = {"baseline": baseline["commit"], "files": {str(p.relative_to(HERE)): digest(p)
                  for p in HERE.iterdir() if p.is_file()}}
    (output / "experiment-sources.json").write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
    if args.mode in ("arm", "all"):
        arm_checks(args, output)
    if args.mode in ("host", "all"):
        host_checks(args, output)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        sys.exit(str(error))
