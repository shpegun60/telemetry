#!/usr/bin/env python3
"""Headless runtime Descriptor client, all-six-operation native parity and lifecycle. MIT."""
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


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def snapshot():
    roots = ('lib/telemetry', 'lib/common', 'lib/delegate', 'lib/boost_pfr/include',
             'lib/magic_enum', 'lib/wire', 'lib/integration/encoded',
             'tests/batch_static', 'tests/client_web', 'tests/client_dynamic_web',
             'tools/telemetry_codegen')
    extensions = {'.cpp', '.hpp', '.py', '.ts', '.mjs', '.js', '.json'}
    paths = [path for root in roots for path in sorted((ROOT / root).rglob('*'))
             if path.is_file() and path.suffix in extensions and '__pycache__' not in path.parts]
    paths += [ROOT / 'lib/client/web' / name
              for name in ('Client.ts', 'Client.js', 'Client.d.ts', 'build.py')]
    paths += [ROOT / 'web' / name for name in ('DynamicClient.js', 'DynamicClient.d.ts', 'telemetry.js', 'telemetry.d.ts')]
    return {path.relative_to(ROOT).as_posix(): digest(path) for path in sorted(paths)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--node', required=True)
    parser.add_argument('--tsc', required=True)
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    out = args.build_dir.resolve()
    if out == ROOT or ROOT in out.parents or out in ROOT.parents:
        parser.error('Use an external dedicated output directory')
    if out.exists() and (not out.is_dir() or any(out.iterdir())):
        parser.error('Use a fresh empty output directory; nothing is deleted')
    out.mkdir(parents=True, exist_ok=True)
    (out / 'package.json').write_text('{"type":"module"}\n')
    report = dict(passed=False, hardware=False, sanitize=args.sanitize,
                  started_utc=datetime.now(timezone.utc).isoformat(),
                  source_sha256=snapshot(), commands=[], executions=[], executables={})

    def run(name, command, floor=None):
        command = list(map(str, command))
        result = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=300,
                                env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1',
                                         ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                                         UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
        log = out / (name + '.log')
        log.write_text(result.stdout, encoding='utf8')
        report['commands'].append(dict(name=name, command=command, returncode=result.returncode,
                                       log=log.name, sha256=digest(log)))
        if result.returncode:
            raise RuntimeError(name + ' failed:\n' + result.stdout)
        if floor is not None:
            counts = re.findall(r'checks=(\d+)(?: packets=\d+)? failures=0', result.stdout)
            if len(counts) != 1 or int(counts[0]) < floor:
                raise RuntimeError('Missing maintained check count: ' + name)
            report['executions'].append(dict(name=name, checks=int(counts[0])))
        print(name + ': ' + result.stdout.strip().split('\n')[-1], flush=True)

    try:
        node, tsc = args.node, [args.node, args.tsc]
        run('node-version', [node, '--version'])
        run('typescript-version', [*tsc, '--version'])
        run('compiler-version', [args.cxx, '--version'])
        runtime = out / 'runtime'
        run('single-source-runtime', [sys.executable, ROOT / 'lib/client/web/build.py',
                                     '--node', node, '--tsc', args.tsc, '--output', runtime])
        bridge = out / ('bridge.exe' if os.name == 'nt' else 'bridge')
        flags = ['-std=c++20', '-O1' if args.sanitize else '-O2', '-Wall', '-Wextra', '-Werror',
                 '-Ilib', '-Ilib/boost_pfr/include', '-Ilib/magic_enum']
        if args.sanitize:
            flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        run('compile-bridge', [args.cxx, *flags, HERE / 'Bridge.cpp',
            ROOT / 'lib/integration/encoded/Dispatcher.cpp', ROOT / 'lib/telemetry/encoded/Batch.cpp',
            ROOT / 'lib/integration/encoded/BatchDispatcher.cpp',
            ROOT / 'lib/telemetry/encoded/ReadMany.cpp', ROOT / 'lib/integration/encoded/ReadManyDispatcher.cpp',
            ROOT / 'lib/telemetry/model/Adapter.cpp', ROOT / 'lib/telemetry/abi/StructuredAbi.cpp',
            '-o', bridge])
        report['executables']['bridge'] = dict(path=bridge.name, size=bridge.stat().st_size,
                                               sha256=digest(bridge))
        descriptor, capability, read_many = out / 'descriptor.bin', out / 'tbm1.bin', out / 'trm1.bin'
        run('descriptor', [bridge, '--descriptor', descriptor])
        run('capability', [bridge, '--capability', capability])
        run('read-many-capability', [bridge, '--read-many-capability', read_many])
        sdk = out / 'generated'
        run('generate', [sys.executable, '-m', 'tools.telemetry_codegen', 'generate', descriptor,
                         '--targets', 'js,ts', '--output', sdk])
        run('compile-generated', [*tsc, sdk / 'ts/device.ts', '--strict', '--target', 'ES2022',
                                 '--module', 'ES2022', '--declaration', '--outDir', sdk / 'compiled'])
        for label, core, generated in [('js', ROOT / 'lib/client/web', sdk / 'js'),
                                        ('ts', runtime, sdk / 'compiled')]:
            consumer = out / ('consumer-' + label)
            (consumer / 'lib/client/web').mkdir(parents=True)
            (consumer / 'web').mkdir()
            for name in ('Client.js', 'Client.d.ts'):
                shutil.copy2(core / name, consumer / 'lib/client/web' / name)
            for name in ('DynamicClient.js', 'DynamicClient.d.ts', 'telemetry.js', 'telemetry.d.ts'):
                shutil.copy2(ROOT / 'web' / name, consumer / 'web' / name)
            module = consumer / 'web/DynamicClient.js'
            runtime_module = consumer / 'lib/client/web/Client.js'
            run('checks-' + label, [node, HERE / 'Check.mjs', module, runtime_module,
                descriptor, read_many, capability], 361)
            run('preserving-' + label, [node, HERE / 'Preserving.mjs', module, runtime_module,
                descriptor], 97)
            run('integration-' + label, [node, HERE / 'Integration.mjs', module, runtime_module,
                generated / 'device.js', descriptor, read_many, capability, bridge], 101)
            shutil.copy2(HERE / 'Consumer.ts', consumer / 'Consumer.ts')
            run('strict-consumer-' + label, [*tsc, consumer / 'Consumer.ts', '--strict',
                 '--noUnusedLocals', '--noUnusedParameters', '--target', 'ES2022', '--module',
                 'ES2022', '--noEmit'])
        report['source_sha256_after'] = snapshot()
        if report['source_sha256'] != report['source_sha256_after']:
            raise RuntimeError('Source inputs changed during qualification')
        report['checks'] = sum(row['checks'] for row in report['executions'])
        report['passed'] = True
    finally:
        report['finished_utc'] = datetime.now(timezone.utc).isoformat()
        (out / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')


if __name__ == '__main__':
    main()
