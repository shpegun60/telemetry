#!/usr/bin/env python3
"""Check authored Markdown file links and GitHub-style local heading anchors.

Use the Git inventory rather than generated build trees. Retained evidence and
vendor documents are valid targets, but are not rewritten or treated as current
authored guides. No network requests or third-party parser dependencies are used.
Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""
import argparse
from dataclasses import dataclass
import html
import json
from pathlib import Path
import re
import subprocess
import tempfile
import unicodedata
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[2]
RETAINED_OR_VENDOR = (
    "lib/boost_pfr/", "lib/magic_enum/", "lib/delegate/", "doc/evidence/",
)
INLINE_CODE = re.compile(r"(?<!`)(`+)(?!`)(.*?)\1(?!`)", re.S)


@dataclass(frozen=True)
class Link:
    """A link target and its original one-based source line."""
    destination: str
    line: int


def without_code(text):
    """Mask fenced/indented code without changing source line positions."""
    lines = text.splitlines(keepends=True)
    result = []
    fence = None
    for line in lines:
        opening = re.match(r"^ {0,3}(`{3,}|~{3,})", line)
        if fence:
            closing = re.match(r"^ {0,3}" + re.escape(fence[0]) +
                               "{" + str(fence[1]) + r",}\s*$", line.rstrip())
            if closing:
                fence = None
            result.append("\n" if line.endswith("\n") else "")
        elif opening:
            fence = (opening[1][0], len(opening[1]))
            result.append("\n" if line.endswith("\n") else "")
        elif line.startswith("    ") or line.startswith("\t"):
            result.append("\n" if line.endswith("\n") else "")
        else:
            result.append(line)
    return "".join(result)


def heading_text(text):
    """Keep rendered label/code text while removing Markdown/HTML markup."""
    text = INLINE_CODE.sub(lambda match: html.escape(match[2]).replace("_", "&#95;"), text)
    text = text.replace(r"\_", "&#95;")
    text = re.sub(r"!?\[([^\]]*)\]\([^)]*\)", r"\1", text)
    text = re.sub(r"!?\[([^\]]*)\]\[[^\]]*\]", r"\1", text)
    text = re.sub(r"(?<!\w)(_+)(?=\S)(.+?)(?<=\S)\1(?!\w)", r"\2", text)
    text = re.sub(r"<[^>]*>", "", text)
    return html.unescape(re.sub(r"\\([!\"#$%&'()*+,\-./:;<=>?@\[\]^_`{|}~])", r"\1", text))


def github_slug(text):
    """Apply GitHub's case/space/punctuation rules, retaining Unicode letters."""
    value = heading_text(text).lower()
    value = "".join(char for char in value if char in "-_ " or
                    unicodedata.category(char)[0] in "LNM")
    return value.replace(" ", "-")


def anchors(text):
    """Collect ATX, setext and explicit HTML anchors with unique heading IDs."""
    plain = without_code(text)
    result = set(re.findall(r"<(?:a|[hH][1-6])\b[^>]*\b(?:id|name)=[\"']([^\"']+)", plain))
    occupied = set()
    previous = ""
    for line in plain.splitlines():
        heading = re.match(r"^ {0,3}#{1,6}(?:[ \t]+(.*)|$)", line)
        title = None
        if heading:
            title = re.sub(r"[ \t]+#+[ \t]*$", "", heading[1] or "").strip()
        elif previous.strip() and re.fullmatch(r" {0,3}(?:=+|-+)\s*", line):
            title = previous.strip()
        if title is not None:
            base = github_slug(title)
            slug = base
            suffix = 0
            while slug in occupied:
                suffix += 1
                slug = base + "-" + str(suffix)
            occupied.add(slug)
            result.add(slug)
        previous = line
    return result


def label_key(label):
    return " ".join(label.split()).casefold()


def destination(text):
    """Extract a destination, ignoring an optional quoted link title."""
    text = text.strip()
    if text.startswith("<"):
        end = text.find(">")
        return text[1:end] if end >= 0 else ""
    return re.split(r"\s+", text, maxsplit=1)[0] if text else ""


