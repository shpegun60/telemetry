#!/usr/bin/env python3
"""Build/run the review fixture; restore and verify the complete internal Flash.

Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
Only --run touches the selected board. A copied Cube scaffold is required.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import runpy
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
HARNESS = runpy.run_path(str(ROOT / "tests/field_layout/h7s/run.py"))
run, sha = HARNESS["run"], HARNESS["sha"]


def measure(args, images):
    import serial
    output = args.output
    with serial.Serial(args.port, 115200, timeout=0.2):
        pass
    connection = ["-c", "port=SWD", "sn=" + args.serial, "mode=UR", "reset=HWrst", "freq=1000"]
    receipt = dict(started=datetime.now(timezone.utc).isoformat(), serial=args.serial,
                   completed=False, restored_and_verified=False, images=images, runs={})
    receipt["compiler"] = (output / "common/compiler.log").read_text().splitlines()[0]
    receipt["build_inputs"] = json.loads((output / "build-inputs.json").read_text())

    def save():
        (output / "session.json").write_text(json.dumps(receipt, indent=2) + "\n")

    def program(tag, path, raw=False):
        log = run([args.programmer, *connection, "-w", path,
                   *(["0x08000000"] if raw else []), "-v", "-rst"], output / (tag + ".log"), 60)
        if "Download verified successfully" not in log:
            raise RuntimeError("Programming verification missing: " + tag)

    backup = output / "before.bin"
    log = run([args.programmer, *connection, "-u", "0x08000000", "0x10000", backup, "-rst"],
              output / "backup.log", 60)
    if "NUCLEO-H7S3L8" not in log or not backup.is_file() or backup.stat().st_size != 65536:
        raise RuntimeError("Expected NUCLEO-H7S3L8 and a complete 64 KiB backup")
    receipt["backup_sha256"] = sha(backup)
    save()
    try:
        for image in images:
            opt = image["optimization"]
            if sha(image["elf"]) != image["elf_sha256"]:
                raise RuntimeError("Image changed after build")
            program("flash-" + opt, image["elf"])
            with serial.Serial(args.port, 115200, timeout=0.5, write_timeout=2) as port:
                time.sleep(0.3)
                port.reset_input_buffer()
                port.write(b"P")
                port.flush()
                idle = port.readline().decode("ascii").strip()
                if idle != "REVIEW IDLE 1":
                    raise RuntimeError("Unexpected idle handshake: " + idle)
                port.write(b"R")
                port.flush()
                start = time.monotonic()
                report = ""
                while time.monotonic() - start < 30 and not report:
                    report = port.readline().decode("ascii").strip()
                (output / (opt + "-uart.log")).write_text(idle + "\n" + report + "\n")
                expected = f"REVIEW RESULT 1 {2 if opt == 'O2' else 0} 600000000 3328 0 0"
                if report != expected:
                    raise RuntimeError("MCU review failed: " + report)
                receipt["runs"][opt] = dict(idle=idle, result=report)
                save()
                print("PASS " + opt + ": " + report, flush=True)
        receipt["completed"] = True
    except BaseException as error:
        receipt["run_error"] = str(error)
        raise
    finally:
        try:
            program("restore", backup, True)
            restored = output / "after.bin"
            run([args.programmer, *connection, "-u", "0x08000000", "0x10000", restored, "-rst"],
                output / "readback.log", 60)
            receipt["restored_sha256"] = sha(restored)
            receipt["restored_and_verified"] = receipt["restored_sha256"] == receipt["backup_sha256"]
            if not receipt["restored_and_verified"]:
                raise RuntimeError("Firmware readback differs from the backup")
            print("Original 64 KiB firmware restored and verified.", flush=True)
        except BaseException as error:
            receipt["restore_error"] = str(error)
            raise
        finally:
            save()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cube", type=Path, required=True)
    parser.add_argument("--arm-cxx", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--run", action="store_true")
    parser.add_argument("--serial", default="002A001F3033510135393935")
    parser.add_argument("--port", default="COM6")
    parser.add_argument("--programmer", default="C:/ST/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe")
    args = parser.parse_args()
    args.output = args.output.resolve()
    args.variants, args.optimizations = ["Current"], ["O2", "Os"]
    args.baseline_ref = None
    inputs = [Path(__file__), HERE.parent / "EmbeddedReviewCheck.hpp", ROOT / "tests/resources/Golden.hpp"]
    sources = [HERE / "Review.cpp", Path("lib/telemetry/abi/TelemetryAbi.cpp")]
    sources += [Path(f"lib/resource/telemetry/{name}File.cpp") for name in ("Schema", "Commands", "Values")]
    images = HARNESS["build"](args, args.output, fixture_sources=sources,
                                fixture_inputs=inputs, cxx_standard="c++20")
    if args.run:
        measure(args, images)


if __name__ == "__main__":
    main()
