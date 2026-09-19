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
    10: r"no matching|cannot bind|expects an lvalue|deleted", 11: r"cannot be null",
    12: r"deleted", 13: r"deleted",
    14: r"requires an enum type", 15: r"does not have a name|must have names",
    16: r"codes must be unique", 17: r"dictionary is empty",
    18: r"no matching", 19: r"no matching",
    **{case: r"invalidFieldLimits" for case in range(20, 31)},
    **{case: r"deleted" for case in range(31, 36)},
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
    parser.add_argument("--ar", default=os.environ.get("AR", "ar"))
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
                       "-fsanitize-address-use-after-scope",
                       "-fno-sanitize-recover=all"]
    environment = os.environ.copy()
    environment.setdefault("ASAN_OPTIONS", "detect_leaks=1:detect_stack_use_after_return=1")
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
    for case in range(1, 10):
        run(flags + [f"-DTELEMETRY_FIELD_FAIL_CASE={case}", "-fsyntax-only", "tests/TelemetryFieldCompileFail.cpp"],
            f"immutable-field-{case}", r"deleted|const|read.only")
    print("9 immutable Field rejections verified", flush=True)

    empty = output / "HeaderCheck.cpp"
    empty.write_text("int main() {}\n", encoding="utf-8")
    for header in sorted((ROOT / "lib/telemetry").glob("*.h")):
        run(flags + ["-include", str(header), "-fsyntax-only", str(empty)], header.stem + "-standalone")
    for option in ("-ffast-math", "-ffinite-math-only"):
        run(flags + [option, "-include", "TelemetryConversion.h", "-fsyntax-only", str(empty)],
            "reject" + option, r"Compile telemetry conversions without")
    print("Standalone headers and fast-math rejection verified", flush=True)

    cache = output / "CachelineCheck.cpp"
    cache.write_text('#include "TelemetryCacheline.h"\nstatic_assert(telemetry::cacheLineBytes == EXPECTED_SIZE);\n', encoding="utf-8")
    cache_cases = [
        (64, ["-DTELEMETRY_FORCE_CACHELINE=64"]),
        (128, ["-DTELEMETRY_FORCE_CACHELINE=128"]),
        (64, ["-DTELEMETRY_CACHELINE_BYTES=64"]),
        (64, ["-DTELEMETRY_CACHELINE_BYTES=64", "-DTELEMETRY_FORCE_CACHELINE=64"]),
        (32, ["-D__ARM_ARCH_PROFILE=77"]),
        (32, ["-D__ARM_ARCH_8M_BASE__=1"]),
        (32, ["-D__ARM_ARCH_PROFILE=77", "-D__APPLE__=1", "-D__aarch64__=1"]),
        (128, ["-D__APPLE__=1", "-D__aarch64__=1"]),
        (64, ["-D__ARM_ARCH_PROFILE=77", "-DTELEMETRY_FORCE_CACHELINE=64"]),
    ]
    for case, (expected, definitions) in enumerate(cache_cases):
        run(flags + definitions + [f"-DEXPECTED_SIZE={expected}", "-fsyntax-only", str(cache)], f"cacheline-{case}")
    for value in (0, 16, 48, -32):
        run(flags + [f"-DTELEMETRY_FORCE_CACHELINE={value}", "-DEXPECTED_SIZE=64", "-fsyntax-only", str(cache)],
            f"cacheline-reject-{value}", r"must be a power of two")
    run(flags + ["-DTELEMETRY_FORCE_CACHELINE=32", "-DTELEMETRY_CACHELINE_BYTES=64", "-DEXPECTED_SIZE=64", "-fsyntax-only", str(cache)],
        "cacheline-conflict", r"Conflicting telemetry cache-line overrides")
    print("9 cache-line configurations and 5 invalid overrides verified", flush=True)
    field_layout = output / "CachelineFieldCheck.cpp"
    field_layout.write_text('#include "TelemetryCatalog.h"\n'
        'static_assert(alignof(telemetry::Field) == EXPECTED_SIZE);\n'
        'static_assert(offsetof(telemetry::Field, set) == EXPECTED_SIZE);\n'
        'static_assert(sizeof(telemetry::Field) % EXPECTED_SIZE == 0);\n', encoding="utf-8")
    for size in (64, 128):
        run(flags + [f"-DTELEMETRY_FORCE_CACHELINE={size}", f"-DEXPECTED_SIZE={size}", "-fsyntax-only", str(field_layout)],
            f"cacheline-field-{size}")
    print("Field layout obeys explicit 64/128-byte alignment", flush=True)

    # The ABI tag is carried by the compiled JSON entry points. Equal layouts
    # link normally; a caller built with a different Field layout must fail at
    # link time instead of using incompatible member offsets or array strides.
    json_object = output / "TelemetryJson-abi64.o"
    archive = output / "libTelemetryAbi64.a"
    caller_object = output / "TelemetryAbiLinkCheck-abi64.o"
    mismatch_object = output / "TelemetryAbiLinkCheck-abi128.o"
    executable = output / ("TelemetryAbiLinkCheck" + (".exe" if os.name == "nt" else ""))
    abi64 = ["-DTELEMETRY_FORCE_CACHELINE=64"]
    run(flags + build_flags + abi64 + ["-c", "lib/telemetry/TelemetryJson.cpp", "-o", str(json_object)],
        "abi64-json-compile")
    run([args.ar, "rcs", str(archive), str(json_object)], "abi64-archive")
    run(flags + build_flags + abi64 + ["-c", "tests/TelemetryAbiLinkCheck.cpp", "-o", str(caller_object)],
        "abi64-caller-compile")
    run(flags + build_flags + [str(caller_object), str(archive), "-o", str(executable)],
        "abi64-link")
    run([str(executable)], "abi64-run")
    run(flags + build_flags + ["-DTELEMETRY_FORCE_CACHELINE=128", "-c",
        "tests/TelemetryAbiLinkCheck.cpp", "-o", str(mismatch_object)], "abi128-caller-compile")
    run(flags + build_flags + [str(mismatch_object), str(archive), "-o", str(executable)],
        "abi-mismatch-link", r"undefined reference|unresolved external|AbiTag")
    print("Matching telemetry ABI archive linked; mixed 64/128-byte layouts were rejected", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        sys.exit(str(error))
