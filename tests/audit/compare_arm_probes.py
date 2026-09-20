#!/usr/bin/env python3
"""Compare instruction encodings in two run_arm_checks.py output directories."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def encodings(path):
    # Ignore function addresses, labels and relocation-only lines. Preserve
    # actual instruction words and literal pools, including branch encodings.
    words = []
    for line in path.read_text(encoding="utf-8").splitlines():
        columns = line.split("\t")
        if len(columns) >= 3 and re.fullmatch(r"\s*[0-9a-fA-F]+:\s*", columns[0]):
            encoded = columns[1].replace(" ", "")
            if re.fullmatch(r"[0-9a-fA-F]+", encoded):
                words.append(encoded.lower())
    if not words:
        raise ValueError(f"No instruction encodings in {path}")
    return words


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--after", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--allow-new-probes", action="store_true",
                        help="compare every baseline probe while permitting additional new probes")
    args = parser.parse_args()
    old = {p.name: p for p in args.before.glob("*Codegen-*-disassembly.log")}
    new = {p.name: p for p in args.after.glob("*Codegen-*-disassembly.log")}
    if not old or not old.keys() <= new.keys() or (not args.allow_new_probes and old.keys() != new.keys()):
        raise SystemExit("Missing or different probe sets; run both complete ARM checks first")
    records = []
    for name in sorted(old):
        before, after = encodings(old[name]), encodings(new[name])
        same = before == after
        records.append({"probe": name, "identical": same, "words": len(after),
                        "before_sha256": hashlib.sha256("\n".join(before).encode()).hexdigest(),
                        "after_sha256": hashlib.sha256("\n".join(after).encode()).hexdigest()})
        print(f"{'PASS' if same else 'DIFF'} {name}")
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")
    if not all(record["identical"] for record in records):
        raise SystemExit("ARM probe encodings changed; inspect the disassembly and relocations")
    print(f"{len(records)} probe/optimization pairs have identical instruction encodings")
    if args.allow_new_probes and new.keys() != old.keys():
        print(f"{len(new.keys() - old.keys())} additional pairs are covered by the current ARM checks")


if __name__ == "__main__":
    main()