def links(text):
    """Read inline/reference links and images, ignoring code and HTML comments."""
    plain = without_code(text)
    plain = re.sub(r"<!--.*?-->", lambda match: "\n" * match[0].count("\n"), plain, flags=re.S)
    plain = INLINE_CODE.sub(lambda match: "".join("\n" if char == "\n" else " "
                                                  for char in match[0]), plain)
    references = {}
    for match in re.finditer(r"^ {0,3}\[([^\]\n]+)\]:[ \t]*(.+)$", plain, re.M):
        references[label_key(match[1])] = destination(match[2])
    result = []
    position = 0
    while position < len(plain):
        match = re.search(r"(?<!\\)\[([^\]\n]+)\]", plain[position:])
        if not match:
            break
        start = position + match.start()
        end = position + match.end()
        label = match[1]
        if plain[end:end + 1] == ":":
            position = end + 1
            continue
        target = None
        if plain[end:end + 1] == "(":
            depth = 1
            cursor = end + 1
            escaped = False
            while cursor < len(plain) and depth:
                char = plain[cursor]
                if not escaped:
                    if char == "(":
                        depth += 1
                    elif char == ")":
                        depth -= 1
                escaped = char == "\\" and not escaped
                cursor += 1
            if depth == 0:
                target = destination(plain[end + 1:cursor - 1])
                end = cursor
        elif plain[end:end + 1] == "[":
            closing = plain.find("]", end + 1)
            if closing >= 0:
                key = plain[end + 1:closing] or label
                target = references.get(label_key(key))
                end = closing + 1
        else:
            target = references.get(label_key(label))
        if target is not None:
            target = re.sub(r"\\([()])", r"\1", target)
            result.append(Link(target, plain.count("\n", 0, start) + 1))
        position = end
    for match in re.finditer(r"<(?:a|img)\b[^>]*\b(?:href|src)=[\"']([^\"']+)", plain):
        result.append(Link(html.unescape(match[1]), plain.count("\n", 0, match.start()) + 1))
    return result


def authored_documents(root):
    """Include tracked/new authored Markdown, never ignored generated files."""
    names = set()
    for flags in (["--cached"], ["--others", "--exclude-standard"]):
        names.update(subprocess.check_output(
            ["git", "ls-files", "-z", *flags], cwd=root, text=True, encoding="utf-8").split("\0"))
    return [root / name for name in sorted(names) if name.lower().endswith(".md")
            and not name.startswith(RETAINED_OR_VENDOR) and (root / name).is_file()]


def check_documents(root, documents):
    """Return deterministic diagnostics and actual file/anchor check counts."""
    root = root.resolve()
    cache = {}
    errors = []
    local_count = 0
    anchor_count = 0
    external_count = 0
    for source in documents:
        for link in links(source.read_text(encoding="utf-8-sig")):
            url = urlsplit(link.destination)
            if url.scheme or url.netloc:
                external_count += 1
                continue
            local_count += 1
            path = unquote(url.path)
            target = (root / path.lstrip("/")) if path.startswith("/") else (source.parent / path)
            target = target.resolve()
            location = source.relative_to(root).as_posix() + ":" + str(link.line)
            if not target.exists():
                errors.append(f"{location}: missing file: {link.destination}")
                continue
            fragment = unquote(url.fragment)
            if fragment and target.suffix.lower() == ".md":
                anchor_count += 1
                if target not in cache:
                    cache[target] = anchors(target.read_text(encoding="utf-8-sig"))
                if fragment not in cache[target]:
                    errors.append(f"{location}: missing heading anchor: {link.destination}")
    return {"documents": len(documents), "local_links": local_count,
            "heading_links": anchor_count, "external_links_skipped": external_count,
            "errors": errors, "completed": not errors}


