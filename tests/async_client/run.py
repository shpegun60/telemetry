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


def counted_result(output, minimum, stem):
    matches = re.findall(r'(?<!\S)checks=(\d+) failures=(\d+)(?!\S)', output)
    if (len(matches) != 1 or int(matches[0][0]) < minimum
            or int(matches[0][0]) <= 0 or int(matches[0][1]) != 0):
        raise RuntimeError('Missing/invalid counted result: ' + stem)
    return int(matches[0][0])


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

    def run(command, name, expected=0, cwd=ROOT):
        command = list(map(str, command))
        start = time.monotonic()
        result = subprocess.run(command, cwd=cwd, env=environment, text=True,
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

    def executable(stem, source, sources=(), extra=(), arguments=(), *, minimum):
        target = build / (stem + suffix)
        run([*flags, *extra, source, *sources, '-o', target], stem + '-build')
        output = run([target, *arguments], stem + '-run')
        if minimum is not None:
            report['checks'][stem] = counted_result(output, minimum, stem)
        report['artifacts'][str(target)] = hashlib.sha256(target.read_bytes()).hexdigest()
        print(stem + ': ' + (output.strip() or 'PASS'), flush=True)
        return target

    try:
        report['compiler'] = run([args.cxx, '--version'], 'compiler')
        # These are real successful subprocesses: exit zero alone must never
        # qualify a counted test which silently stopped executing assertions.
        invalid_reports = {
            'missing': '',
            'multiple': 'control checks=2 failures=0\ncontrol checks=2 failures=0',
            'multiple-same-line': 'control checks=2 failures=0 control checks=2 failures=0',
            'zero': 'control checks=0 failures=0',
            'failing': 'control checks=2 failures=1',
            'below-floor': 'control checks=1 failures=0',
        }
        for name, value in invalid_reports.items():
            output = run([sys.executable, '-B', '-c', 'import sys; sys.stdout.write(sys.argv[1])',
                          value], 'report-control-' + name)
            try:
                counted_result(output, 2, name)
            except RuntimeError:
                pass
            else:
                raise RuntimeError('Counted-report control was accepted: ' + name)
        if counted_result('control checks=2 failures=0\n', 2, 'positive') != 2:
            raise RuntimeError('Counted-report positive control failed')
        report['counted_report_controls'] = {'expected_rejections': len(invalid_reports),
                                             'positive': 1}
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
        executable('core', HERE / 'Core.cpp', extra=['-pthread'], minimum=148)
        if not args.thread_sanitize:
            executable('noheap', HERE / 'NoHeap.cpp', minimum=130)
            executable('encoded', HERE / 'Encoded.cpp', encoded, minimum=348)
            retrying = executable('retrying', HERE / 'Retrying.cpp', encoded, minimum=800)
            noheap_retrying = executable('retrying-noheap', HERE / 'RetryingNoHeap.cpp',
                                         encoded, minimum=800)
            control = run([noheap_retrying, '--allocation-control'],
                          'retrying-allocation-control', expected=86)
            if control.count('RETRYING_HEAP_FORBIDDEN') != 1:
                raise RuntimeError('Retry allocation trap lost its positive control')
            for option in ('--zero-reply-timeout', '--oversized-reply-timeout'):
                control = run([retrying, option], 'retrying' + option,
                              expected=3 if os.name == 'nt' else -6, cwd=build)
                if control.count('RETRY_INVALID_OPTIONS_CONTROL') != 1:
                    raise RuntimeError('Retry option rejection lost its control marker')
            report['retrying_controls'] = {'allocation_trap': 1, 'invalid_options': 2}
            executable('rpc', HERE / 'RpcChecks.cpp', rpc, minimum=1104)
            fixtures = build / 'fixtures'
            fixtures.mkdir(exist_ok=True)
            executable('producer', ROOT / 'tests/codegen/Producer.cpp', native,
                       arguments=[fixtures], minimum=None)
            generated = build / 'generated'
            run([sys.executable, '-B', '-m', 'tools.telemetry_codegen', 'generate',
                 fixtures / 'descriptor.bin', '--targets', 'cpp', '--namespace', 'device_api',
                 '--output', generated], 'generate')
            include = '-I' + str(generated / 'cpp')
            executable('bare-metal-host', ROOT / 'examples/async_client/BareMetal.cpp',
                       [HERE / 'BareMetalHost.cpp', *encoded], extra=[include], minimum=None)
            executable('retry-bare-metal-host', ROOT / 'examples/async_client/BareMetal.cpp',
                       [HERE / 'BareMetalHost.cpp', *encoded],
                       extra=[include, '-DTELEMETRY_EXAMPLE_RETRY_ENCODED'], minimum=None)
            if args.arm_cxx:
                obj = build / 'bare-metal.o'
                arm_flags = [args.arm_cxx, '-std=c++20', '-Os', '-Wall', '-Wextra', '-Werror',
                             '-mcpu=cortex-m7', '-mthumb', '-fno-exceptions', '-fno-rtti',
                             '-fstack-usage', '-ffunction-sections', '-fdata-sections',
                             '-DTINY_DELEGATE_ENABLE_HEAP_FALLBACK=0', '-Ilib', include]
                run([*arm_flags, '-c', ROOT / 'examples/async_client/BareMetal.cpp', '-o', obj], 'arm-build')
                retry_obj = build / 'retry-bare-metal.o'
                run([*arm_flags, '-DTELEMETRY_EXAMPLE_RETRY_ENCODED', '-c',
                     ROOT / 'examples/async_client/BareMetal.cpp', '-o', retry_obj], 'retry-arm-build')
                retry_backend_obj = build / 'retrying-backend.o'
                run([*arm_flags, '-c', HERE / 'RetryingArm.cpp', '-o', retry_backend_obj],
                    'retry-backend-arm-build')
                prefix = args.arm_cxx.removesuffix('g++')
                dependencies = run([args.arm_cxx, '-std=c++20', '-mcpu=cortex-m7', '-mthumb',
                                    '-Ilib', include, '-MM', ROOT / 'examples/async_client/BareMetal.cpp'], 'arm-dependencies')
                if re.search(r'(?:/|\\)(?:QtCore|QtQml|FreeRTOS|model|boost_pfr)(?:/|\\)|(?:^|\s)(?:mutex|thread)(?:\s|$)', dependencies):
                    raise RuntimeError('Bare-metal dependency boundary changed')
                symbols = run([prefix + 'nm', '-uC', obj], 'arm-symbols')
                if re.search(r'\b(malloc|calloc|realloc|operator new|pthread_|QMetaObject|HAL_)', symbols):
                    raise RuntimeError('Unexpected core allocation/platform reference')
                retry_symbols = run([prefix + 'nm', '-uC', retry_backend_obj], 'retry-arm-symbols')
                if re.search(r'\b(malloc|calloc|realloc|operator new|pthread_|QMetaObject|HAL_|__atomic_)', retry_symbols):
                    raise RuntimeError('Unexpected retry allocation/platform reference')
                retry_dependencies = run([*arm_flags, '-MM', HERE / 'RetryingArm.cpp'],
                                         'retry-arm-dependencies')
                if re.search(r'(?:/|\\)(?:QtCore|QtQml|FreeRTOS|model|boost_pfr)(?:/|\\)|(?:^|\s)(?:mutex|thread)(?:\s|$)', retry_dependencies):
                    raise RuntimeError('Retry backend dependency boundary changed')
                size_symbols = run([prefix + 'nm', '-SC', retry_backend_obj], 'retry-arm-layout')
                sizes = {name: int(size, 16) for size, name in re.findall(
                    r'^[0-9a-fA-F]+\s+([0-9a-fA-F]+)\s+R\s+(encoded_backend_abi|retrying_backend_abi)$',
                    size_symbols, re.M)}
                if len(sizes) != 2:
                    raise RuntimeError('Missing literal ARM backend size evidence')
                report['retrying_arm'] = {'backend_sizes': sizes,
                    'object_sha256': hashlib.sha256(retry_backend_obj.read_bytes()).hexdigest(),
                    'bare_metal_sha256': hashlib.sha256(retry_obj.read_bytes()).hexdigest()}
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
