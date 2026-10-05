# Source style

The authored C++ code uses the root [`.clang-format`](../../.clang-format):
four-column tabs, braces on the next line for function bodies, readable
control flow, no forced one-line methods, and preserved explanatory comments.
Formatting never reorders includes or rewrites wire behavior.

Every maintained source starts with its purpose and relevant contract. Above
classes, short public-method summaries explain what the user can do. Comments
explain ownership, state, compile-time selection and byte contracts; they do
not merely repeat each statement.

Every authored `.h`/`.hpp` uses both a unique include guard and `#pragma once`.
Check from the repository root:

```sh
python tools/source_style/check.py
python tools/source_style/check.py --fix-guards
```

The checker inventories tracked files and new nonignored files. It excludes
original vendor trees and sealed `doc/evidence`, `tests/**/evidence` and
`tests/**/h7s` inputs: their original source bytes identify measured evidence.
Those files are historical/current bench inputs, not an exception permitting
undocumented new library code. They retain their existing explanatory comments.
The checker verifies file headers/guard uniqueness; class-summary quality is
reviewed against actual methods, rather than guessed by a C++ regular expression.

Formatting is enforced by the separate **Source comments, guards and pinned
formatting** CI job. It installs clang-format **22.1.8** into a virtual
environment and runs this checker over the same maintained inventory. The
checker rejects any other formatter version. It compares formatter output
with each C++/JavaScript file after normalizing CRLF to LF; it never rewrites
source during this check. Noncanonical tabs, braces, spacing or blank lines
fail CI, while Windows checkout line endings do not.

Run the complete gate locally with the same pinned formatter:

```sh
python3 -m venv /tmp/telemetry-source-style
/tmp/telemetry-source-style/bin/python -m pip install clang-format==22.1.8
python3 tools/source_style/check.py \
  --clang-format /tmp/telemetry-source-style/bin/clang-format --self-test
```

`--self-test` feeds a deliberately malformed C++ function through the real
formatter, then proves that the canonical result is accepted, the original
malformed function is rejected, and canonical CRLF text is accepted. These
controls use the same comparison as the maintained-file gate and do not create
or modify source files. `--self-test` requires `--clang-format`; the original
no-argument command remains the comments/guards-only check.

Use clang-format **22.1.8** for an intentional formatting pass, then compile
the affected examples and run the contract/codegen suites. Negative fixtures
and macros need compiler verification too. Virtual environments, logs and
generated binaries belong outside the source tree during local validation;
CI keeps generated files in its ignored `build` tree.

The 2026-10-04 pass checked 296 authored file-purpose headers and 88 dual
header guards, and formatted 244 C++/JavaScript sources. Library/consumer token
comparison preserved implementation expressions and statements. The only
added preprocessor behavior outside inclusion guards is the Exchange fixture's
three diagnostic-line anchors; [the firmware equivalence proof](../../tests/resources/evidence/README.md)
explains why the measured fixture keeps those constants.

Validation included 19 Linux host suites (17 sanitized), seven Windows GCC
suites, 16 Cortex-M7 offline suites, Qt startup, 3057 JS/C++ interoperability
checks, the real COBS example under sanitizers/big-endian execution, and its
Cortex-M7 O2/Os/Og links. Eight little/big-endian contract executions produced
five byte-identical Descriptor/Values files. Twelve complete retained firmware
images also remained byte-identical. No hardware session was performed for
this comment/style change; compiler codegen and old measured identities remain
separate evidence.
