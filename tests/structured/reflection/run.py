#!/usr/bin/env python3
"""Stage 01 C++20 PFR/magic_enum checks. Authors: Ruslan Kovtun, codexAi. MIT."""

import argparse
import os
from pathlib import Path
import re
import subprocess
import time


ROOT = Path(__file__).resolve().parents[3]
PROBES = Path(__file__).resolve().parent


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
    parser.add_argument("--arm", action="store_true")
    parser.add_argument("--build-dir", type=Path, required=True)
    args = parser.parse_args()

    output = args.build_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)

    def run(command: list[str], label: str, *, reject: bool = False) -> str:
        started = time.perf_counter()
        result = subprocess.run(
            command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, encoding="utf-8", errors="replace", timeout=120,
        )
        elapsed = time.perf_counter() - started
        log = f"COMMAND {command!r}\nEXIT {result.returncode}\nSECONDS {elapsed:.3f}\n{result.stdout}"
        (output / f"{label}.log").write_text(log, encoding="utf-8")
        if (result.returncode == 0) == reject:
            raise RuntimeError(f"{label}: unexpected exit\n{log}")
        print(f"{label}: {'expected rejection' if reject else 'pass'} ({elapsed:.2f}s)", flush=True)
        return result.stdout

    version = run([args.cxx, "--version"], "compiler-version")
    print(version.splitlines()[0], flush=True)

    common = [
        args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic-errors",
        "-fdiagnostics-color=never", "-Ilib/boost_pfr/include", "-Ilib/magic_enum",
    ]
    if args.arm:
        common += [
            "-mcpu=cortex-m7", "-mthumb", "-mfpu=fpv5-d16", "-mfloat-abi=hard",
            "-fno-exceptions", "-fno-rtti", "-ffunction-sections", "-fdata-sections",
        ]

    for optimization in (("O2", "Os", "Og") if args.arm else ("O2",)):
        objects = []
        for source in ("PfrProbe.cpp", "PfrOther.cpp"):
            obj = output / f"{optimization}-{Path(source).stem}.o"
            command = common + [f"-{optimization}", "-c", str(PROBES / source), "-o", str(obj)]
            run(command, f"{optimization}-{Path(source).stem}")
            print(f"  {source} object: {obj.stat().st_size} bytes", flush=True)
            objects.append(obj)

        if args.arm:
            compiler = Path(args.cxx)
            size_tool = str(compiler.with_name("arm-none-eabi-size" + compiler.suffix))
            sections = run([size_tool, *(str(obj) for obj in objects)], f"{optimization}-sections")
            print(sections.strip(), flush=True)

        if not args.arm:
            program = output / ("pfr-probe.exe" if os.name == "nt" else "pfr-probe")
            run([args.cxx, *(str(obj) for obj in objects), "-o", str(program)], "link")
            response = run([str(program)], "run")
            if not re.search(r"PFR=voltage,rpm; sparse=Far; alias=\w+", response):
                raise RuntimeError(f"Unexpected reflection output: {response!r}")
            print(response.strip(), flush=True)

    rejection = run(
        common + ["-O2", "-fsyntax-only", str(PROBES / "UnsupportedNameCompileFail.cpp")],
        "non-ascii-name-rejected", reject=True,
    )
    if "Automatic reflected member names must be ASCII" not in rejection:
        raise RuntimeError("Non-ASCII probe failed for a reason unrelated to its contract")


if __name__ == "__main__":
    main()
