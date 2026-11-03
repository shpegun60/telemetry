"""Focused optional web-starter hook and guarded-publication controls (MIT).

Run using a selected installed core interpreter (or an explicitly supplied source
PYTHONPATH), with --descriptor and a fresh external --output-dir. No UX package,
Qt, compiler, network or SDK checkout fallback is required by these controls.
"""
import argparse
from contextlib import redirect_stderr, redirect_stdout
import hashlib
import io
import json
from pathlib import Path
import types
from unittest.mock import patch

from tools.telemetry_codegen import cli
from tools.telemetry_codegen.emitter_api import API_VERSION, IR_VERSION, SDK_VERSION, GeneratedTarget
from tools.telemetry_codegen.schema import load_schema


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--descriptor', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    output = args.output_dir.resolve()
    for source in (Path(__file__).resolve().parents[2], Path(cli.__file__).resolve().parents[2]):
        if output == source or source in output.parents or output in source.parents:
            parser.error('Output must be separate from test sources and the selected core installation')
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        parser.error('Use a fresh empty external output directory; nothing is deleted')
    output.mkdir(parents=True, exist_ok=True)
    schema = load_schema(args.descriptor.read_bytes(), 'device_api')
    checks = 0
    report = dict(passed=False, core_module=cli.__file__, checks=0)

    def check(condition, reason):
        nonlocal checks
        checks += 1
        if not condition:
            raise AssertionError(reason)

    def inventory(path):
        return {file.relative_to(path).as_posix(): hashlib.sha256(file.read_bytes()).hexdigest()
                for file in path.rglob('*') if file.is_file()}

    def extension(**kwargs):
        module = types.ModuleType('telemetry_ux_codegen')
        module.API_VERSION = API_VERSION
        module.IR_VERSION = IR_VERSION
        module.SDK_VERSION = SDK_VERSION
        module.generate_web_starter = lambda schema: GeneratedTarget(
            {'ui/DevicePage.js': '// generated mount\n', 'ui/DevicePage.d.ts': '// typed mount\n'}, {'mount': 'mountDevice'})
        for key, value in kwargs.items():
            setattr(module, key, value)
        return module

    original_import = cli.importlib.import_module
    loads = []
    candidate = extension()

    def import_selected(name, *positional, **keywords):
        if name == 'telemetry_ux_codegen':
            loads.append(name)
            if isinstance(candidate, Exception):
                raise candidate
            return candidate
        return original_import(name, *positional, **keywords)

    def invoke(name, targets='js', metadata=True):
        destination = output / name
        destination.mkdir()
        (destination / 'application.txt').write_bytes(b'unchanged\x00\xff')
        before = inventory(destination)
        arguments = ['generate', str(args.descriptor), '--targets', targets,
                     '--ux-starter', 'web', '--output', str(destination)]
        if metadata:
            arguments.append('--structural-metadata')
        stream = io.StringIO()
        with redirect_stdout(stream), redirect_stderr(stream):
            status = cli.main(arguments)
        return status, stream.getvalue(), destination, before

    try:
        with patch.object(cli.importlib, 'import_module', import_selected):
            # An installed UX module is not even looked up by headless generation.
            candidate = ModuleNotFoundError('not installed', name='telemetry_ux_codegen')
            for selected in ('js', 'cpp,js,ts'):
                default = cli.render(schema, selected)
                check(default == cli.render(schema, selected, ux_starter=None), 'Default bytes remain unchanged')
                check('ux_starter' not in json.loads(default['manifest.json'])['options'], 'Default manifest has no new option')
            check(not loads, 'Default generation never resolves UX')
            for name, targets, metadata in [('no-js', 'ts', True), ('no-metadata', 'js', False)]:
                status, diagnostic, destination, before = invoke(name, targets, metadata)
                check(status == 1 and 'requires an explicit js target' in diagnostic, 'Prerequisite diagnostic')
                check(inventory(destination) == before, 'Prerequisite failure does not publish')
            check(not loads, 'Prerequisite failures do not resolve UX')
            for bad in ('html', True, 42):
                try:
                    cli.render(schema, 'js', structural_metadata=True, ux_starter=bad)
                except ValueError as error:
                    check('--ux-starter must be web' in str(error), 'Unknown public render option refused')
                else:
                    check(False, 'Unknown option accepted')

            faults = [
                ('missing', candidate, 'telemetry-ux-codegen'),
                ('nested-import', ModuleNotFoundError('missing internal dependency', name='ux_internal_dependency'), 'Cannot import installed Telemetry UX'),
                ('api', extension(API_VERSION=99), 'API_VERSION'),
                ('api-bool', extension(API_VERSION=True), 'API_VERSION'),
                ('ir', extension(IR_VERSION=99), 'IR_VERSION'),
                ('sdk', extension(SDK_VERSION='9.9.9'), 'SDK_VERSION'),
                ('missing-method', extension(generate_web_starter=None), 'generate_web_starter'),
                ('wrong-target', extension(generate_web_starter=lambda _: {}), 'GeneratedTarget'),
                ('wrong-files', extension(generate_web_starter=lambda _: GeneratedTarget([], {})), 'GeneratedTarget'),
                ('wrong-symbols', extension(generate_web_starter=lambda _: GeneratedTarget({}, [])), 'symbol_mapping'),
                ('nan', extension(generate_web_starter=lambda _: GeneratedTarget({}, {'bad': float('nan')})), 'Out of range'),
                ('infinity', extension(generate_web_starter=lambda _: GeneratedTarget({}, {'bad': float('inf')})), 'Out of range'),
                ('escape', extension(generate_web_starter=lambda _: GeneratedTarget({'../escape.js': 'bad'}, {})), 'output path'),
                ('manifest', extension(generate_web_starter=lambda _: GeneratedTarget({'manifest.json': 'bad'}, {})), 'output path'),
                ('collision', extension(generate_web_starter=lambda _: GeneratedTarget({'JS/DEVICE.JS': 'bad'}, {})), 'colliding'),
                ('binary', extension(generate_web_starter=lambda _: GeneratedTarget({'ui/Page.js': b'bad'}, {})), 'must be text')]
            fresh = output / 'fresh-missing'
            with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                status = cli.main(['generate', str(args.descriptor), '--targets', 'js',
                    '--structural-metadata', '--ux-starter', 'web', '--output', str(fresh)])
            check(status == 1 and not fresh.exists(), 'Missing optional extension creates no output directory')
            for name, candidate, expected in faults:
                status, diagnostic, destination, before = invoke('fault-' + name)
                check(status == 1, 'Fault returns failure: ' + name)
                check(expected.casefold() in diagnostic.casefold(), 'Specific diagnostic: ' + name)
                check(inventory(destination) == before, 'No partial publication: ' + name)

            from types import MappingProxyType
            calls = []
            candidate = extension(generate_web_starter=lambda current: (
                calls.append(('web', current)) or GeneratedTarget(MappingProxyType(
                    {'ui/DevicePage.js': '// mount\n', 'ui/DevicePage.d.ts': '// types\n'}), {'mount': 'mountDevice'})),
                generate_qml=lambda current: (calls.append(('qml', current)) or GeneratedTarget({'qml/Probe.hpp': '// qml\n'}, {'probe': True})))
            loads.clear()
            files = cli.render(schema, 'qml,js', structural_metadata=True, ux_starter='web')
            check(len(loads) == 1, 'Combined selected emitters load the one extension once')
            check([kind for kind, _ in calls] == ['qml', 'web'], 'Each selected emitter runs once')
            check(all(current is schema for _, current in calls), 'Emitters consume the same validated Schema')
            check(files['ui/DevicePage.js'] == b'// mount\n', 'Mapping output accepted')
            check(any(name.startswith('cpp/') for name in files), 'QML automatic C++ dependency remains')
            manifest = json.loads(files['manifest.json'])
            check(manifest['options']['ux_starter'] == 'web', 'Opt-in receipt option')
            check(manifest['generated_symbol_mapping']['web_starter'] == {'mount': 'mountDevice'}, 'Starter symbol receipt')
            check(manifest['generated_symbol_mapping']['qml'] == {'probe': True}, 'Legacy QML symbol receipt')
            check(manifest['generated_files']['ui/DevicePage.js'] == hashlib.sha256(files['ui/DevicePage.js']).hexdigest(), 'Starter output ownership hash')
            status, diagnostic, positive, before = invoke('positive')
            check(status == 0, 'Positive CLI flag invokes the selected extension')
            check((positive / 'application.txt').read_bytes() == b'unchanged\x00\xff', 'Positive publication preserves unowned application data')
            check(json.loads((positive / 'manifest.json').read_text())['options']['ux_starter'] == 'web', 'Actual CLI receipt includes the opt-in')
            destination = output / 'published'
            cli.publish(files, destination)
            cli.publish(files, destination, check=True)
            check((destination / 'ui/DevicePage.js').read_bytes() == b'// mount\n', 'Actual guarded publication')
            (destination / 'ui/DevicePage.js').write_text('// user changes\n')
            before = inventory(destination)
            try:
                cli.publish(files, destination)
            except ValueError:
                check(inventory(destination) == before, 'Regeneration preserves modified starter and all owned output')
            else:
                check(False, 'Modified generated starter overwritten')
        report['passed'] = True
    finally:
        report['checks'] = checks
        (output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
