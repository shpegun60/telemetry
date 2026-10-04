#!/usr/bin/env python3
"""Build the real COBS + telemetry example and retain reproducible evidence.

The COBS checkout is explicit and read-only. Host modes execute the application;
ARM mode only compiles/links at O2/Os/Og and never accesses a device. Source and
image hashes, command logs and the executed condition count remain in --build-dir.
Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
COBS_REVISION = "2e0abf260848fcb74e4a57b37532188047759643"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cobs-root", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--arm", action="store_true")
    parser.add_argument("--emulator")
    parser.add_argument("--sysroot", type=Path)
    parser.add_argument("--expect-big-endian", action="store_true")
    args = parser.parse_args()
    if args.arm and (args.sanitize or args.emulator):
        parser.error("ARM is compile/link only; use a host execution mode for these options")
    if args.expect_big_endian and not args.emulator:
        parser.error("The big-endian mode needs an explicit user-mode emulator")
    cobs = args.cobs_root.resolve()
    if not (cobs / "src/cobs/Cobs.h").is_file():
        parser.error("--cobs-root must contain src/cobs/Cobs.h")
    out = args.build_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    compiler = Path(shutil.which(args.cxx) or args.cxx).absolute()
    env = dict(os.environ)
    env["PATH"] = str(compiler.parent) + os.pathsep + env.get("PATH", "")
    env.setdefault("ASAN_OPTIONS", "detect_leaks=1:detect_stack_use_after_return=1")
    env.setdefault("UBSAN_OPTIONS", "halt_on_error=1")
    actual_revision = subprocess.check_output(
        ["git", "-C", str(cobs), "rev-parse", "HEAD"], text=True).strip()
    if actual_revision != COBS_REVISION:
        parser.error("COBS checkout must be at the documented pinned revision " + COBS_REVISION)
    report = {"completed": False, "compiler_sha256": digest(compiler),
              "cobs_tested_revision": actual_revision, "images": {}, "checks": 0,
              "mode": "arm-compile-link" if args.arm else "host-execution"}

    def run(command, label):
        started = time.monotonic()
        command = list(map(str, command))
        result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True,
                                encoding="utf-8", errors="replace", timeout=180)
        log = (f"COMMAND {command!r}\nEXIT {result.returncode}\n"
               f"SECONDS {time.monotonic() - started:.3f}\n{result.stdout}{result.stderr}")
        (out / (label + ".log")).write_text(log, encoding="utf-8")
        if result.returncode:
            raise RuntimeError(label + " failed\n" + log)
        print(label + ": pass", flush=True)
        return result.stdout

    report["compiler"] = run([compiler, "--version"], "compiler").splitlines()[0]
    sources = [ROOT / path for path in (
        "examples/cobs_integration/main.cpp", "examples/device_integration/Device.cpp",
        "examples/device_integration/Api.cpp", "lib/telemetry/abi/StructuredAbi.cpp",
        "lib/telemetry/model/Adapter.cpp", "lib/resource/protocol/Protocol.cpp")]
    sources += [cobs / "src/cobs/Encoder.cpp", cobs / "src/cobs/Decoder.cpp"]
    inputs = sources + [path for path in (cobs / "src").rglob("*.h")
                       if "tests" not in path.parts and
                       path.relative_to(cobs / "src").parts[0] in {"cobs", "wire", "crc"}]
    inputs += [path for folder in (ROOT / "lib/telemetry", ROOT / "lib/resource",
                                  ROOT / "lib/delegate", ROOT / "lib/boost_pfr",
                                  ROOT / "lib/magic_enum")
               for path in folder.rglob("*") if path.suffix in {".h", ".hpp"}]
    inputs += [ROOT / "examples/device_integration/Api.hpp",
               ROOT / "examples/device_integration/Device.hpp"]
    before = {str(path): digest(path) for path in sorted(set(inputs))}
    flags = [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic-errors",
             "-fdiagnostics-color=never", "-Ilib", "-Ilib/boost_pfr/include",
             "-Ilib/magic_enum", "-Ilib/delegate", "-I" + str(cobs / "src")]
    if args.sanitize:
        flags += ["-g", "-fno-omit-frame-pointer", "-fsanitize=address,undefined",
                  "-fno-sanitize-recover=all"]
    if args.expect_big_endian:
        target = out / "ExpectedEndian.hpp"
        target.write_text("#include <bit>\nstatic_assert(std::endian::native == "
                          "std::endian::big, \"This run requires a big-endian target\");\n",
                          encoding="utf-8")
        flags += ["-include", target]
        report["target_endian"] = "big (compile-time asserted)"
    if args.arm:
        flags += ["-mcpu=cortex-m7", "-mthumb", "-mfpu=fpv5-d16", "-mfloat-abi=hard",
                  "-fno-exceptions", "-fno-rtti", "-ffunction-sections", "-fdata-sections",
                  "-fstack-usage", "--specs=nano.specs", "--specs=nosys.specs",
                  "-Wl,--gc-sections"]
    prefix = []
    if args.emulator:
        prefix = [args.emulator]
        if args.sysroot:
            prefix += ["-L", str(args.sysroot)]
    for opt in ("O2", "Os", "Og") if args.arm else ("O2",):
        image = out / (opt + (".elf" if args.arm else ".exe" if os.name == "nt" else ""))
        run([*flags, "-" + opt, *sources, "-o", image], "build-" + opt)
        report["images"][opt] = {"bytes": image.stat().st_size, "sha256": digest(image)}
        if not args.arm:
            output = run([*prefix, image], "execute-" + opt)
            match = re.fullmatch(r"COBS telemetry/resource integration: (\d+) checks passed\s*", output)
            if not match or int(match[1]) != 295:
                raise RuntimeError("The example did not execute its 295 expected checks")
            report["checks"] += int(match[1])
    if {str(path): digest(path) for path in sorted(set(inputs))} != before:
        raise RuntimeError("Build inputs changed during validation")
    report["input_sha256"] = before
    report["completed"] = True
    (out / "summary.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("COBS integration completed; executed checks:", report["checks"], flush=True)


if __name__ == "__main__":
    main()
