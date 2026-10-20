#!/usr/bin/env python3
"""Qualify test-owned TX lifetime without opening a device. MIT."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time


REPO = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
SOURCES = [
    "lib/client/rpc/RpcClient.cpp",
    "lib/telemetry/model/Adapter.cpp",
    "lib/telemetry/abi/StructuredAbi.cpp",
]


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inputs() -> dict[str, str]:
    paths = [p for p in (REPO / "lib").rglob("*")
             if p.is_file() and p.suffix in {".hpp", ".h", ".cpp", ".inc", ".inl"}]
    paths += [p for p in HERE.rglob("*") if p.is_file() and "__pycache__" not in p.parts]
    paths.append(REPO / "tests/structured/rpc/Fixture.hpp")
    return {p.relative_to(REPO).as_posix(): sha(p) for p in sorted(set(paths))}


def sdk_inputs(root: Path) -> dict[str, str]:
    directories = ["Core/Inc", "Drivers/STM32H7xx_HAL_Driver/Inc", "Drivers/CMSIS/Include",
                   "Drivers/CMSIS/Device/ST/STM32H7xx/Include"]
    required = ["Core/Inc/stm32h7xx_hal_conf.h",
                "Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal.h",
                "Drivers/CMSIS/Include/core_cm7.h",
                "Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h753xx.h"]
    for path in required:
        if not (root / path).is_file():
            raise RuntimeError(f"actual SDK input absent: {path}")
    return {p.relative_to(root).as_posix(): sha(p) for directory in directories
            for p in sorted((root / directory).rglob("*")) if p.is_file()}


def tool(name: str) -> str:
    resolved = shutil.which(name)
    if not resolved:
        raise RuntimeError(f"tool not found: {name}")
    # The clang++ symlink spelling selects the C++ link driver.
    return str(Path(resolved).absolute())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--cxx", default="g++")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--arm-cxx")
    parser.add_argument("--hal-cxx")
    parser.add_argument("--hal-root", type=Path)
    args = parser.parse_args()
    out = args.build_dir.resolve()
    if out == REPO or REPO in out.parents or out in REPO.parents:
        parser.error("--build-dir must be external to the repository and its ancestors")
    if bool(args.hal_cxx) != bool(args.hal_root):
        parser.error("--hal-cxx and --hal-root are required together")
    out.mkdir(parents=True, exist_ok=True)
    before = inputs()
    report = {"passed": False, "physical_device_execution": False,
              "source_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=REPO, text=True).strip(),
              "source_dirty": subprocess.check_output(["git", "status", "--porcelain"], cwd=REPO, text=True).splitlines(),
              "inputs_before": before, "commands": [], "tools": {}, "executions": [],
              "portable_arm": {"requested": bool(args.arm_cxx), "actual_hal": False},
              "actual_hal": {"requested": bool(args.hal_cxx), "execution": False}}
    environment = os.environ.copy()
    environment["PYTHONDONTWRITEBYTECODE"] = "1"
    if args.sanitize:
        environment["ASAN_OPTIONS"] = "detect_leaks=1:halt_on_error=1"
        environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"

    def run(name: str, command: list[str]) -> str:
        started = time.monotonic()
        result = subprocess.run([str(x) for x in command], cwd=REPO, env=environment,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace", timeout=600)
        log = out / f"{name}.log"
        log.write_text(result.stdout, encoding="utf-8")
        report["commands"].append({"name": name, "command": [str(x) for x in command],
                                   "returncode": result.returncode, "seconds": time.monotonic() - started,
                                   "log": log.name, "sha256": sha(log)})
        if result.returncode:
            raise RuntimeError(f"{name} failed ({result.returncode}); see {log}")
        return result.stdout

    def identify(name: str) -> str:
        executable = tool(name)
        if executable not in report["tools"]:
            version = run(f"tool-{len(report['tools'])}", [executable, "--version"])
            report["tools"][executable] = {"sha256": sha(Path(executable)), "version": version.splitlines()[0]}
        return executable

    common = ["-std=c++20", "-Wall", "-Wextra", "-Werror",
              "-DTINY_DELEGATE_ENABLE_HEAP_FALLBACK=0", "-DTELEMETRY_STRUCTURED_LOCAL_BYTES=0",
              "-Ilib", "-Ilib/boost_pfr/include", "-Ilib/magic_enum"]
    arm_flags = ["-Os", "-fno-exceptions", "-fno-rtti", "-mcpu=cortex-m7", "-mthumb",
                 "-mfpu=fpv5-d16", "-mfloat-abi=hard", "-fstack-usage", "-ffunction-sections", "-fdata-sections"]

    def inspect_object(name: str, compiler: str, obj: Path) -> dict:
        compiler_path = Path(compiler)
        nm_name = compiler_path.name.replace("g++", "nm").replace("clang++", "nm")
        sibling = compiler_path.with_name(nm_name)
        nm = identify(str(sibling) if sibling.is_file() else "nm")
        undefined = run(name + "-undefined", [nm, "-u", str(obj)])
        if re.search(r"\b(?:_Zn[aw]\w*|_Zd[la]\w*|malloc|calloc|realloc|free|__atomic_\w+)\b", undefined):
            raise RuntimeError(f"allocation or out-of-line atomic helper in {name}")
        stack = obj.with_suffix(".su")
        if not stack.is_file():
            raise RuntimeError(f"stack usage missing: {stack}")
        return {"object": obj.name, "sha256": sha(obj), "undefined_log": name + "-undefined.log",
                "allocation_references": 0, "stack_usage": stack.name, "stack_sha256": sha(stack)}

    try:
        cxx = identify(args.cxx)
        host_flags = ["-O1", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                      "-fno-omit-frame-pointer"] if args.sanitize else ["-O2"]
        suffix = ".exe" if os.name == "nt" else ""
        for stem, extra, source_list, minimum in [
            ("host", [], ["tests/transport_dma/Host.cpp", *SOURCES,
                          "lib/integration/telemetry/v3/RpcMailbox.cpp", "lib/resource/protocol/Protocol.cpp"], 397),
            ("hal-double", ["-Itests/transport_dma/hal_double"],
             ["tests/transport_dma/Hal.cpp", "tests/transport_dma/H753Port.cpp", *SOURCES], 122),
            ("retrying", [], ["tests/transport_dma/Retrying.cpp", *SOURCES,
                              "lib/integration/encoded/Dispatcher.cpp"], 112),
        ]:
            binary = out / (stem + suffix)
            run(stem + "-compile", [cxx, *common, *host_flags, *extra, *source_list, "-o", str(binary)])
            output = run(stem + "-run", [str(binary)])
            match = re.search(r"transport_dma(?:_hal|_retry)? checks=(\d+) failures=(\d+)", output)
            if not match or int(match[1]) < minimum or int(match[2]):
                raise RuntimeError(f"missing/insufficient counted result: {stem}")
            report["executions"].append({"name": stem, "checks": int(match[1]), "failures": int(match[2]),
                                          "binary_sha256": sha(binary), "hal_test_double": stem == "hal-double"})
        if args.arm_cxx:
            arm = identify(args.arm_cxx)
            obj = out / "portable-arm.o"
            run("portable-arm-compile", [arm, *common, *arm_flags, "-c", "tests/transport_dma/Arm.cpp", "-o", str(obj)])
            report["portable_arm"].update(inspect_object("portable-arm", arm, obj))
        if args.hal_cxx:
            hal = identify(args.hal_cxx)
            sdk = args.hal_root.resolve()
            sdk_before = sdk_inputs(sdk)
            report["actual_hal"].update({"sdk_root": str(sdk), "inputs_before": sdk_before})
            sdk_flags = []
            for directory in ["Core/Inc", "Drivers/STM32H7xx_HAL_Driver/Inc", "Drivers/CMSIS/Include",
                              "Drivers/CMSIS/Device/ST/STM32H7xx/Include"]:
                sdk_flags += ["-isystem", str(sdk / directory)]
            obj = out / "actual-hal.o"
            run("actual-hal-compile", [hal, *common, *arm_flags, "-DSTM32H753xx", "-DUSE_HAL_DRIVER",
                                       *sdk_flags, "-c", "tests/transport_dma/H753Port.cpp", "-o", str(obj)])
            report["actual_hal"].update(inspect_object("actual-hal", hal, obj))
            report["actual_hal"]["inputs_after"] = sdk_inputs(sdk)
            if report["actual_hal"]["inputs_after"] != sdk_before:
                raise RuntimeError("actual SDK/config changed during qualification")
        report["passed"] = True
    except Exception as error:
        report["error"] = str(error)
    finally:
        report["inputs_after"] = inputs()
        report["inputs_unchanged"] = report["inputs_after"] == before
        if not report["inputs_unchanged"]:
            report["passed"] = False
            report["error"] = "source inputs changed during qualification"
        report["counts"] = {"commands": len(report["commands"]),
                            "executions": len(report["executions"]),
                            "checks": sum(row["checks"] for row in report["executions"]),
                            "source_inputs": len(before)}
        (out / "summary.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"passed": report["passed"], **report["counts"], "summary": str(out / "summary.json")}))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
