#!/usr/bin/env python3
"""Review helper (slots): compile each slot compile-fail case and print the
first error line, so each rejection can be checked for its intended reason.
The shipped runner only regex-searches the whole log.

Usage: first_errors.py --cxx g++ --std c++17 --out build/review-slots/first-errors-gcc17.txt
"""
import argparse
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
CASES = (
    ("TELEMETRY_OWNER_SLOT_FAIL_CASE", "tests/TelemetryOwnerSlotCompileFail.cpp", range(1, 19), []),
    ("TELEMETRY_FUNCTION_SLOT_FAIL_CASE", "tests/TelemetryFunctionSlotCompileFail.cpp", range(1, 23), []),
    ("TELEMETRY_LATE_BOUND_FAIL_CASE", "tests/TelemetryLateBoundCompileFail.cpp", range(1, 43), []),
    ("TELEMETRY_LATE_BOUND_FAIL_CASE", "tests/TelemetryLateBoundCompileFail.cpp", (12, 13, 14, 15, 16),
     ["-DTINY_DELEGATE_ENABLE_HEAP_FALLBACK=1"]),
)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cxx", default="g++")
    parser.add_argument("--std", default="c++17")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    flags = [args.cxx, "-std=" + args.std, "-Wall", "-Wextra", "-Werror", "-pedantic-errors",
             "-fdiagnostics-color=never", "-Ilib/telemetry", "-Ilib/delegate", "-fsyntax-only"]
    lines = []
    for macro, source, cases, extra in CASES:
        for case in cases:
            result = subprocess.run(flags + extra + [f"-D{macro}={case}", source], cwd=ROOT,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                    encoding="utf-8", errors="replace", timeout=180)
            errors = [line for line in result.stdout.splitlines() if re.search(r"\berror\b", line)]
            first = errors[0] if errors else "<no error>"
            # Keep only the message after the location for compact review.
            first = re.sub(r"^.*?error:\s*", "", first)
            tag = " ".join(extra)
            lines.append(f"{Path(source).stem} {case} {tag} exit={result.returncode} errors={len(errors)} :: {first[:220]}")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
