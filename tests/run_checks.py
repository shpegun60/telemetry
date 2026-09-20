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
          "TelemetryJsonCheck", "TelemetryNumericCheck", "TelemetryEnumCheck", "TelemetryLimitsCheck",
          "TelemetryFactoryCheck", "TelemetryCommandCheck", "TelemetryTableCheck", "TelemetryOwnerSlotCheck",
          "TelemetryFunctionSlotCheck", "TelemetryLateBoundCheck", "TelemetryTraversalCheck")
LIBRARY_SOURCES = ("lib/telemetry/abi/TelemetryAbi.cpp",
                   "lib/telemetry/serialization/TelemetryJson.cpp",
                   "lib/telemetry/serialization/TelemetryCommandJson.cpp")
FACTORY_REJECTIONS = {
    1: "exact same C\\+\\+ type", 2: "exact same C\\+\\+ type", 3: "return WriteResult",
    4: "noexcept", 5: "no matching|deleted", 6: "numeric or enum", 7: "Metadata values must match",
    8: "invalidFieldLimits", 9: "signature|noexcept|owner", 10: "no matching|deleted",
    11: "no matching|deleted", 12: "no matching|deleted", 13: "metadata count", 14: "Metadata values must match",
    15: "invalidFieldLimits", 16: "noexcept", 17: "without references", 18: "numeric or enum",
    19: "numeric or enum", 20: "return CommandResult", 21: "owner|invocable",
    22: "target cannot be null", 23: "deleted", 24: "no matching",
    25: "invalidFieldLimits", 26: "deleted", 27: "deleted", 28: "reserved for Scalar",
    29: "no matching|deleted", 30: "invalidFieldLimits", 31: "noexcept",
    32: "exact same C\\+\\+ type", 33: "noexcept", 34: "no matching",
    35: "no matching", 36: "deleted", 37: "no matching", 38: "no matching",
    39: "native numeric or bool", 40: "no matching",
    41: "noexcept", 42: "concrete operator|operator\\(\\)", 43: "concrete operator|operator\\(\\)",
    44: "no matching|deleted", 45: "exact enum type", 46: "exact same enum type",
    47: "codes must be unique", 48: "exact enum type", 49: "numeric value", 50: "deleted",
    51: "deleted", 52: "deleted",
    53: "positions must be unique", 54: "outside the function", 55: "Metadata values must match",
    56: "cannot mix positional", 57: "no matching|deleted", 58: "exact same C\\+\\+ type",
    59: "exact enum type|Metadata values must match", 60: "positions must be unique",
    61: "indexed arg", 62: "deleted", 63: "deleted", 64: "deleted",
    65: "noexcept", 66: "noexcept", 67: "concrete operator|operator\\(\\)",
    68: "concrete operator|operator\\(\\)", 69: "noexcept|invocable",
    70: "reserved for Scalar",
    71: "deleted", 72: "deleted", 73: "deleted",
    74: "deleted", 75: "outside CommandCatalogTable", 76: "deleted",
    77: "outside CommandTable", 78: "argument count",
    79: "native numeric or enum", 80: "native numeric or enum",
    81: "native numeric or enum", 82: "native numeric or enum",
}
REJECTIONS = {
    1: r"accepted prefix|positional bounds", 2: r"accepted prefix|positional bounds", 3: r"numeric or Bool declaredType",
    4: r"no matching", 5: r"constant\s+expression|constexpr",
    6: r"lvalue|not assignable", 7: r"deleted", 8: r"noexcept", 9: r"noexcept",
    10: r"no matching|cannot bind|expects an lvalue|deleted", 11: r"cannot be null",
    12: r"deleted", 13: r"deleted",
    14: r"requires an enum type", 15: r"does not have a name|must have names",
    16: r"codes must be unique", 17: r"dictionary is empty",
    18: r"no matching", 19: r"no matching",
    **{case: r"invalidFieldLimits" for case in range(20, 31)},
    **{case: r"deleted" for case in range(31, 36)},
    36: r"accepted prefix|positional bounds", 37: r"accepted prefix|positional bounds",
    38: r"no matching", 39: r"no matching",
    **{case: r"cannot be null" for case in range(40, 44)},
    **{case: r"conversion must be noexcept" for case in range(44, 47)},
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
        run(flags + build_flags + [f"tests/{suite}.cpp", *LIBRARY_SOURCES,
                                   "-o", str(executable)], suite + "-build")
        run([str(executable)], suite + "-run")
    for case, message in REJECTIONS.items():
        run(flags + [f"-DTELEMETRY_READ_FAIL_CASE={case}", "-fsyntax-only",
                     "tests/TelemetryReadCompileFail.cpp"], f"reject-{case}", message)
    print(f"{len(REJECTIONS)} expected compilation failures verified", flush=True)

    for case in range(1, 25):
        if case <= 3:
            diagnostic = r"invalidFieldFlags|constant expression|constexpr"
        elif case in (4, 5):
            diagnostic = r"convert|conversion"
        elif case <= 17:
            diagnostic = r"deleted"
        elif case in (18, 19, 20, 24):
            diagnostic = r"parameter visitor must be noexcept and return bool"
        elif case == 21:
            diagnostic = r"invoke_result|no matching|visit"
        else:
            diagnostic = r"const|read.only|discard.*qualifier"
        run(flags + [f"-DTELEMETRY_TRAVERSAL_FAIL_CASE={case}", "-fsyntax-only",
                     "tests/TelemetryTraversalCompileFail.cpp"], f"traversal-reject-{case}", diagnostic)
    print("24 metadata/visitor/traversal compilation rejections verified", flush=True)

    # A runtime policy gets the same validation as a constexpr definition. The
    # separate process must abort, not return normally or fail with an unrelated
    # access violation. Disable Windows' optional crash dialog for child tests.
    if os.name == "nt":
        import ctypes
        ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002)
    executable = output / ("TelemetryTraversalCheck.exe" if os.name == "nt" else "TelemetryTraversalCheck")
    for mode in ("readonly", "writeonly"):
        result = subprocess.run([str(executable), mode], cwd=output, env=environment,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
        (output / ("invalid-policy-" + mode + ".log")).write_bytes(result.stdout)
        aborted = result.returncode in ((3, -1073740791) if os.name == "nt" else (-6,))
        if not aborted:
            raise RuntimeError(f"Invalid {mode} persistent policy did not abort (exit {result.returncode})")
    print("2 runtime persistent-policy construction failures verified", flush=True)
    if args.std == "c++20":
        for case in range(1, 4):
            run(flags + [f"-DTELEMETRY_ADAPTER_FAIL_CASE={case}", "-fsyntax-only",
                         "tests/TelemetryAdapterCompileFail.cpp"],
                f"structural-adapter-reject-{case}", r"(?:setter invocation|adapter must).*noexcept")
        print("3 structural adapter compilation rejections verified", flush=True)
    for case in range(1, 11):
        run(flags + [f"-DTELEMETRY_COMMAND_LIFETIME_FAIL_CASE={case}", "-fsyntax-only",
                     "tests/TelemetryCommandLifetimeCompileFail.cpp"],
            f"command-lifetime-reject-{case}", r"deleted|no matching")
    print("10 command lifetime compilation rejections verified", flush=True)
    borrowed = output / ("BorrowedFieldLvalues" + (".exe" if os.name == "nt" else ""))
    run(flags + build_flags + ["tests/TelemetryBorrowedFieldCompileFail.cpp",
                              *LIBRARY_SOURCES, "-o", str(borrowed)], "borrowed-field-lvalues-build")
    run([str(borrowed)], "borrowed-field-lvalues-run")
    for case in range(1, 37):
        run(flags + [f"-DTELEMETRY_BORROWED_FIELD_FAIL_CASE={case}", "-fsyntax-only",
                     "tests/TelemetryBorrowedFieldCompileFail.cpp"],
            f"borrowed-field-reject-{case}", r"deleted|no matching")
    print("36 borrowed field compilation rejections verified", flush=True)
    for case in range(1, 10):
        run(flags + [f"-DTELEMETRY_FIELD_FAIL_CASE={case}", "-fsyntax-only", "tests/TelemetryFieldCompileFail.cpp"],
            f"immutable-field-{case}", r"deleted|const|read.only")
    print("9 immutable Field rejections verified", flush=True)
    for case, message in FACTORY_REJECTIONS.items():
        run(flags + [f"-DTELEMETRY_FACTORY_FAIL_CASE={case}", "-fsyntax-only",
                     "tests/TelemetryFactoryCompileFail.cpp"], f"factory-reject-{case}", message)
    print(f"{len(FACTORY_REJECTIONS)} factory/command compilation rejections verified", flush=True)

    for case in range(1, 27):
        run(flags + [f"-DTELEMETRY_TABLE_FAIL_CASE={case}", "-fsyntax-only",
                     "tests/TelemetryTableCompileFail.cpp"], f"table-reject-{case}",
            r"outside|deleted|no matching|requires group|native numeric or enum|accepts only|indexed arg|make(?:Field|Command).*(?:not|member)|no member named")
    print("26 positional table compilation rejections verified", flush=True)

    position_cases = range(1, 23 if args.std == "c++20" else 19)
    for case in position_cases:
        if case in (1, 2, 3, 4, 13, 14):
            message = r"position must be non-negative"
        elif 5 <= case <= 12:
            message = r"outside (?:FieldTable|CommandTable)"
        else:
            # Some compiler versions reject floating-point NTTPs before the
            # library can issue its explicit integer/enum diagnostic.
            message = r"position must be an integer or enum|non-type template argument.*not yet supported"
        run(flags + [f"-DTELEMETRY_POSITION_FAIL_CASE={case}", "-fsyntax-only",
                     "tests/TelemetryPositionCompileFail.cpp"], f"position-reject-{case}", message)
    print(f"{len(position_cases)} typed position compilation rejections verified", flush=True)

    for case in range(1, 19):
        if case in (8, 9):
            diagnostic = "Factory owner or parameter types"
        elif case in (13, 14):
            diagnostic = "OwnerSlot requires a non-volatile object type"
        elif case == 16:
            diagnostic = "no matching"
        else:
            diagnostic = "deleted"
        run(flags + [f"-DTELEMETRY_OWNER_SLOT_FAIL_CASE={case}", "-fsyntax-only",
                     "tests/TelemetryOwnerSlotCompileFail.cpp"],
            f"owner-slot-reject-{case}", diagnostic)
    print("18 owner slot lifetime and type rejections verified", flush=True)

    function_slot_rejections = {
        1: "FunctionSlot requires", 2: "FunctionSlot requires",
        3: "convert|conversion", 4: "convert|conversion", 5: "convert|conversion",
        **{case: "deleted" for case in range(6, 15)},
        15: "exact same C\\+\\+ type", 16: "return CommandResult",
        17: "no parameters", 18: "without references", 19: "volatile",
        20: "return WriteResult", 21: "no match|does not provide a call", 22: "deleted",
    }
    for case, diagnostic in function_slot_rejections.items():
        run(flags + [f"-DTELEMETRY_FUNCTION_SLOT_FAIL_CASE={case}", "-fsyntax-only",
                     "tests/TelemetryFunctionSlotCompileFail.cpp"],
            f"function-slot-reject-{case}", diagnostic)
    print("22 function slot lifetime and signature rejections verified", flush=True)

    slot_rejections = {
        **{case: "requires an R" for case in (1, 2, 3)},
        **{case: "deleted" for case in (4, 5, 6, 7, 17, 18, 19, 20, 21, 23, 24, 25, 26, 33, 34)},
        **{case: "noexcept" for case in (8, 9, 10, 14, 15, 16)},
        **{case: "convert|conversion" for case in (11, 30)},
        31: "convert|conversion|cannot initialize a parameter",
        12: "exceeds its inline", 13: "exceeds its inline", 22: "exact same C\\+\\+ type",
        27: "compatible reference", 28: "compatible reference", 29: "direct owner object", 32: "InlineBytes too small",
        **{case: "target signature must match exactly" for case in range(35, 43)},
    }
    for case, diagnostic in slot_rejections.items():
        run(flags + [f"-DTELEMETRY_LATE_BOUND_FAIL_CASE={case}", "-fsyntax-only",
                     "tests/TelemetryLateBoundCompileFail.cpp"],
            f"late-bound-reject-{case}", diagnostic)
    # A consumer enabling the companion delegate's heap mode must not change
    # telemetry's fixed-capacity slot contract.
    for case in (12, 13):
        run(flags + ["-DTINY_DELEGATE_ENABLE_HEAP_FALLBACK=1", f"-DTELEMETRY_LATE_BOUND_FAIL_CASE={case}",
                     "-fsyntax-only", "tests/TelemetryLateBoundCompileFail.cpp"],
            f"late-bound-no-heap-{case}", "exceeds its inline")
    print("42 late-bound lifetime/signature rejections and 2 no-heap controls verified", flush=True)

    empty = output / "HeaderCheck.cpp"
    empty.write_text("int main() {}\n", encoding="utf-8")
    for header in sorted((ROOT / "lib/telemetry").rglob("*.h")):
        label = "-".join(header.relative_to(ROOT / "lib/telemetry").with_suffix("").parts)
        run(flags + ["-include", str(header), "-fsyntax-only", str(empty)], label + "-standalone")
    for option in ("-ffast-math", "-ffinite-math-only", "-D_M_FP_FAST=1"):
        run(flags + [option, "-include", "core/TelemetryConversion.h", "-fsyntax-only", str(empty)],
            "reject" + option, r"Compile telemetry conversions without")
    print("Standalone headers and fast-math rejection verified", flush=True)

    cache = output / "CachelineCheck.cpp"
    cache.write_text('#include "core/TelemetryCacheline.h"\nstatic_assert(telemetry::cacheLineBytes == EXPECTED_SIZE);\n', encoding="utf-8")
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
    field_layout = output / "CachelineLayoutCheck.cpp"
    field_layout.write_text('#include "catalog/TelemetryCatalog.h"\n'
        '#include "command/TelemetryCommand.h"\n'
        'static_assert(alignof(telemetry::Field) == EXPECTED_SIZE);\n'
        'static_assert(offsetof(telemetry::Field, set) == EXPECTED_SIZE);\n'
        'static_assert(sizeof(telemetry::Field) % EXPECTED_SIZE == 0);\n'
        'static_assert(sizeof(telemetry::Command) == sizeof(void*) * 5);\n', encoding="utf-8")
    for size in (64, 128):
        run(flags + [f"-DTELEMETRY_FORCE_CACHELINE={size}", f"-DEXPECTED_SIZE={size}", "-fsyntax-only", str(field_layout)],
            f"cacheline-field-{size}")
    print("Field layout obeys explicit 64/128-byte alignment; Command stays compact", flush=True)

    # The core anchor is independently linkable, and compiled JSON entry points
    # carry the same exact tag. Equal layouts link normally; a caller built with
    # a different Field layout must fail instead of using incompatible offsets.
    abi_object = output / "TelemetryAbi-abi64.o"
    json_object = output / "TelemetryJson-abi64.o"
    command_object = output / "TelemetryCommandJson-abi64.o"
    abi64 = ["-DTELEMETRY_FORCE_CACHELINE=64"]
    run(flags + build_flags + abi64 + ["-c", LIBRARY_SOURCES[0], "-o", str(abi_object)],
        "abi64-core-compile")
    run(flags + build_flags + abi64 + ["-c", LIBRARY_SOURCES[1], "-o", str(json_object)],
        "abi64-json-compile")
    run(flags + build_flags + abi64 + ["-c", LIBRARY_SOURCES[2], "-o", str(command_object)],
        "abi64-command-compile")
    modules = (("core", "TelemetryAbiLinkCheck", abi_object),
               ("json", "TelemetryJsonAbiLinkCheck", json_object),
               ("command", "TelemetryCommandAbiLinkCheck", command_object))
    for module_id, (module, source, library_object) in enumerate(modules):
        archive = output / f"libTelemetry-{module}-abi64.a"
        caller_object = output / f"{source}-abi64.o"
        mismatch_object = output / f"{source}-abi128.o"
        executable = output / (source + (".exe" if os.name == "nt" else ""))
        run([args.ar, "rcs", str(archive), str(library_object)], f"abi64-{module}-archive")
        run(flags + build_flags + abi64 + ["-c", f"tests/{source}.cpp", "-o", str(caller_object)],
            f"abi64-{module}-caller-compile")
        run(flags + build_flags + [str(caller_object), str(archive), "-o", str(executable)],
            f"abi64-{module}-link")
        run([str(executable)], f"abi64-{module}-run")
        run(flags + build_flags + ["-DTELEMETRY_FORCE_CACHELINE=128", "-c",
            f"tests/{source}.cpp", "-o", str(mismatch_object)], f"abi128-{module}-caller-compile")
        run(flags + build_flags + [str(mismatch_object), str(archive), "-o", str(executable)],
            f"abi-{module}-mismatch-link", r"undefined reference|unresolved external|AbiTag")
        legacy = output / f"LegacyAbi6-{module}.o"
        run(flags + build_flags + abi64 + [f"-DTELEMETRY_LEGACY_ABI_MODULE={module_id}", "-c",
            "tests/abi/LegacyAbi6Link.cpp", "-o", str(legacy)], f"abi6-{module}-caller-compile")
        run(flags + build_flags + [str(legacy), str(archive), "-o", str(executable)],
            f"abi6-{module}-mismatch-link", r"undefined reference|unresolved external|AbiTag")
    print("Independent core/JSON/command ABI archives linked; mixed alignments and ABI 6/7 rejected", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        sys.exit(str(error))
