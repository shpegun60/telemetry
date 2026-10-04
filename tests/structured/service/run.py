#!/usr/bin/env python3
"""Native Service bindings, intended diagnostics and offline ARM stack checks.

Authors: Ruslan Kovtun (shpegun60), codexAi. MIT license.
"""

import argparse
import os
from pathlib import Path
import re
import struct
import subprocess
import time


ROOT = Path(__file__).resolve().parents[3]
SOURCES = Path(__file__).resolve().parent

DIAGNOSTICS = {
    1: r"Service request must be an aggregate struct or void",
    2: r"Service response must be an aggregate struct or void",
    3: r"Request must be by value or const lvalue reference",
    4: r"Structured endpoints must be noexcept",
    5: r"Use one request structure",
    6: r"Response must be a value or void",
    7: r"deleted function",
    8: r"deleted function",
    9: r"no matching function",
    10: r"Service method must be noexcept-invocable on this owner",
    11: r"deleted function",
    12: r"deleted function",
    13: r"no matching function|deleted function",
    14: r"deleted function",
    15: r"deleted function",
    16: r"deleted function",
    17: r"Service must return Response, void or the exact ServiceResult",
    18: r"deleted function",
    19: r"Service target cannot be nullptr",
    20: r"Service target cannot be nullptr",
}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
    parser.add_argument("--arm", action="store_true")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--null-checks", action="store_true")
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
                raise RuntimeError(f"{label} missed its intended diagnostic\n{log}")
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
    if args.null_checks:
        flags += ["-fno-delete-null-pointer-checks"]
    if args.arm:
        flags += [
            "-mcpu=cortex-m7", "-mthumb", "-mfpu=fpv5-d16", "-mfloat-abi=hard",
            "-fno-exceptions", "-fno-rtti", "-ffunction-sections", "-fdata-sections",
            "-fstack-usage",
        ]

    optimizations = ("O2", "Os", "Og") if args.arm else ("O2",)
    for optimization in optimizations:
        source = SOURCES / "ServiceProbe.cpp"
        if args.arm:
            obj = output / f"{optimization}-ServiceProbe.o"
            run(flags + [f"-{optimization}", "-c", str(source), "-o", str(obj)],
                f"compile-{optimization}-ServiceProbe")

            codegen = output / f"{optimization}-ArmServiceCodegen.o"
            run(flags + [f"-{optimization}", "-c",
                         str(SOURCES / "ArmServiceCodegen.cpp"), "-o", str(codegen)],
                f"compile-{optimization}-ArmServiceCodegen")
            usage = codegen.with_suffix(".su").read_text(encoding="utf-8")
            for symbol in ("call_big_raw", "call_big_wrapped"):
                match = re.search(rf"\b{symbol}\([^\n]*\)\s*\t(\d+)\tstatic", usage)
                if match is None or int(match.group(1)) > 256:
                    raise RuntimeError(f"{optimization} {symbol} hides a large stack frame")
                print(f"{optimization} {symbol}: {match.group(1)} B frame", flush=True)

            objcopy = str(Path(args.cxx).with_name(
                "arm-none-eabi-objcopy.exe" if os.name == "nt"
                else "arm-none-eabi-objcopy"))
            layout_file = output / f"{optimization}-service-layout.bin"
            run([objcopy, "--dump-section",
                 f".rodata.service_layout={layout_file}", str(codegen)],
                f"inspect-{optimization}-service-layout")
            layout_bytes = layout_file.read_bytes()
            if len(layout_bytes) != 24:
                raise RuntimeError(f"{optimization} Service layout section is malformed")
            result_size, result_align, direct_size, direct_align, owner_size, owner_align = \
                struct.unpack("<6I", layout_bytes)
            if not 4097 <= result_size <= 4100 or not 1 <= result_align <= 4 or \
                    direct_size > 8 or owner_size > 8 or \
                    not 1 <= direct_align <= 4 or not 1 <= owner_align <= 4:
                raise RuntimeError(f"{optimization} Service layout exceeds the ARM budget")
            print(f"{optimization} layout: result {result_size}/{result_align}, "
                  f"direct {direct_size}/{direct_align}, "
                  f"owner {owner_size}/{owner_align} B/alignment", flush=True)

            if optimization != "Og" and not args.null_checks:
                objdump = str(Path(args.cxx).with_name(
                    "arm-none-eabi-objdump.exe" if os.name == "nt"
                    else "arm-none-eabi-objdump"))
                assembly = run([objdump, "-d", "--disassemble=call_direct_owner",
                                str(codegen)], f"inspect-{optimization}-direct-owner")
                function = assembly.split("<call_direct_owner>:", 1)
                if len(function) != 2 or re.search(r"\b(?:cbz|cbnz|beq|bne)\b",
                                                   function[1]):
                    raise RuntimeError(f"{optimization} direct owner acquired a null branch")
        else:
            extension = ".exe" if os.name == "nt" else ""
            program = output / f"ServiceProbe{extension}"
            run(flags + [f"-{optimization}", str(source), "-o", str(program)],
                f"compile-{optimization}-ServiceProbe")
            run([str(program)], f"execute-{optimization}-ServiceProbe")

    negative_flags = [flag for flag in flags if flag != "-fstack-usage"]
    for case, diagnostic in DIAGNOSTICS.items():
        run(negative_flags + ["-O2", f"-DCASE={case}", "-fsyntax-only",
                              str(SOURCES / "ServiceNegative.cpp")],
            f"negative-{case}", diagnostic)

    # Weak-function override semantics are ELF-specific. The absent weak
    # declaration and the weak body are tested with and without a strong TU.
    if not args.arm and os.name != "nt":
        for override, expected in ((False, 11), (True, 22)):
            sources = [str(SOURCES / "WeakProbe.cpp")]
            if override:
                sources.append(str(SOURCES / "WeakOverride.cpp"))
            program = output / ("weak-override" if override else "weak-original")
            run(flags + ["-O2", f"-DEXPECT_OVERRIDE={expected}", *sources,
                         "-o", str(program)], f"compile-weak-{expected}")
            run([str(program)], f"execute-weak-{expected}")

    print("Stage 06 native Service checks passed", flush=True)


if __name__ == "__main__":
    main()
