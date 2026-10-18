#!/usr/bin/env python3
"""Focused real-Model Encoded wire/backend qualification with external receipts.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SOURCES = [ROOT / 'lib/resource/telemetry/v3/EncodedDispatcher.cpp',
           ROOT / 'lib/telemetry/model/Adapter.cpp',
           ROOT / 'lib/telemetry/abi/StructuredAbi.cpp']
INCLUDES = [ROOT / 'lib', ROOT / 'lib/boost_pfr/include', ROOT / 'lib/magic_enum']


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inputs():
    """Literal authored/vendor include closure, excluding platform system headers."""
    pending = SOURCES + [HERE / 'Encoded.cpp', HERE / 'EncodedBridge.cpp']
    found = {Path(__file__).resolve()}
    while pending:
        path = pending.pop().resolve()
        if path in found:
            continue
        found.add(path)
        for name in re.findall(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]',
                               path.read_text(encoding='utf-8'), re.M):
            for directory in [path.parent] + INCLUDES:
                candidate = directory / name
                if candidate.is_file():
                    pending.append(candidate)
                    break
    return {path.relative_to(ROOT).as_posix(): digest(path) for path in sorted(found)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True, type=Path)
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--budget', type=int, choices=(0, 32), default=0)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--no-heap', action='store_true')
    args = parser.parse_args()
    out = args.build_dir.resolve()
    if out == ROOT or ROOT in out.parents:
        parser.error('build-dir must be outside the repository')
    # Keep the C++ driver name: resolving clang++ symlinks changes argv[0] to
    # clang and silently drops the C++ standard/sanitizer-vptr link libraries.
    compiler = Path(os.path.abspath(shutil.which(args.cxx) or args.cxx))
    if not compiler.is_file():
        parser.error('Compiler not found: ' + str(compiler))
    out.mkdir(parents=True, exist_ok=True)
    report = {'passed': False, 'budget': args.budget, 'sanitize': args.sanitize,
              'no_heap': args.no_heap, 'inputs_start': inputs(), 'commands': [],
              'compiler': str(compiler), 'compiler_sha256': digest(compiler)}
    environment = os.environ.copy()
    if args.sanitize:
        environment['ASAN_OPTIONS'] = 'detect_leaks=1:halt_on_error=1'
        environment['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'

    def run(command, label, data=None, expected=0):
        command = [str(value) for value in command]
        started = time.time()
        result = subprocess.run(command, cwd=ROOT, env=environment, input=data,
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                check=False)
        (out / (label + '.log')).write_text(result.stdout, encoding='utf-8')
        report['commands'].append({'command': command, 'exit': result.returncode,
                                   'seconds': time.time() - started, 'log': label + '.log'})
        if result.returncode != expected:
            raise RuntimeError(f'{label}: exit {result.returncode}, expected {expected}')
        return result.stdout

    try:
        report['compiler_version'] = run([compiler, '--version'], 'compiler')
        flags = [compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
                 '-DTINY_DELEGATE_ENABLE_HEAP_FALLBACK=0',
                 '-DTELEMETRY_STRUCTURED_LOCAL_BYTES=' + str(args.budget)]
        flags += ['-I' + str(path) for path in INCLUDES]
        flags += (['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                   '-fno-sanitize-recover=all'] if args.sanitize else ['-O2'])
        suffix = '.exe' if os.name == 'nt' else ''
        executable = out / ('encoded' + suffix)
        test_flags = flags + (['-DENCODED_NO_HEAP=1'] if args.no_heap else [])
        run(test_flags + [HERE / 'Encoded.cpp'] + SOURCES + ['-o', executable], 'build')
        output = run([executable], 'runtime')
        counts = re.findall(r'^encoded checks=(\d+) failures=(\d+)\s*$', output, re.M)
        if len(counts) != 1 or int(counts[0][0]) < 300 or int(counts[0][1]) != 0:
            raise RuntimeError('Missing successful positive runtime coverage report')
        report['checks'] = int(counts[0][0])
        if args.no_heap:
            control = run([executable, '--allocation-control'], 'allocation-control', expected=86)
            if control.count('ENCODED_HEAP_FORBIDDEN') != 1:
                raise RuntimeError('Heap prohibition control lost its named diagnostic')
            report['allocation_control'] = True
        bridge = out / ('encoded-bridge' + suffix)
        run(flags + [HERE / 'EncodedBridge.cpp'] + SOURCES + ['-o', bridge], 'build-bridge')
        descriptor = out / 'descriptor.bin'
        run([bridge, '--descriptor', descriptor], 'descriptor')
        if descriptor.stat().st_size <= 64:
            raise RuntimeError('Actual Model descriptor was not emitted')
        report['descriptor_sha256'] = digest(descriptor)

        def frame(operation, request, payload=b'', response=0, reply=False):
            return struct.pack('<IBBBBIIIIIB3x', 0x31514554, 1, int(reply), operation, 0,
                               17, request, 0, len(payload), response, 0) + payload

        value = struct.pack('<IB', 915, 0)
        requests = frame(1, 1, value).hex() + '\n' + frame(4, 2, response=5).hex() + '\n'
        replies = run([bridge], 'bridge-runtime', requests).splitlines()
        expected = [frame(1, 1, reply=True).hex(), frame(4, 2, value, 5, True).hex()]
        if replies != expected:
            raise RuntimeError('Persistent actual Model bridge did not apply/read exact bytes')
        report['bridge_roundtrips'] = len(replies)
        report['passed'] = True
    finally:
        report['inputs_end'] = inputs()
        if report['inputs_start'] != report['inputs_end']:
            report['passed'] = False
            report['inputs_changed'] = True
        (out / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    if not report['passed']:
        raise RuntimeError('Qualification inputs changed while the run was executing')
    print(json.dumps({'passed': True, 'checks': report['checks'],
                      'bridge_roundtrips': report['bridge_roundtrips'],
                      'summary': str(out / 'summary.json')}))


if __name__ == '__main__':
    main()
