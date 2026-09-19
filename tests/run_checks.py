#!/usr/bin/env python3
"""Build and run telemetry checks with a GCC-compatible C++ driver, without Qt."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parent.parent
SUITES = ("TelemetryCheck", "TelemetryWriteCheck", "TelemetryReadCheck",
          "TelemetryJsonCheck", "TelemetryNumericCheck", "TelemetryEnumCheck", "TelemetryLimitsCheck")
REJECTIONS = {
    1: r"accepted prefix", 2: r"accepted prefix", 3: r"numeric or Bool declaredType",
    4: r"no matching", 5: r"constant\s+expression|constexpr",
    6: r"lvalue|not assignable", 7: r"deleted", 8: r"noexcept", 9: r"noexcept",
    10: r"no matching|cannot bind|expects an lvalue", 11: r"cannot be null",
    12: r"deleted", 13: r"deleted",
    14: r"requires an enum type", 15: r"does not have a name|must have names",
    16: r"codes must be unique", 17: r"dictionary is empty",
    18: r"no matching", 19: r"no matching",
    **{case: r"invalidFieldLimits" for case in range(20, 31)},
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
    parser.add_argument("--std", choices=("c++17", "c++20"), default="c++17")
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    output = args.build_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    flags = [args.cxx, "-std=" + args.std, "-Wall", "-Wextra", "-Werror",
             "-pedantic-errors", "-fdiagnostics-color=never",
             "-Ilib/telemetry", "-Ilib/delegate"]
    build_flags = ["-O2"]
    if args.sanitize:
        build_flags = ["-O1", "-g", "-fno-omit-frame-pointer",
                       "-fsanitize=address,undefined,float-cast-overflow",
                       "-fno-sanitize-recover=all"]
    environment = os.environ.copy()
    environment.setdefault("ASAN_OPTIONS", "detect_leaks=1")
    environment.setdefault("UBSAN_OPTIONS", "halt_on_error=1")

    def run(command, label, rejection=None):
        result = subprocess.run(command, cwd=ROOT, env=environment,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, encoding="utf-8", errors="replace", timeout=180)
        (output / (label + ".log")).write_text(result.stdout, encoding="utf-8")
        if rejection is not None:
            valid = result.returncode != 0 and re.search(rejection, result.stdout, re.IGNORECASE)
        else:
            valid = result.returncode == 0
        if not valid:
            print(result.stdout, file=sys.stderr)
            raise RuntimeError(f"{label} failed (exit {result.returncode}); see {output}")
        if label.endswith("-run"):
            for line in result.stdout.splitlines():
                if line.startswith("SKIP") or "checks passed" in line:
                    print(line, flush=True)

    for suite in SUITES:
        executable = output / (suite + (".exe" if os.name == "nt" else ""))
        run(flags + build_flags + [f"tests/{suite}.cpp", "lib/telemetry/TelemetryJson.cpp",
                                   "-o", str(executable)], suite + "-build")
        run([str(executable)], suite + "-run")
    for case, message in REJECTIONS.items():
        run(flags + [f"-DTELEMETRY_READ_FAIL_CASE={case}", "-fsyntax-only",
                     "tests/TelemetryReadCompileFail.cpp"], f"reject-{case}", message)
    print(f"{len(REJECTIONS)} expected compilation failures verified", flush=True)

    empty = output / "HeaderCheck.cpp"
    empty.write_text("int main() {}\n", encoding="utf-8")
    for header in sorted((ROOT / "lib/telemetry").glob("*.h")):
        run(flags + ["-include", str(header), "-fsyntax-only", str(empty)], header.stem + "-standalone")
    for option in ("-ffast-math", "-ffinite-math-only"):
        run(flags + [option, "-include", "TelemetryConversion.h", "-fsyntax-only", str(empty)],
            "reject" + option, r"Compile telemetry conversions without")
    print("Standalone headers and fast-math rejection verified", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        sys.exit(str(error))
