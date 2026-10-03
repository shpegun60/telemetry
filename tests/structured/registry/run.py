#!/usr/bin/env python3
"""Stage 05 TypeRegistry, immutable storage and exact ABI link checks.

Authors: Ruslan Kovtun (shpegun60), codexAi. MIT license.
"""

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import time


ROOT = Path(__file__).resolve().parents[3]
SOURCES = Path(__file__).resolve().parent
ABI_SOURCE = ROOT / "lib/telemetry/abi/StructuredAbi.cpp"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
    parser.add_argument("--arm", action="store_true")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--build-dir", type=Path, required=True)
    args = parser.parse_args()
    if args.arm and args.sanitize:
        parser.error("--arm and --sanitize cannot be combined")

    output = args.build_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)

    def run(command: list[str], label: str, expected_failure: str = "") -> str:
        start = time.perf_counter()
        result = subprocess.run(
            command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, encoding="utf-8", errors="replace", timeout=180,
        )
        duration = time.perf_counter() - start
        log = f"COMMAND {command!r}\nEXIT {result.returncode}\nSECONDS {duration:.3f}\n{result.stdout}"
        (output / f"{label}.log").write_text(log, encoding="utf-8")
        if expected_failure:
            if result.returncode == 0 or re.search(expected_failure, result.stdout, re.I) is None:
                raise RuntimeError(f"{label} did not fail for the intended reason\n{log}")
        elif result.returncode:
            raise RuntimeError(f"{label} failed\n{log}")
        print(f"{label}: {'expected rejection' if expected_failure else 'pass'} "
              f"({duration:.2f}s)", flush=True)
        return result.stdout

    run([args.cxx, "--version"], "compiler-version")
    flags = [
        args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic-errors",
        "-fdiagnostics-color=never", "-Ilib", "-Ilib/boost_pfr/include",
        "-Ilib/magic_enum",
    ]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    if args.arm:
        flags += [
            "-mcpu=cortex-m7", "-mthumb", "-mfpu=fpv5-d16", "-mfloat-abi=hard",
            "-fno-exceptions", "-fno-rtti", "-ffunction-sections", "-fdata-sections",
        ]

    optimizations = ("O2", "Os", "Og") if args.arm else ("O2",)
    for optimization in optimizations:
        name = f"{optimization}-registry"
        extension = ".elf" if args.arm else (".exe" if os.name == "nt" else "")
        program = output / (name + extension)
        link_flags = ["-nostdlib", "-Wl,-e,main", "-Wl,--gc-sections", "-lgcc"] if args.arm else []
        run(flags + [f"-{optimization}", str(SOURCES / "RegistryProbe.cpp"),
                     str(SOURCES / "RegistryOther.cpp"), str(ABI_SOURCE),
                     *link_flags, "-o", str(program)], f"link-{name}")
        if not args.arm:
            run([str(program)], f"execute-{name}")

        for case, diagnostic in ((1, "Type is absent from this TypeRegistry"),
                                 (2, "TypeId is outside this TypeRegistry")):
            run(flags + [f"-{optimization}", f"-DCASE={case}", "-fsyntax-only",
                         str(SOURCES / "RegistryNegative.cpp")],
                f"negative-{optimization}-{case}", diagnostic)

        # The same definition must resolve the ordinary tag and reject tags
        # that differ in revision, a layout size, or an offset.
        for case in (1, 2, 3):
            run(flags + [f"-{optimization}", f"-DCASE={case}",
                         str(SOURCES / "AbiMismatch.cpp"), str(ABI_SOURCE),
                         *link_flags, "-o", str(output / f"mismatch-{optimization}-{case}.elf")],
                f"abi-mismatch-{optimization}-{case}",
                r"(undefined reference|undefined symbol).*requireStructuredAbi")

    if args.arm:
        tool = Path(args.cxx)
        suffix = tool.suffix
        objdump = tool.with_name("arm-none-eabi-objdump" + suffix)
        size = tool.with_name("arm-none-eabi-size" + suffix)
        objdump_command = str(objdump) if objdump.exists() else shutil.which("arm-none-eabi-objdump")
        size_command = str(size) if size.exists() else shutil.which("arm-none-eabi-size")
        if not objdump_command or not size_command:
            raise RuntimeError("The ARM compiler's objdump and size tools are required")
        for optimization in optimizations:
            obj = output / f"{optimization}-RegistryArm.o"
            run(flags + [f"-{optimization}", "-c", str(SOURCES / "RegistryArm.cpp"),
                         "-o", str(obj)], f"compile-{optimization}-RegistryArm")
            sections = run([objdump_command, "-h", str(obj)], f"sections-{optimization}")
            if not re.search(r"\.rodata\.[^\s]*descriptors_E\s+[0-9a-f]{8}", sections):
                raise RuntimeError(f"{optimization}: registry descriptors are not in .rodata")
            if re.search(r"\.(?:data|bss)(?:\.[^\s]+)?\s+(?!0{8})[0-9a-f]{8}", sections):
                raise RuntimeError(f"{optimization}: registry emitted mutable storage")
            linked = output / f"{optimization}-registry.elf"
            sizes = run([size_command, "-A", str(linked)], f"size-{optimization}")
            if not re.search(r"^\.rodata\s+[1-9][0-9]*\s+", sizes, re.M):
                raise RuntimeError(f"{optimization}: linked registry lost .rodata")
            if re.search(r"^\.(?:data|bss)\s+[1-9][0-9]*\s+", sizes, re.M):
                raise RuntimeError(f"{optimization}: linked registry has mutable sections")
            if re.search(r"^\.(?:preinit_array|init_array|ctors)\s+[1-9][0-9]*\s+",
                         sizes, re.M):
                raise RuntimeError(f"{optimization}: registry requires runtime initialization")

    print("Stage 05 TypeRegistry and ABI checks passed", flush=True)


if __name__ == "__main__":
    main()
