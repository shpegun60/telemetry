"""Public CLI capability errors occur before publishing any output.

The fake UX module proves hook routing only; it is not a Qt-provider test.
Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""
import contextlib
import io
import json
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from tools.telemetry_codegen import cli
from tools.telemetry_codegen.emitter_api import GeneratedTarget
from tools.telemetry_codegen.schema import load_schema


def check_controls(descriptor, build):
    checks = 0

    def check(value):
        nonlocal checks
        assert value
        checks += 1

    schema = load_schema(descriptor, 'wire_edit')
    calls = []
    def qml_factory(actual):
        check(actual is schema)
        calls.append('qml')
        return GeneratedTarget({'qml/Bindings.hpp': '// optional fixture\n'}, {'fixture': True})
    candidate = SimpleNamespace(generate_qml_configurator=qml_factory)
    with patch.object(cli, '_ux_extension', return_value=candidate):
        result = cli.render(schema, 'qml', structural_metadata=True, ux_starter='qml')
    check(calls == ['qml'])
    check(b'encodePreserving' in result['cpp/wire_edit/Codec.hpp'])
    receipt = json.loads(result['manifest.json'])
    check(receipt['options']['ux_starter'] == 'qml')
    check(receipt['options']['wire_editing'] is True)
    check(receipt['generated_symbol_mapping']['qml'] == {'fixture': True})
    with patch.object(cli, '_ux_extension', side_effect=AssertionError('Unexpected UX import')):
        plain = cli.render(schema, 'cpp')
        check(b'encodePreserving' not in plain['cpp/wire_edit/Codec.hpp'])
        preserving = cli.render(schema, 'cpp', wire_editing=True)
        check(b'encodePreserving' in preserving['cpp/wire_edit/Codec.hpp'])
    # The established web starter does not change separately selected C++ bytes.
    web = SimpleNamespace(generate_web_starter=lambda _: GeneratedTarget({'ui/Page.js': '// fixture\n'}, {}))
    with patch.object(cli, '_ux_extension', return_value=web):
        legacy_web = cli.render(schema, 'cpp,js', structural_metadata=True, ux_starter='web')
    check(legacy_web['cpp/wire_edit/Codec.hpp'] == plain['cpp/wire_edit/Codec.hpp'])
    with patch.object(cli, '_ux_extension', return_value=web):
        explicit_web = cli.render(schema, 'cpp,js', structural_metadata=True, ux_starter='web', wire_editing=True)
    check(explicit_web['cpp/wire_edit/Codec.hpp'] == preserving['cpp/wire_edit/Codec.hpp'])
    descriptor_path = build / 'descriptor.bin'
    for index, (targets, metadata, extension, text) in enumerate((
            ('cpp', True, candidate, 'requires an explicit qml target'),
            ('qml', False, candidate, 'requires an explicit qml target'),
            ('qml', True, SimpleNamespace(), 'generate_qml_configurator'),
            ('qml', True, SimpleNamespace(generate_qml_configurator=lambda _: {}), 'GeneratedTarget'))):
        destination = build / ('refused-' + str(index))
        destination.mkdir()
        sentinel = destination / 'user.txt'
        sentinel.write_bytes(b'User-owned content\n')
        arguments = ['generate', str(descriptor_path), '--targets', targets, '--namespace', 'wire_edit',
                     '--ux-starter', 'qml', '--output', str(destination)]
        if metadata:
            arguments.append('--structural-metadata')
        output = io.StringIO()
        with patch.object(cli, '_ux_extension', return_value=extension), contextlib.redirect_stderr(output):
            status = cli.main(arguments)
        check(status == 1)
        check(text in output.getvalue())
        check(sorted(path.name for path in destination.iterdir()) == ['user.txt'])
        check(sentinel.read_bytes() == b'User-owned content\n')
    print('C++ writer CLI: checks=' + str(checks) + ' failures=0')
    return checks
