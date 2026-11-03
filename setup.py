"""Build the offline codegen wheel with canonical optional client assets.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""
from pathlib import Path

from setuptools import find_namespace_packages, setup
from setuptools.command.build_py import build_py

ROOT = Path(__file__).resolve().parent
ASSETS = ("Client.ts", "Client.js", "Client.d.ts")


class BuildPy(build_py):
    def run(self):
        super().run()
        destination = Path(self.build_lib) / "tools/telemetry_codegen/_runtime"
        destination.mkdir(parents=True, exist_ok=True)
        for name in ASSETS:
            self.copy_file(str(ROOT / "lib/client/web" / name), str(destination / name))

    def get_outputs(self, include_bytecode=1):
        return super().get_outputs(include_bytecode) + [
            str(Path(self.build_lib) / "tools/telemetry_codegen/_runtime" / name)
            for name in ASSETS]


setup(
    name="telemetry-codegen",
    version=(ROOT / "VERSION").read_text(encoding="ascii").strip(),
    description="Offline Telemetry Descriptor v3 SDK generator",
    python_requires=">=3.10",
    license="MIT",
    packages=find_namespace_packages(include=("tools.telemetry_codegen", "tools.telemetry_codegen.*")),
    cmdclass={"build_py": BuildPy},
    entry_points={"console_scripts": ["telemetry-codegen=tools.telemetry_codegen.cli:main"]},
)
