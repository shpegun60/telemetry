#!/usr/bin/env python3
"""Check authored file comments, dual guards and pinned source formatting.

Vendor sources and sealed measurement inputs retain their original bytes. The
same inventory is used by CI, formatting checks and --fix-guards, so new
maintained sources do not quietly miss the agreed conventions. Formatting is
checked without rewriting source; only --fix-guards explicitly changes files.
Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""
import argparse
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
VENDOR = ("lib/boost_pfr/", "lib/magic_enum/", "lib/delegate/", "doc/evidence/")
SOURCE = {".h", ".hpp", ".cpp", ".c", ".js", ".mjs", ".py", ".pri", ".pro", ".yml", ".sh", ".S"}
FORMATTED_SOURCE = {".h", ".hpp", ".cpp", ".c", ".js", ".mjs"}
CLANG_FORMAT_VERSION = "22.1.8"


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


def normalized_lf(text):
    """Ignore checkout line endings, but preserve every other source byte."""
    return text.replace("\r\n", "\n")


def require_formatter(command):
    """Resolve the requested binary and reject a different formatter version."""
    executable = shutil.which(command)
    if executable is None:
        raise SystemExit("clang-format executable was not found: " + command)
    version = subprocess.check_output([executable, "--version"], text=True).strip()
    match = re.search(r"\bclang-format version ([0-9]+(?:\.[0-9]+)+)(?=\s|$)", version)
    if not match or match[1] != CLANG_FORMAT_VERSION:
        raise SystemExit(f"Expected clang-format {CLANG_FORMAT_VERSION}, got: {version}")
    return executable


def formatted_source(executable, path, text):
    """Use the checked-in configuration and the real file's language selection."""
    result = subprocess.run(
        [executable, "--style=file", "--fallback-style=none",
         "--assume-filename=" + str(path)],
        input=text.encode("utf-8"), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        reason = result.stderr.decode("utf-8", errors="replace").strip()
        raise SystemExit(f"clang-format failed for {path}: {reason}")
    return normalized_lf(result.stdout.decode("utf-8"))


def formatting_matches(executable, path, text):
    return normalized_lf(text) == formatted_source(executable, path, text)


def formatter_controls(executable):
    """Prove the same comparison accepts canonical text and rejects a mutation."""
    path = ROOT / "tools/source_style/FormatControl.cpp"
    malformed = "/* Formatter comparison control. */\nint control( ){return 1;}\n"
    canonical = formatted_source(executable, path, malformed)
    if canonical == malformed:
        raise SystemExit("Formatter control did not change malformed source")
    if not formatting_matches(executable, path, canonical):
        raise SystemExit("Formatter control rejected canonical source")
    if formatting_matches(executable, path, malformed):
        raise SystemExit("Formatter control accepted malformed source")
    if not formatting_matches(executable, path, canonical.replace("\n", "\r\n")):
        raise SystemExit("Formatter control rejected normalized CRLF source")
    print("Formatting controls: canonical accepted, malformed rejected, CRLF accepted")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fix-guards", action="store_true")
    parser.add_argument("--clang-format", metavar="EXECUTABLE",
                        help="Check C++/JavaScript using exactly clang-format 22.1.8")
    parser.add_argument("--self-test", action="store_true",
                        help="Run positive and negative formatter controls before checking sources")
    args = parser.parse_args()
    if args.self_test and not args.clang_format:
        parser.error("--self-test requires --clang-format")
    formatter = require_formatter(args.clang_format) if args.clang_format else None
    if args.self_test:
        formatter_controls(formatter)
    failures, headers, sources, formatted = [], 0, 0, 0
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
        if formatter and path.suffix in FORMATTED_SOURCE:
            formatted += 1
            if not formatting_matches(formatter, path, text):
                failures.append(name + f": differs from clang-format {CLANG_FORMAT_VERSION}")
    if failures:
        raise SystemExit("\n".join(failures))
    print(f"Source style: {sources} file-purpose headers and {headers} dual guards checked")
    if formatter:
        print(f"Source formatting: {formatted} C++/JavaScript files checked with clang-format {CLANG_FORMAT_VERSION}")


if __name__ == "__main__":
    main()
