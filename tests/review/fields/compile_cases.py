#!/usr/bin/env python3
"""Review helper (fields slice): compile one probe for each CASE with several
compilers and report which cases the library accepts. Logs go to
build/review-fields/<probe>/. Usage: python compile_cases.py Probe.cpp 1 18 [extra flags]"""
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
QT_BIN = r"C:\Qt\Tools\mingw1310_64\bin"
MSYS_BIN = r"C:\msys64\ucrt64\bin"
WSL_ROOT = "/mnt/c/Users/admin/Documents/my_workspace/stm32/triol/power_analyzer/telemetry"
COMMON = ["-Wall", "-Wextra", "-Werror", "-pedantic-errors", "-fdiagnostics-color=never",
          "-Ilib/telemetry", "-Ilib/delegate", "-fsyntax-only"]


def compilers(std):
    yield f"gcc13-{std}", [QT_BIN + "\\g++.exe", "-std=" + std], QT_BIN
    yield f"gcc15-{std}", [MSYS_BIN + "\\g++.exe", "-std=" + std], MSYS_BIN
    yield f"clang18-{std}", ["wsl.exe", "-d", "Ubuntu-24.04", "--cd", WSL_ROOT, "-e",
                             "clang++-18", "-std=" + std], None


def main():
    probe = Path(sys.argv[1])
    first, last = int(sys.argv[2]), int(sys.argv[3])
    extra = sys.argv[4:]
    out = ROOT / "build" / "review-fields" / probe.stem
    out.mkdir(parents=True, exist_ok=True)
    rel = probe.resolve().relative_to(ROOT).as_posix()
    for case in range(first, last + 1):
        row = []
        for std in ("c++17", "c++20"):
            for label, command, bin_dir in compilers(std):
                env = dict(os.environ)
                if bin_dir is not None:
                    env["PATH"] = bin_dir + ";" + env["PATH"]
                cmd = command + COMMON + extra + [f"-DCASE={case}", rel]
                r = subprocess.run(cmd, cwd=ROOT, env=env, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True, errors="replace")
                (out / f"{label}-{case}.log").write_text(
                    " ".join(cmd) + "\n" + r.stdout, encoding="utf-8")
                row.append(f"{label}={'ACCEPT' if r.returncode == 0 else 'reject'}")
        print(f"case {case:2}: " + " ".join(row), flush=True)


if __name__ == "__main__":
    main()
