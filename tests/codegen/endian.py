#!/usr/bin/env python3
"""Execute generated standalone C++ byte contracts on real little/big endian.

s390x runs with its own standard library through QEMU user mode. Both targets
compile the same independent expected-byte checks with no Telemetry includes.
All generated source, binaries, command logs and provenance stay outside source.
Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path[:0] = [str(ROOT), str(HERE)]

from test_cpp import focused_schema, write_focused


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def input_hashes():
    production = ROOT / "tools/telemetry_codegen"
    paths = [production / name for name in
             ("__init__.py", "parser.py", "schema.py", "naming.py", "generators/cpp.py")]
    paths += [HERE / name for name in ("endian.py", "test_core.py", "test_cpp.py", "Standalone.cpp")]
    paths += [ROOT / "tests/structured/descriptor" / name
              for name in ("parser.py", "golden.py", "empty.hex")]
    return {path.relative_to(ROOT).as_posix(): digest(path) for path in paths}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--cxx-native", default="g++")
    parser.add_argument("--cxx-big", default="s390x-linux-gnu-g++")
    parser.add_argument("--emulator", default="qemu-s390x")
    parser.add_argument("--sysroot", default="/usr/s390x-linux-gnu")
    args = parser.parse_args(argv)
    out = args.build_dir.resolve()
    if out == ROOT or ROOT in out.parents:
        parser.error("use an external build directory")
    out.mkdir(parents=True, exist_ok=True)
    report = dict(completed=False, targets={}, executions=0, input_sha256=input_hashes())

    def run(command, label):
        command = list(map(str, command))
        started = time.monotonic()
        result = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, timeout=180)
        log = (f"COMMAND {command!r}\nEXIT {result.returncode}\n"
               f"SECONDS {time.monotonic() - started:.3f}\n{result.stdout}")
        (out / (label + ".log")).write_text(log, encoding="utf-8")
        if result.returncode:
            raise RuntimeError(label + " failed\n" + log)
        print(label + ": pass", flush=True)
        return result.stdout

    def tool(command):
        executable = Path(shutil.which(command) or command).absolute()
        if not executable.is_file():
            raise ValueError("tool was not found: " + command)
        return executable

    try:
        report["source_head"] = run(["git", "rev-parse", "HEAD"], "source-head").strip()
        report["source_dirty"] = bool(run(["git", "status", "--porcelain=v1"], "source-state").strip())
        schema = focused_schema()
        report["descriptor_sha256"] = schema.descriptor_sha256
        report["descriptor_fingerprint"] = f"0x{schema.fingerprint:016x}"
        sdk = write_focused(out / "generated")
        report["generated_sha256"] = {
            path.relative_to(sdk).as_posix(): digest(path)
            for path in sorted(sdk.rglob("*")) if path.is_file()}
        emulator = tool(args.emulator)
        run([emulator, "--version"], "emulator-version")
        report["emulator"] = dict(path=str(emulator), sha256=digest(emulator))
        for target, command in (("little", args.cxx_native), ("big", args.cxx_big)):
            compiler = tool(command)
            run([compiler, "--version"], target + "-compiler-version")
            folder = out / target
            folder.mkdir(exist_ok=True)
            asserted = folder / "Target.hpp"
            asserted.write_text("#pragma once\n#include <bit>\nstatic_assert(std::endian::native == "
                                f"std::endian::{target}, \"Unexpected native endian\");\n")
            program = folder / "standalone"
            run([compiler, "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic-errors",
                 "-fdiagnostics-color=never", "-include", asserted, "-I", sdk / "cpp",
                 HERE / "Standalone.cpp", "-o", program], target + "-compile")
            execution = [program] if target == "little" else [emulator, "-L", args.sysroot, program]
            result = run(execution, target + "-execute")
            match = re.fullmatch(r"codegen standalone checks=([0-9]+) failures=0\s*", result)
            if not match or int(match[1]) < 83:
                raise RuntimeError(target + ": missing complete positive standalone report")
            report["targets"][target] = dict(compiler=str(compiler), compiler_sha256=digest(compiler),
                                             program_sha256=digest(program), checks=int(match[1]),
                                             execution=list(map(str, execution)), native_endian=target)
            report["executions"] += 1
        if report["targets"]["little"]["checks"] != report["targets"]["big"]["checks"]:
            raise RuntimeError("endian targets did not execute the same checks")
        report["input_sha256_after"] = input_hashes()
        if report["input_sha256_after"] != report["input_sha256"]:
            raise RuntimeError("qualification source inputs changed during execution")
        report["completed"] = True
        print("generated endian checks=" + str(sum(target["checks"] for target in report["targets"].values()))
              + " failures=0")
    finally:
        (out / "summary.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")


if __name__ == "__main__":
    main()
