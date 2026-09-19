#!/usr/bin/env python3
"""Compile Cortex-M7 checks, verify constant storage and link a newlib-nano consumer."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parent.parent
FLAGS = ["-std=c++17", "-mcpu=cortex-m7", "-mthumb", "-mfpu=fpv5-d16",
         "-mfloat-abi=hard", "-fno-exceptions", "-fno-rtti", "-Wall", "-Wextra",
         "-Werror", "-pedantic-errors", "-fdiagnostics-color=never",
         "-Ilib/telemetry", "-Ilib/delegate"]


def check_probe(name, headers, symbols):
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
    if name == "IndexCodegen":
        expected = {"telemetry_probe_index": 8, "telemetry_probe_catalogs": 32,
                    "telemetry_probe_group0_fields": 4 * 96,
                    "telemetry_probe_group1_fields": 3 * 96}
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
    output = args.build_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)

    def run(command, label):
        result = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True,
                                encoding="utf-8", errors="replace", timeout=180)
        (output / (label + ".log")).write_text(result.stdout, encoding="utf-8")
        if result.returncode != 0:
            print(result.stdout, file=sys.stderr)
            raise RuntimeError(f"{label} failed (exit {result.returncode}); see {output}")
        return result.stdout

    print(run([compiler, "--version"], "compiler-version").splitlines()[0], flush=True)
    target = run([compiler, "-dumpmachine"], "compiler-target").strip()
    if target != "arm-none-eabi":
        raise RuntimeError(f"Expected arm-none-eabi, got {target}")
    sources = [ROOT / "lib/telemetry/TelemetryJson.cpp", ROOT / "app/demo/DemoCatalog.cpp"]
    sources += sorted(source for source in (ROOT / "tests").glob("*.cpp")
                      if source.name != "TelemetryReadCompileFail.cpp")
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
                check_probe(source.stem, headers, symbols)
                run([objdump, "-dr", "-C", str(obj)], label + "-disassembly")
                probes += 1
        # nosys supplies link-only stubs. Their expected warnings do not
        # establish board behavior; this executable is deliberately not run.
        executable = output / ("consumer" + optimization + ".elf")
        run(flags + ["tests/EmbeddedLinkCheck.cpp", "lib/telemetry/TelemetryJson.cpp",
                     "--specs=nano.specs", "--specs=nosys.specs", "-Wl,-u,_printf_float",
                     "-o", str(executable)], "consumer" + optimization + "-link")
        print(f"{optimization}: {len(sources)} sources compiled, {probes} read-only probes checked, "
              "newlib-nano consumer linked", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        sys.exit(str(error))
