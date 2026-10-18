"""Compatibility import for the promoted, sole Python Descriptor parser.

The C++ producer goldens and JavaScript consumer remain independent oracles.
Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""
from pathlib import Path
import sys

_ROOT = Path(__file__).resolve().parents[3]
if str(_ROOT) not in sys.path:
    sys.path.insert(0, str(_ROOT))

from tools.telemetry_codegen.parser import (  # noqa: E402
    InvalidDescriptor, Limits, Reader, fingerprint, parse, require,
)

__all__ = ["InvalidDescriptor", "Limits", "Reader", "fingerprint", "parse", "require"]
