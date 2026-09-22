#!/usr/bin/env python3
"""C++20 resource checks, contract rejections and Cortex-M7 compilation (MIT).

Authors: Ruslan Kovtun (shpegun60), codexAi.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
from stack_check import check_usage, self_test as check_stack_controls

ROOT = Path(__file__).resolve().parents[2]
TELEMETRY = ["lib/telemetry/abi/TelemetryAbi.cpp"]
ADAPTERS = [f"lib/resource/telemetry/{name}.cpp" for name in ("SchemaFile", "CommandsFile", "ValuesFile")]
PROTOCOL = ["lib/resource/protocol/Protocol.cpp"]
DEVICE = ["app/resources/DeviceResources.cpp", "app/demo/DemoCatalog.cpp"]
HEADERS = [str(p.relative_to(ROOT / "lib")).replace("\\", "/")
           for p in sorted((ROOT / "lib/resource").rglob("*.hpp"))]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--arm", action="store_true")
    parser.add_argument("--node", help="Node executable for the browser decoder checks")
    parser.add_argument("--objdump", default="arm-none-eabi-objdump")
    parser.add_argument("--size", default="arm-none-eabi-size")
    args = parser.parse_args()
    check_stack_controls()
    build = args.build_dir.resolve()
    build.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    if args.sanitize:
        env.setdefault("ASAN_OPTIONS", "detect_leaks=1:detect_stack_use_after_return=1")
        env.setdefault("UBSAN_OPTIONS", "halt_on_error=1")

    def run(command, name, text=None, reject=False):
        result = subprocess.run(command, cwd=ROOT, env=env, input=text,
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (build / (name + ".log")).write_text("COMMAND " + repr(command) + "\n" + result.stdout, encoding="utf-8")
        if (result.returncode == 0) == reject:
            raise RuntimeError(name + "\n" + result.stdout)
        if result.returncode == 0 and result.stdout and not name.startswith(("compile", "header", "negative", "dump", "link")):
            print(result.stdout.strip(), flush=True)
        return result.stdout

    flags = [args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic-errors",
             "-fdiagnostics-color=never", "-Ilib", "-Ilib/telemetry", "-Ilib/delegate"]
    if args.arm:
        flags += ["-mcpu=cortex-m7", "-mthumb", "-mfpu=fpv5-d16", "-mfloat-abi=hard",
                  "-fno-exceptions", "-fno-rtti", "-ffunction-sections", "-fdata-sections"]
    if args.sanitize:
        flags += ["-O1", "-g", "-fno-omit-frame-pointer", "-fsanitize=address,undefined,float-cast-overflow",
                  "-fsanitize-address-use-after-scope", "-fno-sanitize-recover=all"]
    run([args.cxx, "--version"], "compiler")
    for n, header in enumerate(HEADERS):
        run(flags + ["-x", "c++", "-fsyntax-only", "-"], f"header-{n}", f"#include <{header}>\n")
    for case in range(12):
        run(flags + [f"-DCASE={case}", "-fsyntax-only", "tests/resources/Negative.cpp"],
            f"negative-{case}", reject=case != 0)
    sources = TELEMETRY + ADAPTERS + PROTOCOL + DEVICE
    # Only object builds need .su output. Syntax-only checks otherwise leave
    # a--.su / a-Negative.su beside the repository sources on ARM GCC.
    object_flags = flags + (["-fstack-usage"] if args.arm else [])
    for opt in (("O2", "Os") if args.arm else ("O1" if args.sanitize else "O2",)):
        objects = {}
        for src in sources:
            obj = build / f"{opt}-{Path(src).stem}.o"
            run(object_flags + ["-" + opt, "-c", src, "-o", str(obj)], f"compile-{opt}-{obj.stem}")
            objects[src] = str(obj)
            if args.arm and src in ADAPTERS + PROTOCOL:
                symbols = run([args.objdump, "-tC", str(obj)], f"dump-object-symbols-{opt}-{Path(src).stem}")
                if re.search(r"malloc|calloc|realloc|operator new", symbols):
                    raise RuntimeError("Heap dependency entered resource code: " + src)
                if re.search(r"__aeabi_lmul|__muldi3", symbols):
                    raise RuntimeError("Software 64-bit multiply entered resource code: " + src)

        if args.arm:
            stack = {}
            for source in ADAPTERS + PROTOCOL:
                stem = Path(source).stem
                report = build / f"{opt}-{stem}.su"
                stack[stem] = check_usage(report.read_text(encoding="utf-8"),
                    required=stem if stem.endswith("File") else None)
            (build / f"stack-usage-{opt}.log").write_text(json.dumps(stack, indent=2), encoding="utf-8")
            print(f"ARM {opt} bounded stack guards passed", flush=True)
            obj = build / f"{opt}-ArmProbe.o"
            run(object_flags + ["-" + opt, "-c", "tests/resources/ArmProbe.cpp", "-o", str(obj)], f"compile-{opt}-probe")
            dump = run([args.objdump, "-drC", str(obj)], f"dump-{opt}")
            sections = run([args.objdump, "-t", str(obj)], f"dump-sections-{opt}")
            if not re.search(r"\.rodata\S*\s+\S+\s+resource_probe_files", sections):
                raise RuntimeError("Resource table did not land in constant storage")
            for name in ("resource_probe_read", "resource_probe_known", "resource_probe_stat",
                         "resource_probe_fingerprint", "resource_probe_value_lookup"):
                match = re.search(rf"<{name}>:\n(.*?)(?=\nDisassembly|\n[0-9a-f]+ <|\Z)", dump, re.S)
                if not match:
                    raise RuntimeError("Missing dispatch probe: " + name)
                body = match.group(1)
                if re.search(r"malloc|operator new|strcmp|strlen|memcmp", body):
                    raise RuntimeError("Unexpected runtime work in " + name)
                if name == "resource_probe_known" and re.search(r"\bblx\b|\bbx\s+r[0-9]+", body):
                    raise RuntimeError("Known provider retained indirect dispatch")
                if name == "resource_probe_value_lookup":
                    instructions = re.findall(r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{4}\s+)+([^\n]+)", body, re.M)
                    if len(instructions) > 40 or re.search(r"\bbl(?:\.w)?\b|\bblx\b", body):
                        raise RuntimeError("Direct value lookup gained a call or excessive work")
                    # No backwards branches: the selected descriptor is reached
                    # through two bounds checks, never an ordinal loop.
                    for address, instruction in instructions:
                        branch = re.match(r"b(?:eq|ne|hi|ls|cc|cs|lo|hs|ge|gt|le|lt|\.n|\.w)?(?:\.\w+)?\s+([0-9a-f]+)\b", instruction)
                        if branch and int(branch[1], 16) < int(address, 16):
                            raise RuntimeError("Direct value lookup acquired a backwards branch")
            # The hash itself uses the hardware widening multiply. The probe's
            # call to that shared hash loop is intentional; libgcc helpers are not.
            kernel = re.search(
                r"<telemetry_resource::detail::Fingerprint::bytes\([^\n]*>:\n"
                r"(.*?)(?=\nDisassembly|\n[0-9a-f]+ <|\Z)", dump, re.S)
            if (not kernel or not re.search(r"\bumull\b", kernel[1])
                    or re.search(r"\bbl(?:\.w)?\b|\bblx\b", kernel[1])
                    or re.search(r"__aeabi_lmul|__muldi3", dump)):
                raise RuntimeError("Fingerprint lost its hardware multiply")
            elf = build / f"{opt}-resources.elf"
            run(flags + ["-" + opt, str(obj), *objects.values(), "--specs=nano.specs", "--specs=nosys.specs",
                         "-Wl,--gc-sections", "-o", str(elf)], f"link-{opt}")
            run([args.size, "-A", str(elf)], f"size-{opt}")
            symbols = run([args.objdump, "-tC", str(elf)], f"dump-symbols-{opt}")
            if re.search(r"snprintf|_printf_float|localeconv|strtod|to_chars|ryu|schubfach|POW10|\bpow[f l]?\b|schemaCrc", symbols, re.I):
                raise RuntimeError("Text formatting or JSON dependency entered the binary resource image")
        else:
            tests = {
                "CoreCheck": PROTOCOL,
                "BinaryCheck": [],
                "StreamCheck": [],
                "TelemetryFilesCheck": TELEMETRY + ADAPTERS + PROTOCOL,
                "LocalityCheck": TELEMETRY + ADAPTERS,
                "DeviceCheck": sources,
                "NoHeapCheck": TELEMETRY + ADAPTERS + PROTOCOL,
            }
            for name, deps in tests.items():
                exe = build / (name + (".exe" if os.name == "nt" else ""))
                run(flags + ["-" + opt, f"tests/resources/{name}.cpp", *(objects[d] for d in deps), "-o", str(exe)], "compile-" + name)
                run([str(exe), *([str(build / "fixtures")] if name == "TelemetryFilesCheck" else [])], name)
            if args.node:
                run([args.node, "tests/resources/DecoderCheck.mjs", str(build / "fixtures")], "DecoderCheck")
            for adapter in range(3):
                for mismatch in (False, True):
                    options = [f"-DADAPTER={adapter}"]
                    if mismatch:
                        options += ["-DTELEMETRY_FORCE_CACHELINE=128"]
                    exe = build / f"abi-{adapter}-{int(mismatch)}.exe"
                    run(flags + options + ["tests/resources/AbiCheck.cpp",
                        *(objects[d] for d in TELEMETRY + ADAPTERS), "-o", str(exe)],
                        f"abi-{adapter}-{int(mismatch)}", reject=mismatch)
                run(flags + [f"-DADAPTER={adapter}", "-DLEGACY_ABI7", "tests/resources/AbiCheck.cpp",
                    *(objects[d] for d in TELEMETRY + ADAPTERS), "-o", str(build / f"abi7-{adapter}.exe")],
                    f"abi7-{adapter}", reject=True)
    print(f"Resources: {len(HEADERS)} standalone headers, 11 contract rejections + control; "
          + ("ARM O2/Os compile/link/layout/codegen passed" if args.arm else "7 host suites + 3 ABI controls and alignment/ABI7 rejections passed"), flush=True)


if __name__ == "__main__":
    main()
