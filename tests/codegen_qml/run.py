"""Build and execute real Qt/QML + native Encoded/Resource qualification.

All generated files, binaries and command receipts stay in --build-dir.
Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""
from __future__ import annotations

import argparse
import ctypes
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def executable_image(path: str) -> Path:
    if os.name == "nt" and path == sys.executable:
        buffer = ctypes.create_unicode_buffer(32768)
        if not ctypes.windll.kernel32.GetModuleFileNameW(None, buffer, len(buffer)):
            raise OSError("GetModuleFileNameW failed")
        return Path(buffer.value)
    return Path(path).resolve()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--qt-prefix", type=Path, required=True)
    parser.add_argument("--cxx", default="g++")
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--ninja", default="ninja")
    parser.add_argument("--jobs", default="2")
    args = parser.parse_args()
    for key in ("cxx", "cmake", "ninja"):
        value = getattr(args, key)
        setattr(args, key, shutil.which(value) or str(Path(value).resolve()))
    output = args.build_dir.resolve()
    if output == ROOT or ROOT in output.parents:
        parser.error("--build-dir must be outside the source repository")
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    env["QT_QPA_PLATFORM"] = "offscreen"
    env["QT_QUICK_BACKEND"] = "software"
    env["PATH"] = os.pathsep.join((str(Path(args.cxx).resolve().parent),
                                 str(args.qt_prefix.resolve() / "bin"), env.get("PATH", "")))
    commands = []
    summary = {"status": "running", "started_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
               "commands": commands, "source_sha256": {}}
    source_paths = list((ROOT / "tools/telemetry_codegen").rglob("*.py"))
    source_paths += list((ROOT / "tests/codegen_qml").glob("*"))
    source_paths += list((ROOT / "examples/codegen_qml").glob("*"))
    source_paths += list((ROOT / "tests/codegen_qml/probes").glob("*"))
    source_paths += [ROOT / name for name in ("tests/codegen/Device.hpp", "tests/codegen/Producer.cpp",
                                             "examples/structured_client/Device.hpp")]
    for directory in ("lib/telemetry", "lib/resource"):
        source_paths += [path for path in (ROOT / directory).rglob("*")
                         if path.suffix in (".hpp", ".h", ".cpp")]
    for path in sorted(source_paths):
        if path.is_file():
            summary["source_sha256"][path.relative_to(ROOT).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()

    def run(command, name):
        command = list(map(str, command))
        started = dt.datetime.now(dt.timezone.utc).isoformat()
        result = subprocess.run(command, cwd=ROOT, env=env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=240)
        (output / (name + ".log")).write_text(result.stdout, encoding="utf-8")
        commands.append({"command": command, "returncode": result.returncode, "log": name + ".log",
                         "started_utc": started, "finished_utc": dt.datetime.now(dt.timezone.utc).isoformat()})
        print(name, "exit", result.returncode, flush=True)
        if result.returncode:
            print(result.stdout[-10000:])
            raise RuntimeError(name + " failed")
        return result.stdout

    suffix = ".exe" if os.name == "nt" else ""
    producer = output / ("Producer" + suffix)
    native = output / "native"
    sdk = output / "sdk"
    build = output / "build"
    try:
        run([args.cxx, "--version"], "compiler-version")
        run([args.cmake, "--version"], "cmake-version")
        run([args.ninja, "--version"], "ninja-version")
        run([sys.executable, "--version"], "python-version")
        summary["tool_sha256"] = {str(executable_image(path)): hashlib.sha256(executable_image(path).read_bytes()).hexdigest()
                                  for path in (args.cxx, args.cmake, args.ninja, sys.executable)}
        run([args.cxx, "-std=c++20", "-O2", "-Ilib", "-Ilib/boost_pfr/include",
             "-Ilib/magic_enum", "tests/codegen/Producer.cpp", "-o", producer], "native-producer-build")
        run([producer, native], "native-producer-run")
        run([sys.executable, "-m", "unittest", "discover", "-s", "tests/codegen_qml", "-p", "test_*.py"],
            "generator-unit")
        run([sys.executable, "-m", "tools.telemetry_codegen", "generate", native / "descriptor.bin",
             "--targets", "cpp,qml", "--output", sdk], "generate")
        summary["input_sha256"] = {str(path.relative_to(output)): hashlib.sha256(path.read_bytes()).hexdigest()
                                   for path in sorted(native.glob("*.bin"))}
        summary["generated_sha256"] = {str(path.relative_to(sdk)): hashlib.sha256(path.read_bytes()).hexdigest()
                                       for path in sorted(sdk.rglob("*")) if path.is_file()}
        run([args.cmake, "-S", ROOT / "tests/codegen_qml", "-B", build, "-G", "Ninja",
             "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_CXX_COMPILER=" + str(Path(args.cxx).resolve()),
             "-DCMAKE_CXX_FLAGS=-Wall -Wextra -Werror",
             "-DCMAKE_MAKE_PROGRAM=" + str(Path(args.ninja).resolve()),
             "-DCMAKE_PREFIX_PATH=" + str(args.qt_prefix.resolve()), "-DGENERATED_SDK=" + str(sdk)], "configure")
        run([args.cmake, "--build", build, "-j", args.jobs], "build")
        runtime = run([build / ("telemetry_qml_check" + suffix), native], "runtime")
        counts = re.search(r"QML checks=(\d+) RPC packets=(\d+)", runtime)
        if not counts:
            raise RuntimeError("runtime produced no qualification counts")
        summary["qml_checks"], summary["rpc_packets"] = map(int, counts.groups())
        if summary["qml_checks"] < 1234 or summary["rpc_packets"] < 40:
            raise RuntimeError("runtime did not execute the current QML/RPC qualification floor")
        run([sys.executable, ROOT / "tests/codegen_qml/probes/run.py", "--build-dir", output / "probes",
             "--qt-prefix", args.qt_prefix, "--cxx", args.cxx, "--cmake", args.cmake,
             "--ninja", args.ninja, "--jobs", args.jobs], "conversion-probes")
        probe_summary = json.loads((output / "probes/summary.json").read_text(encoding="utf-8"))
        if not probe_summary.get("passed"):
            raise RuntimeError("conversion probes did not complete")
        summary["conversion_probes"] = probe_summary
        example = output / "example"
        run([args.cmake, "-S", ROOT / "examples/codegen_qml", "-B", example, "-G", "Ninja",
             "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_CXX_COMPILER=" + args.cxx,
             "-DCMAKE_CXX_FLAGS=-Wall -Wextra -Werror",
             "-DCMAKE_MAKE_PROGRAM=" + args.ninja,
             "-DCMAKE_PREFIX_PATH=" + str(args.qt_prefix.resolve()), "-DGENERATED_SDK=" + str(sdk)],
            "example-configure")
        run([args.cmake, "--build", example, "-j", args.jobs], "example-build")
        run([example / ("telemetry_qml_example" + suffix), native / "descriptor.bin",
             native / "config.bin", "--smoke"], "example-runtime")
        run([example / ("telemetry_qml_example" + suffix), native / "descriptor.bin",
             native / "config.bin", "--rpc", "--smoke"], "example-rpc-runtime")
        summary["source_sha256_after"] = {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest()
                                          for path in summary["source_sha256"]}
        if summary["source_sha256_after"] != summary["source_sha256"]:
            raise RuntimeError("source inputs changed during qualification; rerun from stable sources")
        summary["status"] = "passed"
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        summary["status"] = "failed"
        summary["error"] = str(error)
    summary["finished_utc"] = dt.datetime.now(dt.timezone.utc).isoformat()
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    return 0 if summary["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
