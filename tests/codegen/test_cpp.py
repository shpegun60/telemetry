"""Reusable standalone C++ generator fixtures and deterministic-output checks.

The runner compiles Standalone.cpp against write_focused() outputs both without
Telemetry and with its optional enum/typed-RPC interoperability headers.
Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""

from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(Path(__file__).parent))
from test_core import descriptor, golden
from tools.telemetry_codegen import load_schema
from tools.telemetry_codegen.generators.cpp import generate


def focused_schema():
    extra = (
        golden.enum_type(12, 8, 8, [(0, "Zero"), ((1 << 64) - 1, "Max")]),
        golden.enum_type(13, 9, 8, [(-(1 << 63), "Min"), ((1 << 63) - 1, "Max")]),
        golden.type_record(14, 4, 12, golden.u32(10, 3)),
        golden.struct_type(15, 0, []),
        golden.type_record(16, 4, 0, golden.u32(15, 0)),
        golden.struct_type(17, 57, [(8, "unsignedValue"), (9, "signedValue"), (1, "valid"),
            (10, "single"), (11, "real"), (12, "code"), (13, "signedCode"),
            (14, "samples"), (15, "empty"), (16, "zero")]),
        golden.type_record(18, 4, 10000, golden.u32(2, 10000)),
        golden.struct_type(19, 10001, [(18, "Type_2"), (1, "valid")]),
        golden.enum_type(20, 7, 4, []),
    )
    groups = ((("motor", [(17, "Config", 3), (15, "Empty", 3), (20, "Sparse", 1)]),),
              (("motor", [(0, "Reset"), (17, "Apply"), (19, "Large")]),),
              (("motor", [(0, "Read", 17), (17, "Echo", 17), (17, "Store", 0), (0, "Noop", 0)]),))
    return load_schema(descriptor(extra, groups), "sdk::device")


def write_focused(output):
    """Write source headers to an explicitly chosen external artifact directory."""
    output = Path(output)
    for name, content in generate(focused_schema()).items():
        target = output / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(content, encoding="utf-8", newline="\n")
    return output


class CppTests(unittest.TestCase):
    def test_deterministic_standalone_and_optional_outputs(self):
        schema = focused_schema()
        first = generate(schema)
        self.assertEqual(first, generate(schema))
        self.assertEqual(len(first), 7)
        for name in ("Types", "Schema", "Endpoints", "Codec"):
            text = first[f"cpp/sdk/device/{name}.hpp"]
            self.assertNotIn("#include <telemetry/", text)
            self.assertNotIn("#include <resource/", text)
        self.assertIn('namespace sdk::device', first["cpp/sdk/device/Types.hpp"])
        self.assertIn("(-9223372036854775807LL - 1)", first["cpp/sdk/device/Types.hpp"])
        self.assertIn("18446744073709551615ULL", first["cpp/sdk/device/Types.hpp"])
        self.assertIn("Type_18 Type_2_value;", first["cpp/sdk/device/Types.hpp"])

    def test_explicit_enum_interoperability_and_operation_metadata(self):
        files = generate(focused_schema())
        enums = files["cpp/sdk/device/TelemetryBindings.hpp"]
        self.assertIn("enumEntries<::sdk::device::detail::Type_20>()", enums)
        self.assertIn("Type_12::Max", enums)
        self.assertIn("Type_13::Min", enums)
        endpoints = files["cpp/sdk/device/Endpoints.hpp"]
        self.assertIn("FieldRead = 4", endpoints)
        self.assertIn("writeOperation = Operation::FieldWrite", endpoints)
        self.assertIn("TrustedRpcMapping trust", files["cpp/sdk/device/RpcBindings.hpp"])


if __name__ == "__main__":
    unittest.main()
