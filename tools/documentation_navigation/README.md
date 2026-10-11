# Documentation navigation checks

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.

[`check.py`](check.py) checks Markdown links to local files/directories and
GitHub-style heading anchors before documentation changes reach readers.
It needs only Python 3 and Git; it makes no network requests.

```sh
python tools/documentation_navigation/check.py --self-test
python tools/documentation_navigation/check.py --self-test --summary build/docs-navigation/summary.json
```

The same first command runs in the [source-style CI job](../../.github/workflows/ci.yml).
Run it from any directory: the script resolves the checkout from its own path.
Use `--root PATH` to inspect another Git checkout. The optional JSON report
records actual document/local-link/heading-link counts, diagnostic locations,
self-test count and completion state. Generated reports belong under an
explicit build/output directory; the checker never edits a document.

## What is checked

Git's tracked and nonignored new files define the source inventory. Only
authored `.md` files are inspected, so downloaded dependencies or generated
build documentation cannot silently expand the scope. Vendor trees
`lib/boost_pfr`, `lib/magic_enum` and `lib/delegate`, and sealed
`doc/evidence` records are excluded as source documents. They remain valid
link targets, including their heading anchors; retained bytes are unchanged.

Inline links, linked images, reference links and HTML `href`/`src` attributes
are checked. Paths resolve from the referring document; `/path` resolves from
the checkout root. Encoded path/fragment bytes, angle-bracket destinations,
balanced parentheses, optional link titles and CRLF text are supported.
Fenced/indented code, inline code and HTML comments do not contribute links.

Heading checks support ATX and setext headings, rendered link/code text,
Unicode names, duplicate heading suffixes, and explicit HTML `id`/`name`
anchors. An explicit anchor is useful when an index is reorganized but an
older qualification document still links to its previous heading.
Missing files and missing Markdown anchors produce an error containing the
referring file, one-based line and destination, and return a nonzero exit.

This is a repository navigation check, not a complete CommonMark renderer.
Use ordinary Markdown destinations and headings, as the guides do.
External URLs (including immutable GitHub commit URLs) are counted but not
requested or authenticated. Non-Markdown fragments, such as source-code
`#L120` links, are not treated as heading IDs. Compile/run checks for linked
C++ programs remain in [`tests/docs`](../../tests/docs/README.md).

## Controls

`--self-test` creates temporary documents and verifies positive local,
percent-encoded, Unicode, duplicate-heading, rendered-markup, setext,
reference, explicit-anchor and image destinations. It also requires failure
for a missing file and a missing heading. Fenced/inline code and comment
controls ensure examples containing apparent links are ignored.
No fixture or generated file is left in the checkout.
