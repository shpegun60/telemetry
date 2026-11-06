#!/usr/bin/env python3
"""Packed, installed headless Session consumers with deterministic and native checks. MIT."""
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
import tarfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inputs():
    selected = [ROOT / 'package.json', ROOT / 'VERSION', ROOT / 'LICENSE']
    for directory in ('lib', 'web', 'tools/telemetry_codegen', 'tests/connection_session',
                      'tests/client_dynamic_web', 'tests/batch_static'):
        selected += [p for p in (ROOT / directory).rglob('*') if p.is_file() and
                     '__pycache__' not in p.parts and p.suffix in
                     ('.hpp', '.cpp', '.py', '.js', '.ts', '.json', '.mjs', '.md')]
    selected.append(ROOT / 'tests/client_web/Trace.mjs')
    return {str(p.relative_to(ROOT)).replace('\\', '/'): digest(p) for p in sorted(set(selected))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--node', required=True)
    parser.add_argument('--tsc', type=Path, required=True)
    parser.add_argument('--npm-cli', type=Path)
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    out = args.build_dir.resolve()
    if out == ROOT or ROOT in out.parents or out in ROOT.parents:
        parser.error('Use an external dedicated output directory')
    report = dict(passed=False, hardware=False, started_utc=datetime.now(timezone.utc).isoformat(),
                  checks=0, commands=[], executions=[])
    if out.exists() and (not out.is_dir() or any(out.iterdir())):
        if out.is_dir():
            report['error'] = 'Output must be fresh and empty; no previous success is current'
            (out / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
        parser.error('Output must be fresh and empty; nothing is deleted')
    out.mkdir(parents=True, exist_ok=True)
    summary = out / 'summary.json'
    summary.write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', PYTHONNOUSERSITE='1',
               npm_config_cache=str(out / 'npm-cache'), npm_config_update_notifier='false',
               ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
               UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    env.pop('PYTHONPATH', None)

    def check(condition, message):
        report['checks'] += 1
        if not condition:
            raise RuntimeError(message)

    def run(name, command, *, cwd=out, floor=None, expected_cases=None):
        command = list(map(str, command))
        result = subprocess.run(command, cwd=cwd, env=env, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600)
        log = out / (name + '.log')
        log.write_text(result.stdout, encoding='utf8')
        report['commands'].append(dict(name=name, command=command, returncode=result.returncode,
                                       log=log.name, sha256=digest(log)))
        check(result.returncode == 0, name + ' failed:\n' + result.stdout[-14000:])
        if floor is not None:
            counts = re.findall(r'checks=(\d+)(?: packets=\d+)?(?: cases=\d+)? failures=0', result.stdout)
            check(len(counts) == 1 and int(counts[0]) >= floor,
                  'Missing maintained nonzero check count: ' + name)
            report['executions'].append(dict(name=name, checks=int(counts[0])))
            if expected_cases is not None:
                cases = re.findall(r' cases=(\d+) failures=0', result.stdout)
                check(cases == [str(expected_cases)], 'Unexpected scenario count: ' + name)
        print(name + ': exit=' + str(result.returncode), flush=True)
        return result.stdout

    try:
        node = Path(shutil.which(args.node) or args.node).absolute()
        cxx = Path(shutil.which(args.cxx) or args.cxx).absolute()
        tsc = args.tsc.resolve()
        npm = args.npm_cli.resolve() if args.npm_cli else next((p for p in
            (node.parent / 'node_modules/npm/bin/npm-cli.js',
             node.parent.parent / 'lib/node_modules/npm/bin/npm-cli.js') if p.is_file()), None)
        for name, path in [('node', node), ('compiler', cxx), ('tsc', tsc), ('npm', npm)]:
            check(path is not None and path.is_file(), 'Missing tool: ' + name)
        report['tools'] = {name: dict(path=str(path), sha256=digest(path)) for name, path in
                           [('node', node), ('compiler', cxx), ('tsc', tsc), ('npm', npm)]}
        env['PATH'] = os.pathsep.join((str(node.parent), str(cxx.parent), env.get('PATH', '')))
        report['source_sha256_before'] = inputs()
        run('node-version', [node, '--version'])
        run('typescript-version', [node, tsc, '--version'])
        run('compiler-version', [cxx, '--version'])
        run('session-single-source', [sys.executable, '-B', ROOT / 'web/build_session.py',
            '--node', node, '--tsc', tsc, '--output', out / 'session-compiled'])
        bridge = out / ('bridge.exe' if os.name == 'nt' else 'bridge')
        flags = ['-std=c++20', '-O1' if args.sanitize else '-O2', '-Wall', '-Wextra', '-Werror',
                 '-I' + str(ROOT / 'lib'), '-I' + str(ROOT / 'lib/boost_pfr/include'),
                 '-I' + str(ROOT / 'lib/magic_enum')]
        if args.sanitize:
            flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        sources = ['tests/client_dynamic_web/Bridge.cpp', 'lib/integration/encoded/Dispatcher.cpp',
                   'lib/telemetry/encoded/Batch.cpp', 'lib/integration/encoded/BatchDispatcher.cpp',
                   'lib/telemetry/encoded/ReadMany.cpp', 'lib/integration/encoded/ReadManyDispatcher.cpp',
                   'lib/telemetry/model/Adapter.cpp', 'lib/telemetry/abi/StructuredAbi.cpp']
        run('native-build', [cxx, *flags, *(ROOT / p for p in sources), '-o', bridge])
        report['native_executable'] = dict(path=bridge.name, size=bridge.stat().st_size, sha256=digest(bridge))
        descriptor = out / 'descriptor.bin'
        run('native-descriptor', [bridge, '--descriptor', descriptor])
        generated = out / 'generated'
        run('generate', [sys.executable, '-B', '-m', 'tools.telemetry_codegen', 'generate', descriptor,
                         '--targets', 'js,ts', '--structural-metadata', '--wire-editing', '--output', generated], cwd=ROOT)
        report['descriptor_sha256'] = digest(descriptor)
        report['generated_manifest_sha256'] = digest(generated / 'manifest.json')
        package = json.loads((ROOT / 'package.json').read_text(encoding='utf8'))
        check(not any('ux' in name.lower() for name in package.get('dependencies', {})),
              'SDK npm package must remain independent of UX')
        prepared = out / 'prepared-package'
        prepared.mkdir()
        for name in dict.fromkeys(['package.json', 'README.md', *package['files']]):
            source, target = ROOT / name, prepared / name
            if source.is_dir():
                shutil.copytree(source, target, ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)
        dist = out / 'dist'
        dist.mkdir()
        packed = json.loads(run('npm-pack', [node, npm, 'pack', prepared, '--json', '--pack-destination', dist]))
        check(len(packed) == 1, 'Exactly one packed SDK')
        tarball = dist / packed[0]['filename']
        report['npm_tarball_sha256'] = digest(tarball)
        with tarfile.open(tarball) as archive:
            names = set(archive.getnames())
        for name in ('web/Session.js', 'web/Session.d.ts', 'web/WebSocketTransport.js',
                     'web/WebSocketConnection.js', 'web/WebSocketConnection.d.ts'):
            check('package/' + name in names, 'Missing packed optional module: ' + name)
        check(not any('/tests/' in p or '/apps/' in p or p.endswith('.qml') for p in names),
              'Headless package contains no UX/test dependency')
        consumer = out / 'consumer'
        consumer.mkdir()
        (consumer / 'package.json').write_text('{"private":true,"type":"module"}\n', encoding='utf8')
        run('npm-install', [node, npm, 'install', '--offline', '--ignore-scripts', '--no-audit',
                            '--no-fund', tarball], cwd=consumer)
        # Hide the prepared package after installation; consumers use installed tarball content only.
        prepared.rename(out / 'unavailable-prepared-package')
        shutil.copytree(generated, consumer / 'generated')
        shutil.copy2(descriptor, consumer / 'descriptor.bin')
        for name in ('Check.mjs', 'Consumer.ts', 'Integration.mjs', 'Profile.mjs'):
            shutil.copy2(HERE / name, consumer / name)
        shutil.copy2(ROOT / 'tests/client_web/Trace.mjs', consumer / 'Trace.mjs')
        run('strict-consumer', [node, tsc, consumer / 'Consumer.ts', '--strict', '--noUnusedLocals',
            '--noUnusedParameters', '--target', 'ES2022', '--module', 'NodeNext',
            '--moduleResolution', 'NodeNext', '--noEmit'], cwd=consumer)
        run('lifecycle', [node, consumer / 'Check.mjs'], cwd=consumer, floor=906, expected_cases=48)
        run('reference-profile', [node, consumer / 'Profile.mjs'], cwd=consumer, floor=184, expected_cases=35)
        run('native-integration', [node, consumer / 'Integration.mjs', bridge], cwd=consumer, floor=303)
        report['source_sha256_after'] = inputs()
        check(report['source_sha256_before'] == report['source_sha256_after'], 'Source inputs changed during checks')
        report['checks'] += sum(item['checks'] for item in report['executions'])
        report['passed'] = True
    except BaseException as error:
        report['error'] = str(error)
        raise
    finally:
        report['finished_utc'] = datetime.now(timezone.utc).isoformat()
        summary.write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')


if __name__ == '__main__':
    main()
