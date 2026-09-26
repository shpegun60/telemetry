"""CI integration for confirmed technical-review regressions (MIT).

Authors: Ruslan Kovtun (shpegun60), codexAi.
Original probes remain archived in tests/review; maintained checks live here.
"""
import os
from pathlib import Path
import subprocess


def check_contracts(flags, run):
    cases = {**{i: r"actual object|direct owner" for i in (1, 2, 3, 23)},
             **{i: r"deleted" for i in (4, 5, 6, 7, 16, 17, 21, 22, 24)},
             **{i: r"private" for i in (8, 9)},
             **{i: r"invalidFieldFlags" for i in (10, 11)},
             **{i: r"invalidIdComponent" for i in (12, 13, 14)},
             15: r"deleted", 18: r"invalidFieldLimits|invalidCommandName",
             **{i: r"signature must match exactly" for i in (19, 20, 25)}}
    for case in range(26):
        run(flags + [f"-DCASE={case}", "-fsyntax-only", "tests/regression/ReviewCompileFail.cpp"],
            f"review-contract-{case}", cases.get(case))
    for name, count, diagnostic in (("OwnerLifetimeCompileFail", 18, "deleted"),
                                    ("PointerOwnerCompileFail", 9, "actual object|direct owner")):
        for case in range(count + 1):
            run(flags + [f"-DCASE={case}", "-fsyntax-only", f"tests/regression/{name}.cpp"],
                f"review-{name}-{case}", diagnostic if case else None)
    run(flags + ["-fno-delete-null-pointer-checks", "-fsyntax-only",
                 "tests/regression/NullChecksFlag.cpp"], "review-null-pointer-control")


def check(args, flags, build_flags, run, output, library_sources, environment):
    suites = ("ReviewCheck", "NumericEdges", "CommandArityCheck",
              "JsonBoundaryCheck", "SlotEdgesCheck", "HonorFlagsCheck")
    for name in suites:
        executable = output / (name + (".exe" if os.name == "nt" else ""))
        run(flags + build_flags + [f"tests/regression/{name}.cpp", *library_sources,
                                  "-o", str(executable)], f"review-{name}-build")
        run([str(executable)], f"review-{name}-run")

    # One type per build preserves the complete matrix while staying within
    # CI's compiler time/memory budget, including ASan and UBSan at -O1.
    for value_type in range(1, 19):
        name = f"FieldParityCheck-{value_type}"
        executable = output / (name + (".exe" if os.name == "nt" else ""))
        run(flags + build_flags + [f"-DPARITY_TYPE={value_type}",
            "tests/regression/FieldParityCheck.cpp", *library_sources, "-o", str(executable)],
            f"review-{name}-build")
        run([str(executable)], f"review-{name}-run")

    check_contracts(flags, run)

    # Bad runtime definitions must terminate intentionally. A normal return or
    # an unrelated memory access failure is not an acceptable negative control.
    for name, modes in (("RuntimeLimitsAbort", [None]), ("RuntimeMetadataAbort", [str(i) for i in range(1, 8)])):
        executable = output / (name + (".exe" if os.name == "nt" else ""))
        run(flags + build_flags + [f"tests/regression/{name}.cpp", *library_sources,
                                  "-o", str(executable)], f"review-{name}-build")
        for mode in modes:
            result = subprocess.run([str(executable), *([mode] if mode else [])], cwd=output,
                                    env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    timeout=30)
            (output / f"review-{name}-{mode}.log").write_bytes(result.stdout)
            if result.returncode not in ((3, -1073740791) if os.name == "nt" else (-6,)):
                raise RuntimeError(f"{name}({mode}) did not abort intentionally: {result.returncode}")

    # Clang has no option macros for individual no-honor flags. Check the
    # header's forced diagnostic even when the command line disables it.
    version = subprocess.check_output([args.cxx, "--version"], text=True).lower()
    if "clang" in version:
        for option in ("-fno-honor-nans", "-fno-honor-infinities"):
            for optimization in ("-O1", "-O2"):
                run(flags + [optimization, option, "-Wno-error", "-Wno-nan-infinity-disabled",
                             "tests/regression/HonorFlagsCheck.cpp", "-fsyntax-only"],
                    f"review-reject{option}{optimization}", r"currently enabled floating-point options")
        run(flags + ["-ffp-model=fast", "-include", "core/TelemetryConversion.h", "-x", "c++",
                     "-fsyntax-only", os.devnull], "review-reject-fp-model-fast",
            r"Compile telemetry conversions without")
    print("Review regression suites, 52 diagnostic rejections, null-pointer and runtime-abort controls passed", flush=True)
