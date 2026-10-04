#!/usr/bin/env python3
"""Execute the same canonical byte contracts on little- and big-endian targets.

Linux s390x runs through QEMU user-mode with its own C++ standard library. This
exercises the real big-endian branch, rather than changing a preprocessor endian
macro on a little-endian machine. The test compares complete Descriptor/Values
files too; it does not measure MCU cycles or touch a physical board.
Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[3]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--cxx-native", default="g++")
    parser.add_argument("--cxx-big", default="s390x-linux-gnu-g++")
    parser.add_argument("--emulator", default="qemu-s390x")
    parser.add_argument("--sysroot", default="/usr/s390x-linux-gnu")
    args = parser.parse_args()
    out = args.build_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    report = {"completed": False, "targets": {}, "wire_files": {}}
    inputs = [path for path in (ROOT / "lib").rglob("*")
              if path.is_file() and path.suffix in {".cpp", ".h", ".hpp"}]
    inputs += [path for directory in (ROOT / "tests/structured/codec",
               ROOT / "tests/structured/descriptor", ROOT / "tests/structured/resources",
               ROOT / "tests/resources") for path in directory.glob("*")
               if path.suffix in {".cpp", ".h", ".hpp"}]
    before = {path.relative_to(ROOT).as_posix(): digest(path) for path in sorted(set(inputs))}

    def run(command, label):
        started = time.monotonic()
        command = list(map(str, command))
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                                encoding="utf-8", errors="replace", timeout=180)
        log = (f"COMMAND {command!r}\nEXIT {result.returncode}\n"
               f"SECONDS {time.monotonic() - started:.3f}\n{result.stdout}{result.stderr}")
        (out / (label + ".log")).write_text(log, encoding="utf-8")
        if result.returncode:
            raise RuntimeError(label + " failed\n" + log)
        print(label + ": pass", flush=True)
        return result.stdout

    programs = {
        "codec": (["tests/structured/codec/CodecProbe.cpp"], []),
        "resource": (["tests/resources/CoreCheck.cpp", "lib/resource/protocol/Protocol.cpp"], []),
        "descriptor": (["tests/structured/descriptor/DescriptorCheck.cpp",
                        "tests/structured/descriptor/Other.cpp"],
                       ["mixed.bin", "edge.bin", "empty.bin"]),
        "values": (["tests/structured/resources/Check.cpp", "lib/resource/protocol/Protocol.cpp",
                    "lib/resource/telemetry/v3/detail/Values.cpp"],
                   ["values-descriptor.bin", "values.bin"]),
    }
    for target, cxx, endian in (("little", args.cxx_native, "little"),
                                ("big", args.cxx_big, "big")):
        compiler = Path(shutil.which(cxx) or cxx).absolute()
        folder = out / target
        folder.mkdir(exist_ok=True)
        asserted = folder / "Target.hpp"
        asserted.write_text("#include <bit>\nstatic_assert(std::endian::native == "
                            f"std::endian::{endian}, \"Unexpected native endian\");\n",
                            encoding="utf-8")
        version = run([compiler, "--version"], target + "-compiler").splitlines()[0]
        report["targets"][target] = {"compiler": version, "compiler_sha256": digest(compiler),
                                     "endian": endian, "programs": {}}
        flags = [compiler, "-std=c++20", "-O2", "-UNDEBUG", "-Wall", "-Wextra", "-Werror",
                 "-pedantic-errors", "-Ilib", "-Ilib/boost_pfr/include", "-Ilib/magic_enum",
                 "-include", asserted]
        prefix = [args.emulator, "-L", args.sysroot] if target == "big" else []
        for name, (sources, files) in programs.items():
            image = folder / name
            run([*flags, *sources, "-o", image], target + "-build-" + name)
            output = run([*prefix, image, *(folder / file for file in files)],
                         target + "-execute-" + name)
            report["targets"][target]["programs"][name] = {
                "image_sha256": digest(image), "output": output}
    for name in ("mixed.bin", "edge.bin", "empty.bin", "values-descriptor.bin", "values.bin"):
        little, big = out / "little" / name, out / "big" / name
        if little.read_bytes() != big.read_bytes():
            raise RuntimeError("Wire bytes depend on native endian: " + name)
        report["wire_files"][name] = {"bytes": little.stat().st_size,
                                       "little_sha256": digest(little), "big_sha256": digest(big)}
    if {path.relative_to(ROOT).as_posix(): digest(path) for path in sorted(set(inputs))} != before:
        raise RuntimeError("Source inputs changed during the cross-endian check")
    report["input_sha256"] = before
    report["completed"] = True
    (out / "summary.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("Little/big endian: 8 executions and 5 byte-identical wire files passed", flush=True)


if __name__ == "__main__":
    main()
