#!/usr/bin/env python3
"""Qualify the portable client, real backends and unchanged bare-metal loop.

Builds and generated output stay outside the repository. ARM results are
offline compilation/stack evidence, not physical board or DMA measurements.
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
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True, type=Path)
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--arm-cxx')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--thread-sanitize', action='store_true')
    args = parser.parse_args()
    build = args.build_dir.resolve()
    if ROOT == build or ROOT in build.parents:
        parser.error('Use an external build directory')
    if args.sanitize and args.thread_sanitize:
        parser.error('Run address/undefined and thread sanitizers separately')
    build.mkdir(parents=True, exist_ok=True)
    report = {'passed': False, 'commands': [], 'checks': {}, 'artifacts': {}}

    def snapshot():
        paths = [path for base in (ROOT / 'lib', HERE, ROOT / 'examples/async_client')
                 for path in base.rglob('*') if path.is_file() and path.suffix in {'.hpp', '.h', '.cpp', '.py'}]
        return {path.relative_to(ROOT).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in sorted(paths)}

    report['source_inputs'] = snapshot()
    environment = dict(os.environ, PYTHONDONTWRITEBYTECODE='1')
    environment.setdefault('ASAN_OPTIONS', 'detect_leaks=1:halt_on_error=1')
    environment.setdefault('UBSAN_OPTIONS', 'halt_on_error=1:print_stacktrace=1')

    def run(command, name, expected=0):
        command = list(map(str, command))
        start = time.monotonic()
        result = subprocess.run(command, cwd=ROOT, env=environment, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600)
        (build / (name + '.log')).write_text(result.stdout, encoding='utf-8')
        report['commands'].append({'name': name, 'command': command,
                                   'exit': result.returncode, 'seconds': time.monotonic() - start})
        if result.returncode != expected:
            print(result.stdout, file=sys.stderr)
            raise RuntimeError(name + ' failed')
        return result.stdout

    flags = [args.cxx, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
             '-DTINY_DELEGATE_ENABLE_HEAP_FALLBACK=0', '-DTELEMETRY_STRUCTURED_LOCAL_BYTES=0',
             '-Ilib', '-Ilib/boost_pfr/include', '-Ilib/magic_enum']
    flags += ['-O1', '-g', '-fno-omit-frame-pointer', '-fsanitize=address,undefined',
              '-fno-sanitize-recover=all'] if args.sanitize else ['-O2']
    if args.thread_sanitize:
        flags += ['-g', '-fsanitize=thread', '-fno-omit-frame-pointer']
    suffix = '.exe' if os.name == 'nt' else ''
    native = ['lib/telemetry/model/Adapter.cpp', 'lib/telemetry/abi/StructuredAbi.cpp']
    encoded = ['lib/integration/encoded/Dispatcher.cpp', *native]
    rpc = ['lib/client/rpc/RpcClient.cpp', 'lib/integration/telemetry/v3/RpcMailbox.cpp',
           'lib/resource/protocol/Protocol.cpp', *native]

    def executable(stem, source, sources=(), extra=(), arguments=()):
        target = build / (stem + suffix)
        run([*flags, *extra, source, *sources, '-o', target], stem + '-build')
        output = run([target, *arguments], stem + '-run')
        matches = re.findall(r'checks=(\d+) failures=(\d+)', output)
        if matches:
            if len(matches) != 1 or int(matches[0][0]) <= 0 or int(matches[0][1]):
                raise RuntimeError('Invalid counted result: ' + stem)
            report['checks'][stem] = int(matches[0][0])
        report['artifacts'][str(target)] = hashlib.sha256(target.read_bytes()).hexdigest()
        print(stem + ': ' + (output.strip() or 'PASS'), flush=True)
        return target

    try:
        report['compiler'] = run([args.cxx, '--version'], 'compiler')
        patterns = {
            1: r'Completion ownership must be constructed without exceptions',
            2: r'Completion ownership must be constructed without exceptions',
            3: r'(inline|fit|storage)',
            4: r'Response exceeds the single DTO scratch',
            5: r'(no matching|constraints)',
            6: r'(synchronized|Lock policy)',
            7: r'Completion must be noexcept',
        }
        for case in range(8):
            output = run([*flags, '-fsyntax-only', '-DASYNC_NEGATIVE_CASE=' + str(case),
                          HERE / 'Negative.cpp'], 'compile-contract-' + str(case), expected=0 if case == 0 else 1)
            if case and not re.search(patterns[case], output):
                raise RuntimeError('Compile refusal missed its intended diagnostic: ' + str(case))
        report['compile_contracts'] = {'positive': 1, 'expected_rejections': 7}
        executable('core', HERE / 'Core.cpp', extra=['-pthread'])
        if not args.thread_sanitize:
            executable('noheap', HERE / 'NoHeap.cpp')
            executable('encoded', HERE / 'Encoded.cpp', encoded)
            executable('rpc', HERE / 'RpcChecks.cpp', rpc)
            fixtures = build / 'fixtures'
            fixtures.mkdir(exist_ok=True)
            executable('producer', ROOT / 'tests/codegen/Producer.cpp', native, arguments=[fixtures])
            generated = build / 'generated'
            run([sys.executable, '-B', '-m', 'tools.telemetry_codegen', 'generate',
                 fixtures / 'descriptor.bin', '--targets', 'cpp', '--namespace', 'device_api',
                 '--output', generated], 'generate')
            include = '-I' + str(generated / 'cpp')
            executable('bare-metal-host', ROOT / 'examples/async_client/BareMetal.cpp',
                       [HERE / 'BareMetalHost.cpp', *encoded], extra=[include])
            if args.arm_cxx:
                obj = build / 'bare-metal.o'
                arm_flags = [args.arm_cxx, '-std=c++20', '-Os', '-Wall', '-Wextra', '-Werror',
                             '-mcpu=cortex-m7', '-mthumb', '-fno-exceptions', '-fno-rtti',
                             '-fstack-usage', '-ffunction-sections', '-fdata-sections',
                             '-DTINY_DELEGATE_ENABLE_HEAP_FALLBACK=0', '-Ilib', include]
                run([*arm_flags, '-c', ROOT / 'examples/async_client/BareMetal.cpp', '-o', obj], 'arm-build')
                prefix = args.arm_cxx.removesuffix('g++')
                dependencies = run([args.arm_cxx, '-std=c++20', '-mcpu=cortex-m7', '-mthumb',
                                    '-Ilib', include, '-MM', ROOT / 'examples/async_client/BareMetal.cpp'], 'arm-dependencies')
                if re.search(r'(?:/|\\)(?:QtCore|QtQml|FreeRTOS|model|boost_pfr)(?:/|\\)|(?:^|\s)(?:mutex|thread)(?:\s|$)', dependencies):
                    raise RuntimeError('Bare-metal dependency boundary changed')
                symbols = run([prefix + 'nm', '-uC', obj], 'arm-symbols')
                if re.search(r'\b(malloc|calloc|realloc|operator new|pthread_|QMetaObject|HAL_)', symbols):
                    raise RuntimeError('Unexpected core allocation/platform reference')
                frames = []
                for usage in build.glob('*.su'):
                    for row in usage.read_text().splitlines():
                        parts = row.split('\t')
                        if len(parts) >= 3 and parts[-2].isdigit():
                            frames.append(int(parts[-2]))
                if not frames or max(frames) > 512:
                    raise RuntimeError('Bare-metal client stack-frame gate exceeded 512 bytes')
                report['arm'] = {'max_stack_frame': max(frames), 'frames': len(frames),
                                 'object_sha256': hashlib.sha256(obj.read_bytes()).hexdigest()}
        report['passed'] = True
    finally:
        report['source_inputs_end'] = snapshot()
        if report['source_inputs'] != report['source_inputs_end']:
            report['passed'] = False
            report['error'] = 'Source inputs changed; rerun after they are stable'
        (build / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    if not report['passed']:
        raise RuntimeError(report.get('error', 'Qualification failed'))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
