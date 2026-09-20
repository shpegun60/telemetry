#!/usr/bin/env python3
"""Check the ABI 6 -> 7 field-schema delta against an archived source tree."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--current", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--cxx", default="g++")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    compiler = shutil.which(args.cxx)
    if not compiler:
        raise SystemExit(f"Compiler not found: {args.cxx}")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ)
    environment["PATH"] = str(Path(compiler).parent) + os.pathsep + environment.get("PATH", "")
    records = {}
    for label, root in (("before", args.baseline.resolve()), ("after", args.current.resolve())):
        library = root / "lib/telemetry"
        executable = output / (label + (".exe" if os.name == "nt" else ""))
        command = [compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
                   "-DTELEMETRY_BASELINE_IDS=0", "-I" + str(library),
                   str(args.current.resolve() / "tests/position_tables/SchemaParity.cpp"),
                   str(library / "abi/TelemetryAbi.cpp"),
                   str(library / "serialization/TelemetryJson.cpp"),
                   str(library / "serialization/TelemetryCommandJson.cpp"), "-o", str(executable)]
        result = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, env=environment)
        (output / f"{label}-compile.log").write_text(result.stdout, encoding="utf-8")
        result.check_returncode()
        result = subprocess.run([str(executable)], text=True, capture_output=True,
                                check=True, env=environment)
        (output / f"{label}.jsonl").write_text(result.stdout, encoding="utf-8")
        lines = result.stdout.splitlines()
        if len(lines) != 3:
            raise SystemExit(f"Expected field schema, values and command schema from {label}")
        records[label] = lines

    before, after = records["before"], records["after"]
    if before[1:] != after[1:]:
        raise SystemExit("Value JSON or command schema changed")
    old_schema, new_schema = json.loads(before[0]), json.loads(after[0])
    old_hash, new_hash = old_schema.pop("schema"), new_schema.pop("schema")
    if old_hash == new_hash:
        raise SystemExit("Field schema fingerprint did not change")
    count = 0
    for catalog in old_schema["catalogs"]:
        for field in catalog["fields"]:
            if "f" in field:
                raise SystemExit("Baseline already contains field policy; use an ABI 6 source tree")
    for catalog in new_schema["catalogs"]:
        for field in catalog["fields"]:
            if type(field.get("f")) is not int or field.pop("f") != 0:
                raise SystemExit("Every unchanged field must export a numeric zero flag mask")
            count += 1
    if not count or old_schema != new_schema:
        raise SystemExit("Metadata changed beyond the intended field mask and fingerprint")
    receipt = {"fields": count, "before_fingerprint": old_hash, "after_fingerprint": new_hash,
               "only_field_flags_added": True, "values_identical": True,
               "command_schema_identical": True}
    (output / "comparison.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(receipt))


if __name__ == "__main__":
    main()
