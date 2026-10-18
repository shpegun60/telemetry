"""Offline, transport-independent Telemetry Descriptor v3 SDK generation.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""

__version__ = "0.1.0"

from .schema import Endpoint, Schema, load_schema

__all__ = ["Endpoint", "Schema", "load_schema", "__version__"]
