"""Critic trial: wall-clock compile times for the consumer and library translation units.

Run from the repository root:  python tests/review/critic/time_builds.py <compiler> <tag> [extra flags...]
Objects go to build/review-critic/<tag>/; nothing else is written.
"""
import os
import statistics
import subprocess
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
CXX = sys.argv[1]
TAG = sys.argv[2]
EXTRA = sys.argv[3:]
OUT = os.path.join(ROOT, "build", "review-critic", TAG)
os.makedirs(OUT, exist_ok=True)
WARN = ["-Wall", "-Wextra", "-Werror", "-pedantic-errors"]

UNITS = [
    ("baseline <cstdio> only", "c++17", ["tests/review/critic/Baseline.cpp"], []),
    ("Telemetry.h only (empty TU)", "c++17", ["tests/review/critic/IncludeOnly.cpp"], ["-Ilib/telemetry"]),
    ("consumer fields+commands+JSON", "c++17", ["tests/review/critic/CriticMain.cpp"], ["-Ilib/telemetry"]),
    ("consumer resources (C++20)", "c++20", ["tests/review/critic/CriticResources.cpp"], ["-Ilib", "-Ilib/telemetry"]),
    ("lib TelemetryAbi.cpp", "c++17", ["lib/telemetry/abi/TelemetryAbi.cpp"], ["-Ilib/telemetry"]),
    ("lib TelemetryJson.cpp", "c++17", ["lib/telemetry/serialization/TelemetryJson.cpp"], ["-Ilib/telemetry"]),
    ("lib TelemetryCommandJson.cpp", "c++17", ["lib/telemetry/serialization/TelemetryCommandJson.cpp"], ["-Ilib/telemetry"]),
    ("lib resource Protocol.cpp", "c++20", ["lib/resource/protocol/Protocol.cpp"], ["-Ilib"]),
    ("lib resource SchemaFile.cpp", "c++20", ["lib/resource/telemetry/SchemaFile.cpp"], ["-Ilib"]),
    ("lib resource CommandsFile.cpp", "c++20", ["lib/resource/telemetry/CommandsFile.cpp"], ["-Ilib"]),
    ("lib resource ValuesFile.cpp", "c++20", ["lib/resource/telemetry/ValuesFile.cpp"], ["-Ilib"]),
]

REPEAT = 3
total = 0.0
for label, std, sources, incs in UNITS:
    obj = os.path.join(OUT, os.path.basename(sources[0]).replace(".cpp", ".o"))
    cmd = [CXX, "-std=" + std] + WARN + EXTRA + incs + ["-c"] + sources + ["-o", obj]
    times = []
    for _ in range(REPEAT):
        t0 = time.perf_counter()
        r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        times.append(time.perf_counter() - t0)
        if r.returncode != 0:
            print("FAILED:", " ".join(cmd))
            print(r.stderr[:4000])
            sys.exit(1)
    med = statistics.median(times)
    total += med
    print(f"{label:34s} {med:6.2f} s  (min {min(times):.2f})")
print(f"{'sum of medians':34s} {total:6.2f} s")
