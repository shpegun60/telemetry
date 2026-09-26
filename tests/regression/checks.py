"""CI integration for confirmed technical-review regressions (MIT).

Authors: Ruslan Kovtun (shpegun60), codexAi.
Original probes remain archived in tests/review; maintained checks live here.
"""
import os
from pathlib import Path
import re
import subprocess


def check_abi_retention(flags, run, output, abi_object, alignment, label,
                        link_flags=(), execute=True):
    """Exercise compiler emission and linker collection as separate boundaries."""
    source = "tests/regression/AbiRetention.cpp"
    base = flags + ["-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections"]
    checked = 0
    # Neither a header-only include nor a compiler-omitted internal function
    # promises participation. They must keep working without the ABI object.
    for form in (0, 2):
        exe = output / f"{label}-omitted-{form}.exe"
        run(base + [f"-DABI_RETENTION_FORM={form}", source, *link_flags, "-o", str(exe)],
            f"{label}-omitted-{form}-link")
        checked += 1
    clang = "clang" in subprocess.check_output([flags[0], "--version"], text=True).lower()
    for lto in (False, True):
        options = ["-flto"] if lto else []
        if lto and clang:
            options += ["-fuse-ld=lld"]
        # An ordinary external function can itself be removed by LTO. The
        # namespace marker and used emitter must survive that compiler stage.
        for form in ((1, 4) if lto else (1, 3, 4)):
            for current in (alignment, 128):
                name = f"{label}-{int(lto)}-{form}-{current}"
                exe = output / (name + ".exe")
                run(base + options + [f"-DABI_RETENTION_FORM={form}",
                    f"-DTELEMETRY_FORCE_CACHELINE={current}", source, str(abi_object),
                    *link_flags, "-o", str(exe)], name + "-link",
                    r"undefined (?:reference|symbol).*requireTelemetryAbi|undefined reference|AbiTag"
                    if current != alignment else None)
                if execute and current == alignment:
                    run([str(exe)], name + "-run")
                checked += 1
    print(f"{checked} ABI retention/compiler-omission link controls passed ({label})", flush=True)


def check_contracts(flags, run):
    cases = {**{i: r"actual object|direct owner" for i in (1, 2, 3, 23)},
             **{i: r"deleted" for i in (4, 5, 6, 7, 16, 17, 21, 22, 24)},
             **{i: r"private" for i in (8, 9)},
             **{i: r"invalidFieldFlags" for i in (10, 11)},
             **{i: r"invalidIdComponent" for i in (12, 13, 14)},
             15: r"deleted", 18: r"invalidFieldLimits",
             **{i: r"signature must match exactly" for i in (19, 20, 25)}}
    for case in (0, *cases):
        run(flags + [f"-DCASE={case}", "-fsyntax-only", "tests/regression/ReviewCompileFail.cpp"],
            f"review-contract-{case}", cases.get(case))
    rejection_count = len(cases)
    for name, count, diagnostic in (("OwnerLifetimeCompileFail", 18, "deleted"),
                                   ("PointerOwnerCompileFail", 9, "actual object|direct owner")):
        for case in range(count + 1):
            run(flags + [f"-DCASE={case}", "-fsyntax-only", f"tests/regression/{name}.cpp"],
                f"review-{name}-{case}", diagnostic if case else None)
        rejection_count += count
    run(flags + ["-fno-delete-null-pointer-checks", "-DTELEMETRY_NULL_CHECKS_SLOT_ONLY",
                 "-fsyntax-only",
                 "tests/regression/NullChecksFlag.cpp"], "review-null-pointer-control")
    # Require an error, not a word in an overload-candidate note. Some explicit
    # braced calls reject during overload resolution; they must not pass merely
    # because the compiler listed an unrelated deleted overload afterwards.
    brace_cases = {case: r"error:[^\n]*deleted" for case in range(1, 73)}
    brace_cases.update({case: r"error:[^\n]*ambiguous" for case in (22, 23, 24, 57)})
    brace_cases.update({case: r"error:[^\n]*no matching" for case in range(58, 65)})
    for case in (0, *brace_cases):
        run(flags + [f"-DTELEMETRY_BORROWED_BRACE_FAIL_CASE={case}", "-fsyntax-only",
                     "tests/regression/BorrowedBraceCompileFail.cpp"],
            f"review-borrowed-brace-{case}", brace_cases.get(case))
    name_cases = {case: "invalidFieldLimits" for case in range(1, 7)}
    name_cases.update({case: "invalidGroupName" for case in (7, 8)})
    for case in (0, *name_cases):
        run(flags + [f"-DCASE={case}", "-fsyntax-only",
                     "tests/regression/DefinitionNamesCompileFail.cpp"],
            f"review-definition-name-{case}", name_cases.get(case))
    slot_cases = range(1, 16)
    for case in (0, *slot_cases):
        run(flags + [f"-DTELEMETRY_SLOT_CALLABLE_FAIL_CASE={case}", "-fsyntax-only",
                     "tests/regression/SlotCallableCompileFail.cpp"],
            f"review-slot-callable-{case}", "signature must match exactly" if case else None)
    weak_cases = range(1, 12)
    for case in (0, *weak_cases):
        run(flags + [f"-DTELEMETRY_WEAK_TARGET_FAIL_CASE={case}", "-fsyntax-only",
                     "tests/regression/WeakTargetCompileFail.cpp"],
            f"review-weak-target-{case}", r"constant" if case else None)
    id_cases = {case: r"error:[^\n]*deleted" for case in range(1, 105)}
    id_cases.update({case: r"error:[^\n]*no matching" for case in
                     (3, 4, 7, 8, 11, 12, 25, 26, 30, 31, 34, 35, 53, 54, 76, 78, 79, 82, 83)})
    id_cases.update({case: "invalidIdComponent" for case in (65, 66, 67, 68, 73, 74)})
    id_cases.update({case: "16-bit ID range" for case in (69, 70, 71, 72)})
    id_cases.update({case: "Packed ID must be an integer" for case in range(85, 99)})
    id_cases.update({case: "Packed ID is outside the 32-bit range" for case in range(99, 105)})
    for case in (0, *id_cases):
        run(flags + [f"-DTELEMETRY_ID_BOUNDARY_FAIL_CASE={case}", "-fsyntax-only",
                     "tests/regression/IdBoundaryCompileFail.cpp"],
            f"review-id-boundary-{case}", id_cases.get(case))
    return (rejection_count + len(brace_cases) + len(name_cases) + len(slot_cases)
            + len(weak_cases) + len(id_cases))


