# Complete Telemetry code generator

`telemetry-codegen` is a module-free compatibility distribution. It installs
exactly matching `telemetry-codegen-core` and `telemetry-ux-codegen` packages.
The core owns the sole `telemetry-codegen` command and
`tools.telemetry_codegen` Python modules; the UX extension adds `--targets qml`.
No Qt Python runtime is required to generate files.

Build this distribution from an external copy of this directory. It is
self-contained, including its version and license. Build the core and UX wheels
from their respective source archives, then install the three wheels together
in a fresh environment:

```sh
python3 -m venv /external/codegen-env
/external/codegen-env/bin/python -m pip install --no-index \
  --find-links /external/packages telemetry-codegen==0.1.0
/external/codegen-env/bin/telemetry-codegen generate descriptor.bin \
  --targets qml --output /external/generated
```

An older monolithic `telemetry-codegen` distribution owns the same Python
modules and command that the new core owns. This is not a transparent in-place
upgrade: use a fresh environment, or explicitly uninstall the old distribution
before installing the new packages. Do not install the new core alongside the
old module owner and then uninstall the old owner.

Headless applications may install only `telemetry-codegen-core==0.1.0`.
Without the UX extension, selecting `qml` gives an actionable installation error;
the default C++/JS/TS outputs remain available. These local development packages
have not been published to a registry.
