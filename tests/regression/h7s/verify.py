#!/usr/bin/env python3
"""Validate the retained review run without touching a board (MIT).

Authors: Ruslan Kovtun (shpegun60), codexAi.
This validates an archived receipt, not a fresh hardware measurement.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(value):
    return isinstance(value, str) and len(value) == 64 and all(c in "0123456789abcdef" for c in value)


def verify(receipt):
    require(receipt.get("completed") is True and receipt.get("restored_and_verified") is True,
            "Incomplete session or restoration")
    require(not receipt.get("run_error") and not receipt.get("restore_error"), "Session error")
    require(digest(receipt.get("backup_sha256")) and
            receipt.get("backup_sha256") == receipt.get("restored_sha256"), "Restore hash mismatch")
    images = receipt["images"]
    require(len(images) == 2 and {i["optimization"] for i in images} == {"O2", "Os"}, "Image coverage")
    require(set(receipt["runs"]) == {"O2", "Os"}, "Run coverage")
    libraries = None
    for image in images:
        require(image["variant"] == "Current" and 0 < image["flash_bytes"] <= 65536, "Invalid image")
        require(digest(image["elf_sha256"]) and digest(image["binary_sha256"]), "Missing image digest")
        require(image["objects_sha256"] and all(digest(h) for h in image["objects_sha256"].values()),
                "Missing object digests")
        require(image["library_sources"] and all(digest(h) for h in image["library_sources"].values()),
                "Missing library source digests")
        if libraries is None:
            libraries = image["library_sources"]
        require(libraries == image["library_sources"], "Libraries differ between optimizations")
        opt = image["optimization"]
        run = receipt["runs"][opt]
        require(run["idle"] == "REVIEW IDLE 1", "Missing idle handshake")
        require(run["result"] == f"REVIEW RESULT 1 {2 if opt == 'O2' else 0} 600000000 3328 0 0",
                "Incomplete checks, wrong target, or a failed assertion")
    require(receipt["build_inputs"] and all(digest(h) for h in receipt["build_inputs"].values()),
            "Missing fixture and scaffold digests")
    require("14.3.1" in receipt["compiler"], "Unexpected compiler receipt")


def verify_current_code(receipt):
    """Compare board-tested code with a checkout independent of line endings."""
    root = HERE.parents[2]
    sources = receipt["images"][0]["library_sources"]
    code = {name for name in sources
            if Path(name.replace("\\", "/")).suffix in {".h", ".hpp", ".cpp", ".c", ".pri"}}
    normalized = receipt.get("library_code_lf_sha256")
    require(isinstance(normalized, dict) and set(normalized) == code and code,
            "Incomplete normalized board-tested code hashes")
    for name, expected in normalized.items():
        relative = Path(name.replace("\\", "/"))
        require(relative.parts[0] == "lib" and ".." not in relative.parts,
                "Invalid library source path")
        path = root / relative
        require(path.is_file() and digest(expected)
                and hashlib.sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest() == expected,
                f"Current library code differs from board receipt: {name}")
    return len(code)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--receipt", type=Path, default=HERE / "receipt.json")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--current-code", action="store_true",
                        help="compare retained library code hashes with the current checkout")
    args = parser.parse_args()
    receipt = json.loads(args.receipt.read_text())
    verify(receipt)
    if args.current_code:
        print(f"Current board-tested library inputs: {verify_current_code(receipt)} hashes match")
    if args.self_test:
        mutations = [
            lambda r: r.update(completed=False),
            lambda r: r.update(restored_and_verified=False),
            lambda r: r.update(restored_sha256="0" * 64),
            lambda r: r["images"].pop(),
            lambda r: r["images"][0].update(flash_bytes=65537),
            lambda r: r["images"][0].update(library_sources={}),
            lambda r: r["runs"].pop("Os"),
            lambda r: r["runs"]["O2"].update(idle=""),
            lambda r: r["runs"]["Os"].update(result="REVIEW RESULT 1 0 600000000 3328 1 42"),
            lambda r: r["runs"]["Os"].update(result="REVIEW RESULT 1 0 600000000 3327 0 0"),
            lambda r: r.update(build_inputs={}),
        ]
        for mutate in mutations:
            changed = copy.deepcopy(receipt)
            mutate(changed)
            try:
                verify(changed)
            except (ValueError, KeyError):
                continue
            raise RuntimeError("Receipt mutation was accepted")
        print(f"Review receipt: {len(mutations)} negative controls rejected")
        if args.current_code:
            changed = copy.deepcopy(receipt)
            name = next(iter(changed["library_code_lf_sha256"]))
            changed["library_code_lf_sha256"][name] = "0" * 64
            try:
                verify_current_code(changed)
            except ValueError:
                print("Changed current library hash rejected")
            else:
                raise RuntimeError("Changed current library hash was accepted")
    print("Retained H7S review: O2/Os, 3328 checks each, complete verified restoration")


if __name__ == "__main__":
    main()
