#!/usr/bin/env python3
"""Qualify runtime-bound clients with real fixed-storage backends (MIT)."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def tool(name):
    candidate = shutil.which(name) or name
    path = Path(candidate).absolute()
    if not path.is_file():
        raise ValueError('Tool not found: ' + str(path))
    # Preserve clang++ invocation spelling; resolving its symlink selects clang.
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True, type=Path)
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--arm-cxx')
    args = parser.parse_args()
    out = args.build_dir.resolve()
    if out == ROOT or ROOT in out.parents or out in ROOT.parents:
        parser.error('Use a dedicated external output directory')
    if out.exists() and any(out.iterdir()):
        parser.error('Use a fresh empty output directory')
    out.mkdir(parents=True, exist_ok=True)
    compiler = tool(args.cxx)
    environment = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                       UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1', PYTHONDONTWRITEBYTECODE='1')
    report = dict(passed=False, hardware=False, sanitize=args.sanitize, commands=[], executions={},
                  compiler_sha256=digest(compiler),
                  source_head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip())

    def run(label, command, expected=0, failure=False):
        started = time.monotonic()
        command = list(map(str, command))
        try:
            result = subprocess.run(command, cwd=ROOT, env=environment, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, text=True, timeout=600)
            output, code = result.stdout, result.returncode
        except subprocess.TimeoutExpired as error:
            output = error.stdout or ''
            if isinstance(output, bytes):
                output = output.decode(errors='replace')
            code = None
        log = out / (label + '.log')
        log.write_text(output, encoding='utf-8')
        report['commands'].append(dict(name=label, command=command, exit=code,
                                       expected='nonzero' if failure else expected,
                                       seconds=time.monotonic() - started, log=log.name, sha256=digest(log)))
        if code is None or (code == 0 if failure else code != expected):
            raise RuntimeError(label + ' failed\n' + output)
        print(label + ': passed', flush=True)
        return output

    def count(label, output, floor):
        match = re.search(r': (\d+) checks, 0 failures', output)
        if not match or int(match[1]) < floor:
            raise RuntimeError(label + ': runtime check floor not reached')
        report['executions'][label] = int(match[1])

    base = [compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-pedantic-errors', '-Ilib']
    flags = base + (['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                     '-fno-omit-frame-pointer'] if args.sanitize else ['-O2'])
    native = flags + ['-Ilib/boost_pfr/include', '-Ilib/magic_enum']
    implementation = ['lib/telemetry/model/Adapter.cpp', 'lib/telemetry/abi/StructuredAbi.cpp',
                      'lib/integration/encoded/Dispatcher.cpp', 'lib/client/rpc/RpcClient.cpp',
                      'lib/integration/telemetry/v3/RpcMailbox.cpp', 'lib/resource/protocol/Protocol.cpp']
    inputs = {ROOT / 'CMakeLists.txt', ROOT / 'cmake/Modules.cmake', ROOT / '.github/workflows/ci.yml',
              ROOT / 'lib/integration/dynamic/dynamic.pri', ROOT / 'lib/client/client.pri',
              ROOT / 'lib/telemetry/runtime/runtime.pri'}
    inputs.update(path for folder in (HERE, ROOT / 'tests/dynamic_client_review', ROOT / 'examples/dynamic_client')
                  for path in folder.rglob('*') if path.is_file() and '__pycache__' not in path.parts)

    def snapshot():
        return {path.relative_to(ROOT).as_posix(): digest(path) for path in sorted(inputs)}

    suffix = '.exe' if os.name == 'nt' else ''

    def build(stem, source, extra=(), native_link=False):
        binary = out / (stem + suffix)
        selected = native if native_link else flags
        run('build-' + stem, selected + list(extra) + [source] + (implementation if native_link else []) + ['-o', binary])
        return binary

    try:
        run('compiler', [compiler, '--version'])
        run('python', [sys.executable, '--version'])
        standalone = run('standalone-dependencies', flags + ['-MM', HERE / 'Standalone.cpp'])
        if re.search(r'(?:^|[/\\])(?:resource|reflection|boost_pfr|magic_enum|wire)[/\\]', standalone):
            raise RuntimeError('DynamicClient has an unwanted Native, transport or Resource dependency')
        dependencies = [standalone, run('host-dependencies', native + ['-MM', HERE / 'Host.cpp']),
                        run('gates-dependencies', native + ['-MM', HERE / 'Gates.cpp'])]
        dependencies += [run('dependency-' + str(index), native + ['-MM', source])
                         for index, source in enumerate(implementation)]
        example = ROOT / 'examples/dynamic_client/Host.cpp'
        if not example.is_file():
            raise RuntimeError('Required runnable example is not ready: ' + str(example))
        dependencies.append(run('example-dependencies', native + ['-MM', example]))
        for text in dependencies:
            parts = shlex.split(text.replace('\\\n', ' '), posix=os.name != 'nt')[1:]
            for part in parts:
                path = (ROOT / part.strip('"')).resolve()
                if path.is_file():
                    if ROOT not in path.parents:
                        raise RuntimeError('Unexpected non-repository -MM dependency: ' + str(path))
                    inputs.add(path)
        report['source_sha256_before'] = snapshot()
        gates = build('Gates', HERE / 'Gates.cpp', native_link=True)
        count('Gates', run('execute-Gates', [gates]), 101)
        normal = build('Host', HERE / 'Host.cpp', native_link=True)
        count('Host', run('execute-Host', [normal]), 594)
        noheap = build('NoHeap', HERE / 'NoHeap.cpp', native_link=True)
        count('NoHeap', run('execute-NoHeap', [noheap]), report['executions']['Host'])
        control = build('NoHeapControl', HERE / 'NoHeap.cpp', ['-DDYNAMIC_HEAP_CONTROL'], native_link=True)
        output = run('execute-NoHeapControl', [control], expected=86)
        if 'runtime heap allocation refused' not in output:
            raise RuntimeError('Allocation negative control did not reach the trap')
        report['noheap_negative_exit'] = 86
        isolated = build('Standalone', HERE / 'Standalone.cpp')
        run('execute-Standalone', [isolated])
        positive = build('NegativePositiveControl', HERE / 'Negative.cpp')
        run('execute-NegativePositiveControl', [positive])
        refusals = ('RAW', 'CORE', 'UNWRAP', 'TYPED', 'POD', 'FORGE')
        for macro in refusals:
            run('negative-' + macro, flags + ['-DDYNAMIC_' + macro, '-fsyntax-only', HERE / 'Negative.cpp'], failure=True)
        report['negative_compile_refusals'] = len(refusals)
        demo = build('Example', example, native_link=True)
        run('execute-Example', [demo])
        core = out / 'core-policy'
        run('core-policy', [sys.executable, '-B', ROOT / 'tests/dynamic_client_review/run.py',
                            '--cxx', compiler, '--build-dir', core] + (['--sanitize'] if args.sanitize else []))
        core_receipt = json.loads((core / 'summary.json').read_text())
        if not core_receipt['passed']:
            raise RuntimeError('Independent generic-core policy suite failed')
        report['core_policy_summary_sha256'] = digest(core / 'summary.json')
        if args.arm_cxx:
            arm = tool(args.arm_cxx)
            run('arm-compiler', [arm, '--version'])
            image = out / 'dynamic-client-arm.o'
            run('offline-arm-compile', [arm, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
                                        '-pedantic-errors', '-Ilib', '-mcpu=cortex-m7', '-mthumb',
                                        '-fno-exceptions', '-fno-rtti', '-fstack-usage', '-DTINY_DELEGATE_ENABLE_HEAP_FALLBACK=0',
                                        '-c', HERE / 'Standalone.cpp', '-o', image])
            nm = tool(str(arm).replace('g++', 'nm'))
            symbols = run('offline-arm-symbols', [nm, '-uC', image])
            if re.search(r'operator new|operator delete|\b(?:malloc|calloc|realloc|free)\b', symbols):
                raise RuntimeError('Offline ARM probe references heap allocation')
            report['offline_arm'] = dict(compiled=True, executed=False, allocation_symbols=0,
                                         stack_usage={path.name: path.read_text() for path in out.glob('*.su')})
        report['source_sha256_after'] = snapshot()
        if report['source_sha256_before'] != report['source_sha256_after']:
            raise RuntimeError('Source inputs changed during qualification')
        report['outputs'] = {path.name: digest(path) for path in out.iterdir() if path.is_file() and path.name != 'summary.json'}
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        (out / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('DynamicClient qualification passed:', report['executions'], flush=True)


if __name__ == '__main__':
    main()
