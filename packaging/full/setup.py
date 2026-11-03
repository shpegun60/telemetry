"""Module-free compatibility distribution for the complete code generator.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""
from pathlib import Path

from setuptools import setup

ROOT = Path(__file__).resolve().parent
VERSION = (ROOT / "VERSION").read_text(encoding="ascii").strip()

setup(
    name="telemetry-codegen",
    version=VERSION,
    description="Complete Telemetry code generator with the optional UX extension",
    long_description=(ROOT / "README.md").read_text(encoding="utf8"),
    long_description_content_type="text/markdown",
    python_requires=">=3.10",
    license="MIT",
    license_files=("LICENSE",),
    packages=[],
    py_modules=[],
    include_package_data=False,
    install_requires=[
        "telemetry-codegen-core==" + VERSION,
        "telemetry-ux-codegen==" + VERSION,
    ],
)
