#!/usr/bin/env python3
"""Check authored file-purpose comments and dual C++ header inclusion guards.

Vendor sources and sealed measurement inputs retain their original bytes. The
same inventory is used by CI and by --fix-guards, so new maintained headers do
not quietly miss the agreed guard convention. This tool never formats code.
Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""
import argparse
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
VENDOR = ("lib/boost_pfr/", "lib/magic_enum/", "lib/delegate/", "doc/evidence/")
SOURCE = {".h", ".hpp", ".cpp", ".c", ".js", ".mjs", ".py", ".pri", ".pro", ".yml", ".sh", ".S"}


def maintained_files():
    names = set()
    for flags in (["--cached"], ["--others", "--exclude-standard"]):
        names.update(subprocess.check_output(
            ["git", "ls-files", *flags], cwd=ROOT, text=True).splitlines())
    return [ROOT / name for name in sorted(names)
            if not name.startswith(VENDOR) and "/h7s/" not in name and "/evidence/" not in name
            and (ROOT / name).is_file()
            and ((ROOT / name).suffix in SOURCE or (ROOT / name).name == "CMakeLists.txt")]


def without_comments(text):
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)


def header_guard(text):
    code = without_comments(text)
    match = re.match(r"\s*#ifndef\s+(\w+)\s*\n\s*#define\s+\1\s*\n", code)
    return match[1] if match else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fix-guards", action="store_true")
    args = parser.parse_args()
    failures, headers, sources = [], 0, 0
    guards = {}
    for path in maintained_files():
        name = path.relative_to(ROOT).as_posix()
        text = path.read_text(encoding="utf-8-sig")
        if path.suffix in {".h", ".hpp"}:
            headers += 1
            guard = header_guard(text)
            if args.fix_guards:
                if guard:
                    if not re.search(r"^\s*#pragma once\s*$", text, re.M):
                        text = re.sub(r"(#define\s+" + re.escape(guard) + r"[^\n]*\n)",
                                      r"\1#pragma once\n", text, count=1)
                else:
                    guard = "TELEMETRY_" + re.sub(r"[^A-Za-z0-9]", "_", name).upper()
                    # Keep the file-purpose comment first, then the dual guard.
                    once = re.search(r"^\s*#pragma once\s*$", text, re.M)
                    if not once:
                        failures.append(name + ": no existing guard or pragma once")
                        continue
                    text = text[:once.start()] + "\n#ifndef " + guard + "\n#define " + guard + "\n#pragma once\n" + text[once.end():]
                    text = text.rstrip() + "\n\n#endif // " + guard + "\n"
                path.write_text(text, encoding="utf-8", newline="\n")
            if not guard or not re.search(r"^\s*#pragma once\s*$", text, re.M):
                failures.append(name + ": expected include guard and pragma once")
            if guard and guard in guards:
                failures.append(name + ": guard duplicates " + guards[guard])
            if guard:
                guards[guard] = name
        sources += 1
        stripped = text.lstrip()
        if path.suffix in {".cpp", ".c", ".h", ".hpp", ".js", ".mjs"}:
            if not stripped.startswith(("/*", "//")):
                failures.append(name + ": missing leading file-purpose comment")
        elif path.suffix == ".py":
            if stripped.startswith("#!"):
                stripped = stripped.split("\n", 1)[1].lstrip()
            if not stripped.startswith(('"""', "'''", "#")):
                failures.append(name + ": missing leading file-purpose documentation")
        elif not stripped.startswith("#"):
            failures.append(name + ": missing leading file-purpose comment")
    if failures:
        raise SystemExit("\n".join(failures))
    print(f"Source style: {sources} file-purpose headers and {headers} dual guards checked")


if __name__ == "__main__":
    main()
