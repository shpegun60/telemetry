#!/usr/bin/env python3
"""Optional metadata: frozen goldens, independent runtime graph and actual consumers. MIT."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'tests/codegen'))
from test_core import descriptor, golden
from tools.telemetry_codegen import load_schema
from tools.telemetry_codegen.cli import render, publish


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inputs():
    paths = [p for directory in ('tools/telemetry_codegen', 'tests/sdk_metadata',
                                'tests/codegen', 'tests/structured/descriptor')
             for p in (ROOT / directory).rglob('*') if p.is_file()
             and p.suffix in ('.py', '.cpp', '.ts', '.mjs', '.json', '.hex')
             and '__pycache__' not in p.parts]
    paths += [ROOT / 'web/telemetry.js']
    return {p.relative_to(ROOT).as_posix(): digest(p) for p in sorted(paths)}


def edge():
    return descriptor((
        golden.struct_type(12, 0, []),
        golden.type_record(13, 4, 0, golden.u32(12, 65536)),
        golden.enum_type(14, 9, 8, [(-(1 << 63), 'Мінімум'), (-1, 'negative'),
                                    (0, 'zero'), ((1 << 63) - 1, 'maximum')]),
        golden.enum_type(15, 8, 8, [(0, '__proto__'), ((1 << 64) - 1, 'quote"\\\n')]),
        golden.struct_type(16, 16, [(12, 'empty'), (13, 'zeroArray'),
                                   (14, '__proto__'), (15, 'quote"\\\n')]),
        golden.enum_type(17, 3, 1, [(-128, 'low'), (-1, 'negative'), (0, 'zero'), (127, 'high')]),
    ), groups=(
        (('metadata', ((16, 'Config', 3), (12, 'Empty'), (17, 'Mode'))),
         ('порожній', ()), ('extra', ((15, 'Wide'),))),
        (('metadata', ((16, 'Configure'), (0, 'Reset'))),),
        (('metadata', ((16, 'Echo', 16), (0, 'Ping', 0))),)))


def flattened(data):
    """Native record traversal projection, checked against the independent JS parser."""
    rows, members, enums = [], [], []
    encode = lambda value: value.encode('utf8').hex()
    for row in data['types']:
        first_member, first_enum = len(members), len(enums)
        for member in row.get('members', ()):
            members.append(['M', row['id'], member['typeId'], member['wireOffset'],
                            encode(member['name']), encode(member['cppName']), ''])
        for entry in row.get('entries', ()):
            enums.append(['E', row['id'], int(entry['code']) % (1 << 64), encode(entry['name']), ''])
        rows.append(['T', row['id'], row['kind'], row.get('scalarCode', 0), row['wireBytes'],
                     row.get('underlyingTypeId', row.get('elementTypeId', 0)), row.get('elementCount', 0),
                     first_member, len(members) - first_member, first_enum, len(enums) - first_enum])
    rows += members + enums
    for row in data['catalogs']:
        rows.append(['C', row['category'], row['group'], row['firstInCategory'], row['count'],
                     encode(row['name']), encode(row['symbol']), ''])
    for row in data['endpoints']:
        rows.append(['P', row['category'], row['id'], row['catalogIndex'], row['valueType'],
                     row['requestType'], row['responseType'], row['capabilities'],
                     *[encode(row[key]) for key in ('catalogName', 'name', 'groupSymbol', 'symbol')], ''])
    return '\n'.join('\t'.join(map(str, row)) for row in rows) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--node', required=True)
    parser.add_argument('--tsc', required=True)
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--arm-cxx', help='Optional Cortex-M7 compiler for selected linked roots')
    parser.add_argument('--arm-nm', default='arm-none-eabi-nm')
    parser.add_argument('--arm-objdump', default='arm-none-eabi-objdump')
    args = parser.parse_args()
    out = args.build_dir.resolve()
    if out == ROOT or ROOT in out.parents or out in ROOT.parents or (out.exists() and any(out.iterdir())):
        parser.error('Use a fresh external directory; nothing is deleted')
    out.mkdir(parents=True, exist_ok=True)
    (out / 'package.json').write_text('{"type":"module"}\n')
    report = dict(passed=False, hardware=False, started_utc=datetime.now(timezone.utc).isoformat(),
                  source_sha256=inputs(), commands=[], checks=0)

    def check(condition, label):
        report['checks'] += 1
        if not condition:
            raise RuntimeError(label)

    def run(label, command):
        result = subprocess.run(list(map(str, command)), cwd=ROOT, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=300,
                                env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1',
                                         ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                                         UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
        log = out / (label + '.log')
        log.write_text(result.stdout, encoding='utf8')
        report['commands'].append(dict(name=label, command=list(map(str, command)),
                                       returncode=result.returncode, log=log.name, sha256=digest(log)))
        check(result.returncode == 0, label + ':\n' + result.stdout)
        print(label + ': passed', flush=True)
        return result.stdout

    try:
        run('compiler', [args.cxx, '--version'])
        run('node', [args.node, '--version'])
        run('typescript', [args.node, args.tsc, '--version'])
        fixtures = {name: golden.fixture(name) for name in ('empty', 'mixed', 'edge')}
        fixtures['metadata-edge'] = edge()
        for name, data in fixtures.items():
            directory = out / name
            directory.mkdir()
            binary = directory / 'descriptor.bin'
            binary.write_bytes(data)
            schema = load_schema(data, 'edge_api')
            check(schema.descriptor_header == data[:64], name + ' exact original header')
            plain = render(schema, ('cpp', 'js', 'ts', 'qml'))
            optional = render(schema, ('cpp', 'js', 'ts', 'qml'), structural_metadata=True)
            for path, content in plain.items():
                if path != 'manifest.json':
                    check(optional[path] == content, name + ' ordinary output unchanged: ' + path)
            old_manifest, new_manifest = [json.loads(files['manifest.json']) for files in (plain, optional)]
            check('structural_metadata' not in old_manifest['options'], 'No default manifest opt-in')
            check(new_manifest['options']['structural_metadata'] is True, 'Explicit manifest opt-in')
            generated = directory / 'generated'
            publish(optional, generated)
            check(publish(optional, generated, check=True) is None, 'Owned output --check')
            md_path = generated / 'metadata/schema.json'
            md = json.loads(md_path.read_text(encoding='utf8'))
            check(md['descriptorSha256'] == hashlib.sha256(data).hexdigest(), 'Descriptor SHA256')
            run(name + '-ts', [args.node, args.tsc, generated / 'ts/device.metadata.ts', '--strict',
                               '--target', 'ES2022', '--module', 'ES2022', '--outDir', directory / 'compiled'])
            for label, module in [('js', generated / 'js/device.metadata.js'),
                                  ('ts', directory / 'compiled/device.metadata.js')]:
                run(name + '-' + label, [args.node, HERE / 'Check.mjs', module, md_path, binary,
                                         ROOT / 'web/telemetry.js'])
            native = directory / ('dump.exe' if os.name == 'nt' else 'dump')
            flags = ['-std=c++20', '-O1' if args.sanitize else '-O2', '-Wall', '-Wextra', '-Werror']
            if args.sanitize:
                flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            run(name + '-compile', [args.cxx, *flags, '-I' + str(generated / 'cpp'), HERE / 'Dump.cpp', '-o', native])
            actual = run(name + '-native', [native])
            check(actual == flattened(md), name + ' every C++ record, full UTF8 and enum bits')
            report.setdefault('executables', {})[name] = dict(path=str(native.relative_to(out)), sha256=digest(native))
            standalone = directory / ('standalone.exe' if os.name == 'nt' else 'standalone')
            run(name + '-standalone-compile', [args.cxx, *flags, '-I' + str(generated / 'cpp'),
                                             HERE / 'Standalone.cpp', '-o', standalone])
            run(name + '-standalone-run', [standalone])
            if name == 'metadata-edge':
                shutil.copy2(HERE / 'Consumer.ts', directory / 'Consumer.ts')
                run('strict-use-site', [args.node, args.tsc, directory / 'Consumer.ts', '--strict',
                                        '--noUnusedLocals', '--target', 'ES2022', '--module', 'ES2022', '--noEmit'])
                check(md['types'][13]['wireBytes'] == 0 and md['types'][13]['elementCount'] == 65536,
                      'Large empty array retains count')
                check(md['types'][14]['entries'][0]['code'] == '-9223372036854775808', 'S64 min exact')
                check(md['types'][15]['entries'][-1]['code'] == '18446744073709551615', 'U64 max exact')
                check(any(ep['id'] == 131072 for ep in md['endpoints']), 'Packed ID above16bits')
                if args.arm_cxx:
                    run('arm-compiler', [args.arm_cxx, '--version'])
                    for opt in ('Os', 'O2'):
                        elf = directory / ('metadata-' + opt + '.elf')
                        run('arm-' + opt, [args.arm_cxx, '-std=c++20', '-' + opt, '-mcpu=cortex-m7',
                            '-mthumb', '-ffreestanding', '-fno-exceptions', '-fno-rtti',
                            '-fno-threadsafe-statics', '-ffunction-sections', '-fdata-sections',
                            '-fstack-usage', '-DTELEMETRY_METADATA_ARM_PROBE',
                            '-I' + str(generated / 'cpp'), HERE / 'Standalone.cpp', '-nostdlib',
                            '-Wl,--gc-sections,-e,metadata_probe,-Map=' + str(elf.with_suffix('.map')),
                            '-o', elf])
                        symbols = run('arm-' + opt + '-symbols', [args.arm_nm, '-S', '-C', elf])
                        undefined = run('arm-' + opt + '-undefined', [args.arm_nm, '-u', elf])
                        run('arm-' + opt + '-disassembly', [args.arm_objdump, '-drwC', elf])
                        check(not undefined.strip(), 'No unresolved ARM references')
                        check(not re.search(r'malloc|calloc|realloc|operator new|__cxa_guard|atexit', symbols),
                              'No heap or initialization guard in linked metadata root')
                        report.setdefault('arm', {})[opt] = dict(sha256=digest(elf), size=elf.stat().st_size)
            # Explicit opt-out removes only formerly owned metadata, preserving user files.
            (generated / 'user.txt').write_text('keep\n')
            publish(plain, generated)
            check(not md_path.exists(), 'Opt-out removes owned metadata')
            check((generated / 'user.txt').read_text() == 'keep\n', 'Unowned file preserved')
        report['source_sha256_after'] = inputs()
        check(report['source_sha256'] == report['source_sha256_after'], 'Immutable qualification inputs')
        report['passed'] = True
    finally:
        report['finished_utc'] = datetime.now(timezone.utc).isoformat()
        (out / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')


if __name__ == '__main__':
    main()
