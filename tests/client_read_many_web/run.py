#!/usr/bin/env python3
"""ReadMany JS/TS, generated tuple API and persistent native Encoded interoperability.

All build outputs are external. No hardware, downloads, or source generation in-place.
Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""
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


def snapshot():
    roots = ('lib/telemetry', 'lib/common', 'lib/delegate', 'lib/boost_pfr/include',
             'lib/magic_enum', 'lib/wire', 'lib/integration/encoded', 'lib/client/web',
             'tests/client_web', 'tests/client_read_many_web', 'tools/telemetry_codegen')
    extensions = {'.cpp', '.hpp', '.py', '.ts', '.mjs', '.js', '.json'}
    return {path.relative_to(ROOT).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
            for root in roots for path in sorted((ROOT / root).rglob('*'))
            if path.is_file() and path.suffix in extensions and '__pycache__' not in path.parts}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True)
    parser.add_argument('--node', required=True)
    parser.add_argument('--tsc', required=True)
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    build = Path(args.build_dir).resolve()
    if build == ROOT or ROOT in build.parents:
        parser.error('use an external build directory')
    build.mkdir(parents=True, exist_ok=True)
    (build / 'package.json').write_text('{"type":"module"}\n')
    report = dict(passed=False, hardware=False, sanitize=args.sanitize,
                  started_at_utc=datetime.now(timezone.utc).isoformat(),
                  source_sha256=snapshot(), commands=[], executions=[])

    def run(name, command, floor=None):
        command = list(map(str, command))
        result = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=300,
                                env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1',
                                         ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                                         UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
        (build / (name + '.log')).write_text(result.stdout, encoding='utf8')
        report['commands'].append(dict(name=name, command=command, returncode=result.returncode))
        if result.returncode:
            raise RuntimeError(name + ' failed:\n' + result.stdout)
        if floor is not None:
            counts = re.findall(r'checks=(\d+)(?: packets=\d+)? failures=0', result.stdout)
            if len(counts) != 1 or int(counts[0]) < floor:
                raise RuntimeError('missing maintained check count: ' + name)
            report['executions'].append(dict(name=name, checks=int(counts[0])))
        print(name + ': ' + result.stdout.strip().split('\n')[-1], flush=True)

    try:
        node, tsc = args.node, [args.node, args.tsc]
        run('node-version', [node, '--version'])
        run('typescript-version', [*tsc, '--version'])
        run('compiler-version', [args.cxx, '--version'])
        runtime = build / 'runtime'
        run('single-source-runtime', [sys.executable, ROOT / 'lib/client/web/build.py',
            '--node', node, '--tsc', args.tsc, '--output', runtime])
        suffix = '.exe' if os.name == 'nt' else ''
        bridge = build / ('bridge' + suffix)
        flags = ['-std=c++20', '-O1' if args.sanitize else '-O2', '-Wall', '-Wextra', '-Werror',
                 '-Ilib', '-Ilib/boost_pfr/include', '-Ilib/magic_enum']
        if args.sanitize:
            flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        run('compile-bridge', [args.cxx, *flags, HERE / 'Bridge.cpp',
            ROOT / 'lib/integration/encoded/Dispatcher.cpp', ROOT / 'lib/telemetry/encoded/ReadMany.cpp',
            ROOT / 'lib/integration/encoded/ReadManyDispatcher.cpp',
            ROOT / 'lib/telemetry/model/Adapter.cpp', ROOT / 'lib/telemetry/abi/StructuredAbi.cpp', '-o', bridge])
        descriptor, capability = build / 'descriptor.bin', build / 'trm1.bin'
        run('descriptor', [bridge, '--descriptor', descriptor])
        run('capability', [bridge, '--capability', capability])
        sdk = build / 'generated'
        run('generate', [sys.executable, '-m', 'tools.telemetry_codegen', 'generate', descriptor,
                        '--targets', 'js,ts', '--output', sdk])
        run('compile-generated', [*tsc, sdk / 'ts/device.ts', '--strict', '--target', 'ES2022',
                                 '--module', 'ES2022', '--declaration', '--outDir', sdk / 'compiled'])
        for label, module, generated in [('js', ROOT / 'lib/client/web/Client.js', sdk / 'js'),
                                         ('ts', runtime / 'Client.js', sdk / 'compiled')]:
            run('checks-' + label, [node, HERE / 'Check.mjs', module], 522)
            run('integration-' + label, [node, HERE / 'Integration.mjs', module,
                 generated / 'device.js', descriptor, capability, bridge], 99)
            consumer = build / ('consumer-' + label)
            consumer.mkdir(exist_ok=True)
            shutil.copytree(runtime, consumer / 'runtime', dirs_exist_ok=True)
            shutil.copytree(generated, consumer / 'sdk', dirs_exist_ok=True)
            shutil.copy2(HERE / 'Consumer.ts', consumer / 'Consumer.ts')
            run('strict-consumer-' + label, [*tsc, consumer / 'Consumer.ts', '--strict',
                 '--noUnusedLocals', '--noUnusedParameters', '--target', 'ES2022', '--module', 'ES2022', '--noEmit'])
        report['source_sha256_after'] = snapshot()
        if report['source_sha256'] != report['source_sha256_after']:
            raise RuntimeError('source inputs changed during qualification')
        report['checks'] = sum(row['checks'] for row in report['executions'])
        report['passed'] = True
    finally:
        report['finished_at_utc'] = datetime.now(timezone.utc).isoformat()
        (build / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