def self_test():
    """Exercise real file/anchor failures and rendering/duplicate controls."""
    checks = 0

    def require(condition, label):
        nonlocal checks
        checks += 1
        if not condition:
            raise AssertionError("Navigation self-test: " + label)

    with tempfile.TemporaryDirectory(prefix="telemetry-navigation-") as temporary:
        root = Path(temporary)
        (root / "guide").mkdir()
        target = root / "guide" / "Types (v3).md"
        target.write_text("# Українські типи\n# Repeat\n# Repeat\n# Repeat-1\n"
                          "# Repeat\n# `Codec<T>` та [API](other.md)\n"
                          "Setext heading\n==============\n<a id=\"stable\"></a>\n"
                          "```md\n# Not a heading\n[not a link](missing.md)\n```\n",
                          encoding="utf-8")
        source = root / "README.md"
        source.write_text("[unicode](guide/Types%20%28v3%29.md#українські-типи)\n"
                          "[duplicate](<guide/Types (v3).md#repeat-2>)\n"
                          "[rendered](guide/Types%20%28v3%29.md#codect-та-api)\n"
                          "[setext][types]\n[explicit](guide/Types%20%28v3%29.md#stable)\n"
                          "[types]: guide/Types%20%28v3%29.md#setext-heading \"title\"\n"
                          "`[not a link](missing.md)`\n"
                          "<!-- [comment](missing.md) -->\n"
                          "[remote](https://example.test/missing#not-checked)\n",
                          encoding="utf-8")
        report = check_documents(root, [source])
        require(report["completed"], "relative, encoded, Unicode, reference and explicit links: " +
                str(report["errors"]))
        require(report["local_links"] == 5, "count actual local links")
        require(report["heading_links"] == 5, "count actual heading links")
        require(report["external_links_skipped"] == 1, "external links do not require network")
        ids = anchors(target.read_text(encoding="utf-8"))
        require({"repeat", "repeat-1", "repeat-1-1", "repeat-2"} <= ids,
                "duplicate IDs use global collision tracking")
        # The third Repeat collides with both Repeat and Repeat-1; Repeat-2 is next.
        source.write_text("[duplicate](guide/Types%20%28v3%29.md#repeat-2)\n", encoding="utf-8")
        require(check_documents(root, [source])["completed"], "duplicate heading destination")
        require("not-a-heading" not in ids, "fenced headings are ignored")
        source.write_text("[missing](guide/absent.md)\n", encoding="utf-8")
        errors = check_documents(root, [source])["errors"]
        require(len(errors) == 1 and "missing file" in errors[0], "missing file fails")
        source.write_text("[missing](guide/Types%20%28v3%29.md#absent)\n", encoding="utf-8")
        errors = check_documents(root, [source])["errors"]
        require(len(errors) == 1 and "missing heading anchor" in errors[0], "missing anchor fails")
        source.write_text("![image](<guide/Types (v3).md>)\n", encoding="utf-8")
        require(check_documents(root, [source])["completed"], "image destination exists")
        require(github_slug("**A &amp; B** / `readAs<T>()`") == "a--b--readast",
                "rendered entity and code punctuation")
        require(github_slug("_Word_ / `some_value` / `__literal__` / user_id / \\_escaped\\_") ==
                "word--some_value--__literal__--user_id--_escaped_",
                "emphasis differs from literal code, word-internal and escaped underscores")
    print(f"Documentation navigation self-tests: {checks} passed")
    return checks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT, help="Git checkout root")
    parser.add_argument("--summary", type=Path, help="Optional generated JSON report path")
    parser.add_argument("--self-test", action="store_true", help="Run parser positive/negative controls")
    args = parser.parse_args()
    controls = self_test() if args.self_test else 0
    root = args.root.resolve()
    report = check_documents(root, authored_documents(root))
    report["self_tests"] = controls
    if args.summary:
        args.summary.parent.mkdir(parents=True, exist_ok=True)
        args.summary.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    for error in report["errors"]:
        print(error)
    print(f"Documentation navigation: {report['documents']} authored Markdown files, "
          f"{report['local_links']} local links, {report['heading_links']} heading links, "
          f"{len(report['errors'])} errors")
    raise SystemExit(not report["completed"])


if __name__ == "__main__":
    main()
