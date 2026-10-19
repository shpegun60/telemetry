#!/usr/bin/env python3
"""Compile strict QVariant and real-JavaScript converter probes with receipts.

All generated files, binaries and logs stay in an external --build-dir. Selected
tool/runtime hashes identify this run; they are not a hermetic Qt/toolchain lock.
Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE))
import fixture


def digest(path):
    state = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            state.update(chunk)
    return state.hexdigest()


def python_image():
    # A Microsoft Store app-execution alias can launch Python but cannot be
    # opened as a binary file. Hash the actual running image in that case.
    if os.name == "nt":
        import ctypes
        buffer = ctypes.create_unicode_buffer(32768)
        length = ctypes.windll.kernel32.GetModuleFileNameW(None, buffer, len(buffer))
        if not length or length >= len(buffer):
            raise OSError("cannot identify the running Python image")
        return Path(buffer.value).resolve()
    return Path(sys.executable).resolve()


def source_inputs():
    names = ("tools/telemetry_codegen/__init__.py", "tools/telemetry_codegen/cli.py",
             "tools/telemetry_codegen/parser.py", "tools/telemetry_codegen/schema.py",
             "tools/telemetry_codegen/naming.py", "tools/telemetry_codegen/generators/cpp.py",
             "tools/telemetry_codegen/generators/qml.py",
             "tools/telemetry_codegen/generators/qml_rpc.py",
             "tools/telemetry_codegen/generators/qml_client.py",
             "tools/telemetry_codegen/generators/qml_values.py",
             "tools/telemetry_codegen/generators/qml_js.py",
             "tools/telemetry_codegen/generators/qml_macros.py", "tests/codegen/test_core.py",
             "tests/structured/descriptor/golden.py", "tests/structured/descriptor/parser.py",
             "tests/structured/descriptor/empty.hex")
    paths = [ROOT / name for name in names]
    paths += sorted(path for path in HERE.iterdir() if path.is_file() and
                    path.suffix in (".py", ".cpp", ".txt"))
    return {path.relative_to(ROOT).as_posix(): digest(path) for path in paths}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--qt-prefix", type=Path, required=True)
    parser.add_argument("--cxx", default="g++")
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--ninja", default="ninja")
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args(argv)
    output, prefix = args.build_dir.resolve(), args.qt_prefix.resolve()
    if output == ROOT or ROOT in output.parents:
        parser.error("--build-dir must be outside the source repository")
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
    tools = {}
    for name in ("cxx", "cmake", "ninja"):
        selected = getattr(args, name)
        tools[name] = Path(shutil.which(selected) or selected).resolve()
    env["PATH"] = os.pathsep.join((str(tools["cxx"].parent), str(prefix / "bin"), env.get("PATH", "")))
    report = dict(passed=False, started_utc=datetime.now(timezone.utc).isoformat(), commands=[],
                  executions=[], source_sha256=source_inputs(), tool_sha256={}, qt_prefix=str(prefix))

    def run(command, label):
        command = list(map(str, command))
        started = time.monotonic()
        timestamp = datetime.now(timezone.utc).isoformat()
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, timeout=240)
        (output / (label + ".log")).write_text(result.stdout, encoding="utf-8")
        report["commands"].append(dict(name=label, command=command, returncode=result.returncode,
                                       started_utc=timestamp, seconds=time.monotonic() - started,
                                       log=label + ".log"))
        if result.returncode:
            raise RuntimeError(label + " failed\n" + result.stdout[-12000:])
        print(label + ": pass", flush=True)
        return result.stdout

    python = python_image()
    selected_files = {str(python): python}
    for path in tools.values():
        selected_files[str(path)] = path
    for name in ("Qt6Core.dll", "Qt6Qml.dll", "Qt6Network.dll", "qmake.exe", "qmake", "qmake6"):
        path = prefix / "bin" / name
        if path.is_file():
            selected_files[str(path.resolve())] = path.resolve()
    for library in (prefix / "lib", prefix / "lib/x86_64-linux-gnu"):
        for stem in ("libQt6Core.so", "libQt6Qml.so", "libQt6Network.so"):
            path = library / stem
            if path.is_file():
                selected_files[str(path.resolve())] = path.resolve()
    try:
        for name, path in selected_files.items():
            if not path.is_file():
                raise ValueError("selected executable was not found: " + name)
            report["tool_sha256"][name] = digest(path)
        report["source_head"] = run(["git", "rev-parse", "HEAD"], "source-head").strip()
        report["source_dirty"] = bool(run(["git", "status", "--porcelain=v1"], "source-status").strip())
        for name, path in tools.items():
            run([path, "--version"], name + "-version")
        qmake = next((prefix / "bin" / name for name in ("qmake.exe", "qmake", "qmake6")
                      if (prefix / "bin" / name).is_file()), None)
        if qmake:
            report["qt_version"] = run([qmake, "-query", "QT_VERSION"], "qt-version").strip()
        sdk = output / "sdk"
        schema = fixture.write(sdk)
        report["compiled_modules"] = fixture.write_modules(sdk / "modules")
        report["descriptor_sha256"] = schema.descriptor_sha256
        report["descriptor_fingerprint"] = f"0x{schema.fingerprint:016x}"
        report["generated_sha256"] = {path.relative_to(sdk).as_posix(): digest(path)
                                      for path in sorted(sdk.rglob("*")) if path.is_file()}
        build = output / "build"
        run([tools["cmake"], "-S", HERE, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
             "-DCMAKE_CXX_COMPILER=" + str(tools["cxx"]), "-DCMAKE_MAKE_PROGRAM=" + str(tools["ninja"]),
             "-DCMAKE_CXX_FLAGS=-Wall -Wextra -Werror", "-DCMAKE_PREFIX_PATH=" + str(prefix),
             "-DGENERATED_SDK=" + str(sdk), "-DGENERATED_MODULES=" + str(sdk / "modules")], "configure")
        run([tools["cmake"], "--build", build, "-j", args.jobs], "build")
        suffix = ".exe" if os.name == "nt" else ""
        for name, minimum in (("values", 61), ("snapshot", 48), ("legacy", 3)):
            executable = build / ("qml_" + name + "_probe" + suffix)
            result = run([executable], "execute-" + name)
            matches = re.findall(r"(?m)^qml " + name + r" checks=(\d+) failures=0\s*$", result)
            if len(matches) != 1 or int(matches[0]) < minimum:
                raise RuntimeError(name + " did not report all required positive checks")
            report["executions"].append(dict(name=name, checks=int(matches[0]), failures=0,
                                              executable=str(executable), executable_sha256=digest(executable)))
        report["checks"] = sum(item["checks"] for item in report["executions"])
        report["passed"] = True
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        report["error"] = str(error)
        print(str(error), file=sys.stderr)
    finally:
        report["source_sha256_after"] = source_inputs()
        report["tool_sha256_after"] = {}
        for name, path in selected_files.items():
            try:
                report["tool_sha256_after"][name] = digest(path)
            except OSError as error:
                report["passed"] = False
                report["error"] = "cannot verify selected tool after execution: " + str(error)
        if (report["source_sha256_after"] != report["source_sha256"] or
                report["tool_sha256_after"] != report["tool_sha256"]):
            report["passed"] = False
            report["error"] = "qualification source or selected tool inputs changed during execution"
        report["finished_utc"] = datetime.now(timezone.utc).isoformat()
        (output / "summary.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
