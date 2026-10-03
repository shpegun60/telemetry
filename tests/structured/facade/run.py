#!/usr/bin/env python3
"""Stage 02 facade checks. Authors: Ruslan Kovtun, codexAi. MIT."""

import argparse
import os
import re
from pathlib import Path
import subprocess
import time


ROOT = Path(__file__).resolve().parents[3]
SOURCES = Path(__file__).resolve().parent
DIAGNOSTICS = {
    1: "Structured endpoints must be noexcept",
    2: "Volatile owner methods are unsupported",
    3: "Rvalue-qualified owner methods are unsupported",
    4: "Structured endpoints cannot be variadic",
    5: "Request must be by value or const lvalue reference",
    6: "Response must be a value or void",
    7: "Response must be a value or void",
    8: "Request must be by value or const lvalue reference",
    9: "Use one request structure",
    10: "Callable requires an unambiguous function signature",
    11: "Callable requires an unambiguous function signature",
    12: "Automatic reflected member names must be ASCII identifiers",
    13: "reflection::get requires an lvalue aggregate",
    14: "Request must be by value or const lvalue reference, never volatile or a pointer",
}


def check_backend_boundary() -> None:
    library = ROOT / "lib/telemetry"
    headers = sorted(path for path in library.rglob('*')
                     if path.suffix in ('.h', '.hpp', '.cpp'))
    if not headers:
        raise RuntimeError('Reflection boundary scan found no library sources')
    for header in headers:
        if header.parent == library / "reflection/detail":
            continue
        source = header.read_text(encoding="utf-8")
        vendor_use = (r'\bboost\s*::\s*pfr\b|\bmagic_enum\s*::|'
                      r'#\s*include\s*[<"](?:boost/|magic_enum|meta[>"])')
        if re.search(vendor_use, source):
            raise RuntimeError(f"Vendor dependency escaped reflection/detail: {header}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
    parser.add_argument("--arm", action="store_true")
    parser.add_argument("--build-dir", type=Path, required=True)
    args = parser.parse_args()

    check_backend_boundary()
    output = args.build_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)

    def run(command: list[str], label: str, *, expected_failure: str = "") -> str:
        start = time.perf_counter()
        result = subprocess.run(
            command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, encoding="utf-8", errors="replace", timeout=120,
        )
        elapsed = time.perf_counter() - start
        log = f"COMMAND {command!r}\nEXIT {result.returncode}\nSECONDS {elapsed:.3f}\n{result.stdout}"
        (output / f"{label}.log").write_text(log, encoding="utf-8")
        if expected_failure:
            if result.returncode == 0 or expected_failure not in result.stdout:
                raise RuntimeError(f"{label} did not reject for the intended reason\n{log}")
        elif result.returncode != 0:
            raise RuntimeError(f"{label} failed\n{log}")
        print(f"{label}: {'expected rejection' if expected_failure else 'pass'} ({elapsed:.2f}s)", flush=True)
        return result.stdout

    version = run([args.cxx, "--version"], "compiler-version")
    print(version.splitlines()[0], flush=True)
    flags = [
        args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic-errors",
        "-fdiagnostics-color=never", "-Ilib", "-Ilib/boost_pfr/include",
        "-Ilib/magic_enum",
    ]
    if args.arm:
        flags += [
            "-mcpu=cortex-m7", "-mthumb", "-mfpu=fpv5-d16", "-mfloat-abi=hard",
            "-fno-exceptions", "-fno-rtti", "-ffunction-sections", "-fdata-sections",
        ]

    for optimization in (("O2", "Os", "Og") if args.arm else ("O2",)):
        objects = []
        for source in ("FacadeProbe.cpp", "FacadeOther.cpp"):
            obj = output / f"{optimization}-{Path(source).stem}.o"
            run(flags + [f"-{optimization}", "-c", str(SOURCES / source), "-o", str(obj)],
                f"{optimization}-{Path(source).stem}")
            objects.append(obj)
        if not args.arm:
            program = output / ("facade-probe.exe" if os.name == "nt" else "facade-probe")
            run([args.cxx, *(str(obj) for obj in objects), "-o", str(program)], "link")
            run([str(program)], "run")

    for case, message in DIAGNOSTICS.items():
        run(flags + ["-O2", f"-DCASE={case}", "-fsyntax-only", str(SOURCES / "FacadeNegative.cpp")],
            f"negative-{case}", expected_failure=message)
    print("Reflection boundary and 14 negative diagnostics passed", flush=True)


if __name__ == "__main__":
    main()
