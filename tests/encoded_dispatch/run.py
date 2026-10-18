#!/usr/bin/env python3
"""Qualify protocol-free encoded dispatch; keep all output outside the repo.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    build = args.build_dir.resolve()
    if build == ROOT or ROOT in build.parents:
        parser.error('Use an external build directory')
    build.mkdir(parents=True, exist_ok=True)
    compiler = shutil.which(args.cxx) or args.cxx
    report = {'passed': False, 'commands': []}
    paths = [path for base in (ROOT / 'lib', HERE)
             for path in base.rglob('*') if path.is_file() and path.suffix in {'.hpp', '.h', '.cpp', '.py'}]
    snapshot = lambda: {path.relative_to(ROOT).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
                        for path in sorted(paths)}
    report['source_sha256'] = snapshot()
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1',
               ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1')

    def run(command, name):
        command = list(map(str, command))
        start = time.monotonic()
        result = subprocess.run(command, cwd=ROOT, env=env, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=180)
        (build / (name + '.log')).write_text(result.stdout, encoding='utf-8')
        report['commands'].append({'name': name, 'command': command, 'returncode': result.returncode,
                                   'seconds': time.monotonic() - start})
        if result.returncode:
            raise RuntimeError(name + ': ' + result.stdout[-4000:])
        return result.stdout

    try:
        report['source_head'] = run(['git', 'rev-parse', 'HEAD'], 'source-head').strip()
        report['source_status'] = run(['git', 'status', '--porcelain=v1'], 'source-status')
        report['compiler'] = run([compiler, '--version'], 'compiler')
        report['compiler_sha256'] = hashlib.sha256(Path(compiler).read_bytes()).hexdigest()
        flags = ['-std=c++20', '-Wall', '-Wextra', '-Werror', '-fno-exceptions', '-fno-rtti',
                 '-DTINY_DELEGATE_ENABLE_HEAP_FALLBACK=0', '-DTELEMETRY_STRUCTURED_LOCAL_BYTES=0',
                 '-Ilib', '-Ilib/boost_pfr/include', '-Ilib/magic_enum']
        flags += ['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                  '-fno-omit-frame-pointer'] if args.sanitize else ['-O2']
        binary = build / ('Core.exe' if os.name == 'nt' else 'Core')
        dependency = build / 'Core.d'
        run([compiler, *flags, '-MMD', '-MF', dependency, '-c', HERE / 'Core.cpp',
             '-o', build / 'Core.o'], 'core-build')
        dependencies = dependency.read_text().replace('\\', '/')
        if '/resource/' in dependencies or '/client/' in dependencies:
            raise RuntimeError('Core acquired a Resource or Client dependency')
        report['resource_client_dependencies'] = False
        run([compiler, *flags, build / 'Core.o', 'lib/telemetry/model/Adapter.cpp',
             'lib/telemetry/abi/StructuredAbi.cpp', '-o', binary], 'core-link')
        output = run([binary], 'core-run')
        counts = re.search(r'encoded dispatch checks=(\d+) failures=0', output)
        if not counts or int(counts[1]) < 200:
            raise RuntimeError('Missing meaningful dispatcher qualification counts')
        report['checks'] = int(counts[1])
        report['binary_sha256'] = hashlib.sha256(binary.read_bytes()).hexdigest()
        report['source_sha256_after'] = snapshot()
        if report['source_sha256'] != report['source_sha256_after']:
            raise RuntimeError('Source inputs changed during qualification')
        report['passed'] = True
        print(output.strip())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        report['error'] = str(error)
        print(error)
    (build / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