def check(args, flags, build_flags, run, output, library_sources, environment):
    # Address equality across separately compiled modules detects a regression
    # to TU-local JSON helpers even when the serialized bytes still agree.
    linkage_objects = []
    for part in (1, 2):
        obj = output / f"JsonLinkage-{part}.o"
        run(flags + build_flags + [f"-DTELEMETRY_JSON_LINKAGE_PART={part}", "-c",
            "tests/regression/JsonLinkageCheck.cpp", "-o", str(obj)],
            f"review-json-linkage-{part}-build")
        linkage_objects.append(str(obj))
    executable = output / ("JsonLinkageCheck.exe" if os.name == "nt" else "JsonLinkageCheck")
    run(flags + build_flags + ["tests/regression/JsonLinkageCheck.cpp", *linkage_objects,
                              "-o", str(executable)], "review-json-linkage-build")
    run([str(executable)], "review-json-linkage-run")

    suites = ("ReviewCheck", "NumericEdges", "CommandArityCheck",
              "JsonBoundaryCheck", "SlotEdgesCheck", "HonorFlagsCheck",
              "BorrowedBraceCompileFail", "SetterConversionCheck", "SlotCallableCheck", "NullChecksFlag",
              "IdBoundaryCheck")
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

    rejection_count = check_contracts(flags, run)

    # Bad runtime definitions must terminate intentionally. A normal return or
    # an unrelated memory access failure is not an acceptable negative control.
    for name, modes in (("RuntimeLimitsAbort", [None]), ("RuntimeMetadataAbort", [str(i) for i in range(1, 13)])):
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

    # Compile each violation independently, including initialization before
    # main. Ordinary literal calls have the same runtime contract as variables.
    for case in range(1, 7):
        name = f"IdBoundaryAbort-{case}"
        executable = output / (name + (".exe" if os.name == "nt" else ""))
        run(flags + build_flags + [f"-DTELEMETRY_ID_BOUNDARY_ABORT_CASE={case}",
            "tests/regression/IdBoundaryCheck.cpp", *library_sources, "-o", str(executable)],
            f"review-{name}-build")
        result = subprocess.run([str(executable)], cwd=output, env=environment,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
        (output / f"review-{name}-run.log").write_bytes(result.stdout)
        if result.returncode not in ((3, -1073740791) if os.name == "nt" else (-6,)):
            raise RuntimeError(f"{name} did not abort intentionally: {result.returncode}")

    # Clang has no option macros for individual no-honor flags. Check the
    # header's diagnostic despite a per-warning override. Global -w and system
    # header suppression remain documented compiler-mode boundaries.
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
    print(f"Review regression suites, {rejection_count} diagnostic rejections, "
          "null-pointer and runtime-abort controls passed", flush=True)
