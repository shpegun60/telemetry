#!/usr/bin/env python3
"""Review helper (commands slice): print the first compiler error of each
numbered compile-fail case, so a case that fails for an unintended reason
(typo, unrelated error) is visible even when the runner's broad regex matches.

Usage: python tests/review/commands/first_errors.py --cxx g++ --std c++17
           --out build/review-commands/first-errors [--suite table|lifetime|factory]
Run from the repository root. Logs go only to --out.
"""
import argparse
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SUITES = {
    "table": ("tests/TelemetryTableCompileFail.cpp", "TELEMETRY_TABLE_FAIL_CASE", range(1, 27)),
    "lifetime": ("tests/TelemetryCommandLifetimeCompileFail.cpp",
                 "TELEMETRY_COMMAND_LIFETIME_FAIL_CASE", range(1, 11)),
    "factory": ("tests/TelemetryFactoryCompileFail.cpp", "TELEMETRY_FACTORY_FAIL_CASE", range(1, 83)),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default="g++")
    parser.add_argument("--std", default="c++17")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--suite", choices=sorted(SUITES), action="append")
    parser.add_argument("--cases", help="comma-separated subset")
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    flags = [args.cxx, "-std=" + args.std, "-Wall", "-Wextra", "-Werror", "-pedantic-errors",
             "-fdiagnostics-color=never", "-Ilib/telemetry", "-Ilib/delegate", "-fsyntax-only"]
    subset = {int(c) for c in args.cases.split(",")} if args.cases else None
    for suite in args.suite or sorted(SUITES):
        source, macro, cases = SUITES[suite]
        for case in cases:
            if subset and case not in subset:
                continue
            result = subprocess.run(flags + [f"-D{macro}={case}", source], cwd=ROOT,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    text=True, encoding="utf-8", errors="replace", timeout=300)
            (out / f"{suite}-{case}.log").write_text(result.stdout, encoding="utf-8")
            errors = [line for line in result.stdout.splitlines() if re.search(r"\berror\b", line)]
            first = errors[0] if errors else "(no error line)"
            first = re.sub(r"^.*?(error)", r"\1", first)
            status = "rejected" if result.returncode != 0 else "ACCEPTED"
            print(f"{suite}-{case}: {status}: {len(errors)} errors: {first[:230]}")


if __name__ == "__main__":
    main()
