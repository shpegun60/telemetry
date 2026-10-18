"""QML naming/ownership and unchanged standalone SDK byte regressions.

This suite checks publication and identities independently of Qt availability;
the adjacent CMake/runtime suite executes the generated module in Qt itself.
Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""

import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT), str(ROOT / "tests/codegen")]
from test_core import descriptor, golden
from tools.telemetry_codegen import load_schema
from tools.telemetry_codegen.cli import publish, render
from tools.telemetry_codegen.generators import qml


class QmlGeneratorTests(unittest.TestCase):
    def test_existing_sdk_is_byte_identical(self):
        schema = load_schema(golden.fixture("mixed"))
        old = render(schema, ("cpp", "js", "ts"))
        combined = render(schema, ("cpp", "js", "ts", "qml"))
        for path, source in old.items():
            if path != "manifest.json":
                self.assertEqual(source, combined[path], path)
        only = render(schema, ("qml",))
        self.assertEqual({p for p in only if p.startswith("cpp/")},
                         {p for p in old if p.startswith("cpp/")})
        for path in only:
            if path.startswith("cpp/"):
                self.assertEqual(only[path], old[path])
        manifest = json.loads(only["manifest.json"])
        self.assertEqual(manifest["options"]["targets"], ["qml"])
        for path, digest in manifest["generated_files"].items():
            self.assertEqual(digest, hashlib.sha256(only[path]).hexdigest())
        self.assertIn("qml", manifest["generated_symbol_mapping"])

    def test_full_scope_names_and_nesting(self):
        names = ["Voltage", "voltage", "voltageChanged", "refreshVoltage", "m_device",
                 "objectName", "signals", "slots", "property", "μ", "__proto__", "1x"]
        groups = ((("device", [(2, name, 3) for name in names]),
                   ("m_pending", [(2, "ready")]), ("pending", [(2, "session")])),
                  (("device", [(12, "Voltage"), (0, "resetSession")]),),
                  (("device", [(12, "Voltage", 12), (0, "refreshVoltage", 0)]),))
        schema = load_schema(descriptor([golden.struct_type(12, 0, [])], groups), "company::device_api")
        mapped = qml.layout(schema)
        self.assertEqual(len({g["symbol"] for g in mapped}), len(mapped))
        occupied = set(qml.GROUP_RESERVED)
        for group in mapped:
            self.assertFalse({group["symbol"], "m_" + group["symbol"]} & occupied)
            occupied.update((group["symbol"], "m_" + group["symbol"]))
            family = set(qml.RESERVED)
            for ep, stem in group["endpoints"]:
                names = qml._family(stem, ep.category)
                self.assertFalse(names & family, (stem, names & family))
                family.update(names)
        files = render(schema, ("qml",))
        self.assertIn(b"namespace company::device_api::qml", files["qml/DeviceApi.hpp"])
        self.assertIn(b"URI Telemetry.Generated.company.device_api", files["qml/CMakeLists.txt"])
        mapping = json.loads(files["manifest.json"])["generated_symbol_mapping"]["qml"]
        self.assertEqual(mapping["groups"][0]["endpoints"][0]["original_name"], "Voltage")

    def test_empty_schema_and_case_distinct_guards(self):
        data = golden.fixture("empty")
        first = render(load_schema(data, "lower"), ("qml",))
        second = render(load_schema(data, "Lower"), ("qml",))
        for path in ("qml/DeviceApi.hpp", "qml/QmlValues.hpp", "qml/QmlInput.hpp"):
            guards = [next(line for line in files[path].splitlines() if line.startswith(b"#ifndef"))
                      for files in (first, second)]
            self.assertNotEqual(*guards, path)
        self.assertNotIn(b"Group_0Api", first["qml/DeviceApi.hpp"])

    def test_guarded_publish_and_target_removal(self):
        schema = load_schema(golden.fixture("mixed"))
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "sdk"
            publish(render(schema, ("qml",)), path)
            publish(render(schema, ("qml",)), path, check=True)
            (path / "application.txt").write_text("owned by application")
            publish(render(schema, ("cpp",)), path)
            self.assertFalse((path / "qml/DeviceApi.hpp").exists())
            self.assertEqual((path / "application.txt").read_text(), "owned by application")
            publish(render(schema, ("qml",)), path)
            changed = path / "qml/DeviceApi.hpp"
            changed.write_bytes(changed.read_bytes() + b"// local edit\n")
            with self.assertRaisesRegex(ValueError, "modified generated"):
                publish(render(schema, ("qml",)), path)

    def test_qt_specific_namespace_profile(self):
        data = golden.fixture("empty")
        for name in ("QObject", "company::QString", "Q_OBJECT", "QML_ELEMENT", "qint64", "qApp"):
            schema = load_schema(data, name)
            self.assertIn("manifest.json", render(schema, ("cpp",)))
            with self.assertRaisesRegex(ValueError, "QML namespace conflicts"):
                render(schema, ("qml",))


if __name__ == "__main__":
    unittest.main()
