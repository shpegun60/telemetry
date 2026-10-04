#!/usr/bin/env python3
"""Codec byte/lifetime/allocation checks and offline Cortex-M7 stack inspection."""

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import time


ROOT = Path(__file__).resolve().parents[3]
SOURCES = Path(__file__).resolve().parent


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
    parser.add_argument("--arm", action="store_true")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--build-dir", type=Path, required=True)
    args = parser.parse_args()
    if args.arm and args.sanitize:
        parser.error("--arm and --sanitize cannot be combined")

    output = args.build_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)

    def run(command: list[str], label: str) -> str:
        start = time.perf_counter()
        result = subprocess.run(
            command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, encoding="utf-8", errors="replace", timeout=180,
        )
        duration = time.perf_counter() - start
        log = f"COMMAND {command!r}\nEXIT {result.returncode}\nSECONDS {duration:.3f}\n{result.stdout}"
        (output / f"{label}.log").write_text(log, encoding="utf-8")
        if result.returncode:
            raise RuntimeError(f"{label} failed\n{log}")
        print(f"{label}: pass ({duration:.2f}s)", flush=True)
        return result.stdout

    def reject(command: list[str], label: str, reason: str) -> None:
        start = time.perf_counter()
        result = subprocess.run(
            command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, encoding="utf-8", errors="replace", timeout=180,
        )
        duration = time.perf_counter() - start
        log = f"COMMAND {command!r}\nEXIT {result.returncode}\nSECONDS {duration:.3f}\n{result.stdout}"
        (output / f"{label}.log").write_text(log, encoding="utf-8")
        if result.returncode == 0 or reason not in result.stdout:
            raise RuntimeError(f"{label} missed its intended diagnostic\n{log}")
        print(f"{label}: expected rejection ({duration:.2f}s)", flush=True)

    run([args.cxx, "--version"], "compiler-version")
    flags = [
        args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic-errors",
        "-fdiagnostics-color=never", "-Ilib", "-Ilib/boost_pfr/include",
        "-Ilib/magic_enum",
    ]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    if args.arm:
        flags += [
            "-mcpu=cortex-m7", "-mthumb", "-mfpu=fpv5-d16", "-mfloat-abi=hard",
            "-fno-exceptions", "-fno-rtti", "-ffunction-sections", "-fdata-sections",
            "-fstack-usage",
        ]
        for optimization in ("O2", "Os", "Og"):
            for source in ("CodecProbe.cpp", "ResultProbe.cpp", "ArmCodegen.cpp"):
                stem = Path(source).stem
                object_path = output / f"{optimization}-{stem}.o"
                run(flags + [f"-{optimization}", "-c", str(SOURCES / source),
                             "-o", str(object_path)], f"{optimization}-{stem}")

        for optimization in ("O2", "Os"):
            source = SOURCES / "ArmArrayExpansion.cpp"
            obj = output / f"{optimization}-ArmArrayExpansion.o"
            run(flags + [f"-{optimization}", "-c", str(source), "-o", str(obj)],
                f"{optimization}-ArmArrayExpansion")

        for optimization in ("O2", "Os", "Og"):
            usage = (output / f"{optimization}-ArmCodegen.su").read_text(encoding="utf-8")
            for symbol in ("encode_big", "decode_big", "direct_return",
                           "forwarded_return", "service_return",
                           "make_actual_service", "actual_service_return",
                           "decode_defaulted_big"):
                match = re.search(rf"\b{symbol}\([^\n]*\)\s*\t(\d+)\tstatic", usage)
                if match is None:
                    raise RuntimeError(f"Missing static stack record for {symbol} at {optimization}")
                frame = int(match.group(1))
                if frame > 256:
                    raise RuntimeError(f"{optimization} {symbol} hides a {frame}-byte frame")
                print(f"{optimization} {symbol}: {frame} B stack", flush=True)

            for comparison in ("construct_at_return", "make_optional_service"):
                match = re.search(rf"\b{comparison}\([^\n]*\)\s*\t(\d+)\tstatic", usage)
                if match is None:
                    raise RuntimeError(f"Missing {comparison} stack comparison")
                print(f"{optimization} {comparison} comparison: {match.group(1)} B stack",
                      flush=True)

        # The expanded comparison is evidence, not an alternative production
        # path. Its object section must remain larger than the looped codec.
        tool = Path(args.cxx)
        size_tool = tool.with_name("arm-none-eabi-size" + tool.suffix)
        size_command = str(size_tool) if size_tool.exists() else shutil.which("arm-none-eabi-size")
        if not size_command:
            raise RuntimeError("arm-none-eabi-size is required for the ARM comparison")
        for optimization in ("O2", "Os"):
            loop_obj = output / f"{optimization}-ArmCodegen.o"
            expanded_obj = output / f"{optimization}-ArmArrayExpansion.o"
            loop = run([size_command, "-A", str(loop_obj)], f"size-{optimization}-loop")
            expanded = run([size_command, "-A", str(expanded_obj)],
                           f"size-{optimization}-expanded")
            def text_size(report: str) -> int:
                return sum(int(size) for size in re.findall(
                    r"^\.text\S*\s+(\d+)\s+\d+$", report, re.M))
            loop_bytes = text_size(loop)
            expanded_bytes = text_size(expanded)
            if not loop_bytes or not expanded_bytes:
                raise RuntimeError("ARM text sections were not reported")
            if expanded_bytes <= loop_bytes:
                raise RuntimeError("Array expansion is no longer larger than the loop")
            print(f"{optimization} array decode text: loop {loop_bytes} B, "
                  f"expanded {expanded_bytes} B", flush=True)
    else:
        for source in ("CodecProbe.cpp", "NoHeapProbe.cpp", "ResultProbe.cpp"):
            stem = Path(source).stem
            program = output / (stem + (".exe" if os.name == "nt" else ""))
            run(flags + ["-O2", str(SOURCES / source), "-o", str(program)],
                f"compile-{stem}")
            run([str(program)], f"run-{stem}")

    negative_flags = [flag for flag in flags if flag != "-fstack-usage"]
    reject(negative_flags + ["-O2", "-fsyntax-only", str(SOURCES / "ResultNegative.cpp")],
           "negative-large-service", "Large service response must use successFrom")
    reject(negative_flags + ["-O2", "-fsyntax-only", str(SOURCES / "CodecNegative.cpp")],
           "negative-dmi-expansion", "DMI construction expands too many members")

    print("Stage 04 codec and workspace checks passed", flush=True)


if __name__ == "__main__":
    main()
