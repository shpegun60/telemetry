"""Capture a small, source-identified scalar baseline from exact-SHA CI logs.

Authors: Ruslan Kovtun (shpegun60), codexAi. MIT license.

This reads retained ARM results. It does not compile code or run hardware.
"""

import argparse
import json
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests"))
from run_arm_checks import normalized_instructions  # noqa: E402


SYMBOLS = {
    "IndexCodegen": (
        "telemetry_probe_field_read_native",
        "telemetry_probe_static_read_native",
        "telemetry_probe_field_write_float",
        "telemetry_probe_static_write_float",
    ),
    "FieldTableCodegen": (
        "table_direct_read",
        "table_local_read",
        "table_global_read",
    ),
    "CommandTableCodegen": (
        "command_table_call_known",
        "command_table_call_global",
        "command_table_call_runtime",
    ),
}


def capture(logs, source_sha, ci_run):
    record = {
        "source_sha": source_sha,
        "ci_run": ci_run,
        "arm_compiler": (logs / "arm/compiler-version.log")
        .read_text(encoding="utf-8")
        .splitlines()[0],
        "optimizations": {},
    }

    for optimization in ("O2", "Os"):
        item = {}
        for probe, names in SYMBOLS.items():
            assembly = (logs / "arm" / f"{probe}-{optimization}-disassembly.log")
            disassembly = assembly.read_text(encoding="utf-8")
            item[probe] = {
                name: normalized_instructions(disassembly, name)
                for name in names
            }

        size_log = (logs / "arm-resources" / f"size-{optimization}.log")
        size_text = size_log.read_text(encoding="utf-8")
        item["linked_resource_sections"] = {}
        for section in ("text", "rodata", "data", "bss"):
            match = re.search(rf"^\.{section}\s+(\d+)\s+", size_text, re.MULTILINE)
            if match is None:
                raise ValueError(f"Missing .{section} in {size_log}")
            item["linked_resource_sections"][section] = int(match.group(1))

        stack_log = logs / "arm-resources" / f"stack-usage-{optimization}.log"
        stack = json.loads(stack_log.read_text(encoding="utf-8"))
        item["resource_maximum_frame_bytes"] = {
            name: details["maximum"] for name, details in stack.items()
        }
        record["optimizations"][optimization] = item

    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--logs", type=Path, required=True)
    parser.add_argument("--source-sha", required=True)
    parser.add_argument("--ci-run", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    if not re.fullmatch(r"[0-9a-f]{40}", args.source_sha):
        parser.error("--source-sha must be a full lowercase Git commit SHA")

    baseline = capture(args.logs, args.source_sha, args.ci_run)
    args.output.write_text(json.dumps(baseline, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"Captured scalar baseline from {args.ci_run}")


if __name__ == "__main__":
    main()
