#!/usr/bin/env python3
"""Supported fixed-wire-type and enum dictionary checks. Ruslan Kovtun, codexAi. MIT."""

import argparse
import os
from pathlib import Path
import re
import subprocess
import time


ROOT = Path(__file__).resolve().parents[3]
SOURCES = Path(__file__).resolve().parent

# Each negative case must fail for its intended contract, not merely for any
# compiler error. PFR's wording differs between GCC and Clang for the three
# constructs it cannot decompose before MemberType is available.
ENUM_DIAGNOSTICS = {
    1: r"Duplicate explicit enum code",
    2: r"All enumCodes values must have one exact enum type",
    3: r"enumCodes value has no backend name; use enumEntry",
    4: r"Enum name length is outside 1\.\.4096 bytes",
    5: r"abort",
    6: r"abort",
    7: r"abort",
    8: r"no matching function.*enumEntry",
    9: r"Structured enums must be scoped",
    10: r"Structured enum underlying type must be a supported",
    11: r"abort",
    12: r"abort",
    13: r"abort",
    14: r"abort",
    15: r"Invalid name fails|abort",
    16: r"All enumEntries values must have one exact enum type",
    17: r"Structured enum underlying type must be a supported",
    18: r"Structured enums must be scoped",
    19: r"(no matching function|constraints not satisfied|constraint failure)",
}

TYPE_DIAGNOSTICS = {
    1: r"Unsupported structured wire type",
    2: r"Structured members must be mutable value types",
    3: r"(structured binding|SimpleAggregate|decomposes into)",
    4: r"Structured wire structs must be standard-layout trivial aggregates",
    5: r"bit-field",
    6: r"Structured wire structs must be standard-layout trivial aggregates",
    7: r"Structured wire structs must be standard-layout trivial aggregates",
    8: r"Unsupported structured wire type",
    9: r"Array has too many elements",
    10: r"Array expanded nodes exceed supported limit",
    11: r"Structured members must be mutable value types",
    12: r"Structured wire structs must be standard-layout trivial aggregates",
    13: r"Structured wire structs must be standard-layout trivial aggregates",
    14: r"Structured wire structs must be standard-layout trivial aggregates",
    15: r"Structured wire structs must be standard-layout trivial aggregates",
    16: r"Automatic reflected member names must be ASCII identifiers",
    17: r"(packed field|Packed aggregate member alignment is unsupported)",
    18: r"Array wire size exceeds supported limit",
    19: r"Array element must be mutable value type",
    20: r"Type nesting depth exceeds supported limit",
    21: r"Unsupported structured wire type",
    22: r"Unsupported structured wire type",
    23: r"(packed field|Packed aggregate member alignment is unsupported)",
    24: r"Inherited types are not supported",
}


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

    def run(command: list[str], label: str, expected_failure: str = "") -> None:
        start = time.perf_counter()
        result = subprocess.run(
            command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, encoding="utf-8", errors="replace", timeout=120,
        )
        elapsed = time.perf_counter() - start
        log = f"COMMAND {command!r}\nEXIT {result.returncode}\nSECONDS {elapsed:.3f}\n{result.stdout}"
        (output / f"{label}.log").write_text(log, encoding="utf-8")
        if expected_failure:
            if result.returncode == 0 or re.search(expected_failure, result.stdout, re.I | re.S) is None:
                raise RuntimeError(f"{label} did not reject for the intended reason\n{log}")
        elif result.returncode != 0:
            raise RuntimeError(f"{label} failed\n{log}")
        print(f"{label}: {'expected rejection' if expected_failure else 'pass'} ({elapsed:.2f}s)",
              flush=True)

    run([args.cxx, "--version"], "compiler-version")
    flags = [
        args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic-errors",
        "-fdiagnostics-color=never", "-Ilib", "-Ilib/boost_pfr/include",
        "-Ilib/magic_enum",
    ]
    if args.arm:
        flags += [
            "-mcpu=cortex-m7", "-mthumb", "-mfpu=fpv5-d16", "-mfloat-abi=hard",
            "-fno-exceptions", "-fno-rtti", "-ffunction-sections", "-fdata-sections",
        ]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]

    optimizations = ("O2", "Os", "Og") if args.arm else ("O2",)
    for optimization in optimizations:
        for source in ("EnumProbe.cpp", "TypesProbe.cpp"):
            stem = Path(source).stem
            obj = output / f"{optimization}-{stem}.o"
            run(flags + [f"-{optimization}", "-c", str(SOURCES / source),
                         "-o", str(obj)], f"{optimization}-{stem}")
            if not args.arm:
                program = output / (f"{stem}.exe" if os.name == "nt" else stem)
                run(flags + [str(obj), "-o", str(program)], f"link-{stem}")
                run([str(program)], f"run-{stem}")

    for source, cases in (("EnumNegative.cpp", ENUM_DIAGNOSTICS),
                          ("TypesNegative.cpp", TYPE_DIAGNOSTICS)):
        for case, diagnostic in cases.items():
            label = f"negative-{Path(source).stem}-{case}"
            run(flags + ["-O2", f"-DCASE={case}", "-fsyntax-only",
                         str(SOURCES / source)], label, expected_failure=diagnostic)

    print(f"Stage 03: 2 positive probes and {len(ENUM_DIAGNOSTICS) + len(TYPE_DIAGNOSTICS)} "
          "negative diagnostics passed", flush=True)


if __name__ == "__main__":
    main()
