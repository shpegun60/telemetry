"""Installed headless CLI must never attempt an optional UX/Qt import. MIT."""
import importlib.abc
from pathlib import Path
import sys

class NoUx(importlib.abc.MetaPathFinder):
    def find_spec(self, fullname, path=None, target=None):
        if fullname.startswith(('telemetry_ux_codegen', 'PySide', 'PyQt')):
            raise AssertionError('Unexpected UX import during headless generation: ' + fullname)
        return None

sys.meta_path.insert(0, NoUx())
import tools.telemetry_codegen as codegen
from tools.telemetry_codegen.cli import main
assert 'site-packages' in codegen.__file__
assert Path(codegen.__file__).resolve().is_relative_to(Path(sys.prefix).resolve())
raise SystemExit(main(sys.argv[1:]))
