"""Critic trial: link the Cortex-M7 consumer variants with newlib-nano and report arm-none-eabi-size.

python tests/review/critic/arm_sizes.py <arm-none-eabi-g++> [-O2|-Os]
Outputs go to build/review-critic/arm<opt>/ only. This links images; it never flashes anything.
"""
import os
import subprocess
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
CXX = sys.argv[1]
OPT = sys.argv[2] if len(sys.argv) > 2 else "-O2"
BIN = os.path.dirname(CXX)
SIZE = os.path.join(BIN, "arm-none-eabi-size" + (".exe" if CXX.endswith(".exe") else ""))
OUT = os.path.join(ROOT, "build", "review-critic", "arm" + OPT.replace("-", ""))
os.makedirs(OUT, exist_ok=True)

CPU = ["-mcpu=cortex-m7", "-mthumb", "-mfpu=fpv5-d16", "-mfloat-abi=hard"]
COMMON = CPU + [OPT, "-fno-exceptions", "-fno-rtti", "-ffunction-sections", "-fdata-sections",
                "-Wall", "-Wextra", "-Werror", "-pedantic-errors"]
LINK = CPU + ["--specs=nano.specs", "--specs=nosys.specs", "-Wl,--gc-sections"]

T = "lib/telemetry/"
R = "lib/resource/"
CORE_LIB = [(T + "abi/TelemetryAbi.cpp", "c++17", ["-Ilib/telemetry"])]
JSON_LIB = [(T + "serialization/TelemetryJson.cpp", "c++17", ["-Ilib/telemetry"]),
            (T + "serialization/TelemetryCommandJson.cpp", "c++17", ["-Ilib/telemetry"])]
RES_LIB = [(R + "protocol/Protocol.cpp", "c++20", ["-Ilib"]),
           (R + "telemetry/SchemaFile.cpp", "c++20", ["-Ilib"]),
           (R + "telemetry/CommandsFile.cpp", "c++20", ["-Ilib"]),
           (R + "telemetry/ValuesFile.cpp", "c++20", ["-Ilib"]),
           ("tests/review/critic/CriticFirmwareResources.cpp", "c++20", ["-Ilib", "-Ilib/telemetry"])]

VARIANTS = [
    ("baseline", [], [], []),
    ("fields-only", ["-DCRITIC_CORE", "-DCRITIC_NO_EXEC"], CORE_LIB, []),
    ("core-bare-cmds", ["-DCRITIC_CORE", "-DCRITIC_BARE_COMMANDS"], CORE_LIB, []),
    ("core", ["-DCRITIC_CORE"], CORE_LIB, []),
    ("core+json", ["-DCRITIC_CORE", "-DCRITIC_JSON"], CORE_LIB + JSON_LIB, ["-Wl,-u,_printf_float"]),
    ("core+res", ["-DCRITIC_CORE", "-DCRITIC_RES"], CORE_LIB + RES_LIB, []),
    ("core+json+res", ["-DCRITIC_CORE", "-DCRITIC_JSON", "-DCRITIC_RES"], CORE_LIB + JSON_LIB + RES_LIB,
     ["-Wl,-u,_printf_float"]),
]


def run(cmd):
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        print("FAILED:", " ".join(cmd))
        print(r.stdout[-3000:], r.stderr[-6000:])
        sys.exit(1)
    return r.stdout


rows = []
for name, defs, libs, extra_link in VARIANTS:
    objs = []
    t0 = time.perf_counter()
    main_obj = os.path.join(OUT, f"main_{name}.o")
    run([CXX, "-std=c++17"] + COMMON + defs + ["-Ilib/telemetry", "-c", "tests/review/critic/CriticFirmware.cpp",
                                              "-o", main_obj])
    objs.append(main_obj)
    for src, std, incs in libs:
        obj = os.path.join(OUT, os.path.basename(src).replace(".cpp", ".o"))
        run([CXX, "-std=" + std] + COMMON + incs + ["-c", src, "-o", obj])
        objs.append(obj)
    elf = os.path.join(OUT, f"critic_{name}.elf")
    run([CXX] + LINK + extra_link + objs + ["-o", elf, "-Wl,-Map=" + elf + ".map"])
    dt = time.perf_counter() - t0
    out = run([SIZE, elf]).splitlines()[-1].split()
    text, data, bss = int(out[0]), int(out[1]), int(out[2])
    rows.append((name, text, data, bss, dt))

base = rows[0]
print(f"{'variant':16s} {'text':>8s} {'data':>6s} {'bss':>6s}   {'dtext':>7s} {'ddata':>6s} {'dbss':>6s}  build")
for name, text, data, bss, dt in rows:
    print(f"{name:16s} {text:8d} {data:6d} {bss:6d}   {text - base[1]:7d} {data - base[2]:6d} {bss - base[3]:6d}  {dt:5.1f}s")
