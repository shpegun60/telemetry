#!/usr/bin/env python3
"""Qualify shared snapshots, Inspector and Workbench with real Encoded/RPC (MIT)."""
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
    # Preserve clang++ spelling; resolving its symlink changes driver mode.
    path = Path(shutil.which(name) or name).absolute()
    if not path.is_file():
        raise ValueError('Tool not found: ' + str(path))
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True, type=Path)
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--arm-cxx')
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--ninja', default='ninja')
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
        if log.exists():
            raise RuntimeError('Qualification log already exists: ' + str(log))
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

    def timeout_phases(label, output, workbench=False):
        owner, side_effect = ('Workbench', 'writes') if workbench else ('Inspector', 'reads')
        observed = re.findall(owner + r' timeout phase=(queued|offered|prepared) '
                              r'callbacks=(\d+) pending=(\d+) ' + side_effect + r'=(\d+) recovery=(\d+)', output)
        expected = [('queued', '1', '0', '0', '0'), ('offered', '1', '0', '0', '0'),
                    ('prepared', '1', '0', '1', '1')]
        if observed != expected:
            raise RuntimeError(label + ': deterministic queued/offered/prepared timeout cases missing')
        report.setdefault('timeout_phases', {})[label] = [phase[0] for phase in observed]

    base = [compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-pedantic-errors', '-Ilib']
    plain = base + ['-O2', '-DTINY_DELEGATE_ENABLE_HEAP_FALLBACK=0']
    flags = base + (['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                     '-fno-omit-frame-pointer'] if args.sanitize else ['-O2'])
    includes = ['-Ilib/boost_pfr/include', '-Ilib/magic_enum']
    implementation = ['lib/telemetry/model/Adapter.cpp', 'lib/telemetry/abi/StructuredAbi.cpp',
                      'lib/integration/encoded/Dispatcher.cpp', 'lib/client/rpc/RpcClient.cpp',
                      'lib/integration/telemetry/v3/RpcMailbox.cpp', 'lib/resource/protocol/Protocol.cpp']
    inputs = {ROOT / 'CMakeLists.txt', ROOT / 'cmake/Modules.cmake', ROOT / '.github/workflows/ci.yml'}
    inputs.update(path for path in HERE.rglob('*') if path.is_file() and '__pycache__' not in path.parts)
    inputs.update(path for path in (ROOT / 'cmake').rglob('*') if path.is_file())
    inputs.update(path for path in (ROOT / 'examples/inspector').rglob('*') if path.is_file())
    for folder in ('tests/workbench', 'lib/integration/workbench'):
        inputs.update(path for path in (ROOT / folder).rglob('*')
                      if path.is_file() and '__pycache__' not in path.parts)
    for path in (ROOT / 'lib/integration/inspector/inspector.pri',
                 ROOT / 'lib/integration/dynamic/dynamic.pri', ROOT / 'lib/client/client.pri',
                 ROOT / 'lib/telemetry/runtime/runtime.pri'):
        if path.is_file():
            inputs.add(path)

    def snapshot():
        return {path.relative_to(ROOT).as_posix(): digest(path) for path in sorted(inputs)}

    suffix = '.exe' if os.name == 'nt' else ''

    def build(stem, source, extra=(), native_link=False, allocation_trap=False):
        binary = out / (stem + suffix)
        selected = plain if allocation_trap else flags
        run('build-' + stem, selected + (includes if native_link else []) + list(extra) + [source] +
            (implementation if native_link else []) + ['-o', binary])
        return binary

    try:
        for required in ('Host.cpp', 'CMakeLists.txt'):
            path = ROOT / 'examples/inspector' / required
            if not path.is_file():
                raise RuntimeError('Required runnable example is not ready: ' + str(path))
        run('compiler', [compiler, '--version'])
        run('python', [sys.executable, '--version'])
        isolated = run('standalone-dependencies', flags + ['-MM', HERE / 'Standalone.cpp'])
        if re.search(r'(?:^|[/\\])(?:resource|reflection|boost_pfr|magic_enum|wire)[/\\]', isolated):
            raise RuntimeError('Inspector has an unwanted Native, Resource or transport dependency')
        workbench = ROOT / 'tests/workbench'
        workbench_isolated = run('workbench-standalone-dependencies', flags + ['-MM', workbench / 'Standalone.cpp'])
        if re.search(r'(?:^|[/\\])(?:resource|reflection|boost_pfr|magic_enum|wire)[/\\]', workbench_isolated):
            raise RuntimeError('Workbench has an unwanted Native, Resource or transport dependency')
        sources = [HERE / name for name in ('Host.cpp', 'Lifetime.cpp', 'Negative.cpp', 'NoHeap.cpp', 'Snapshot.cpp')]
        sources += [workbench / name for name in ('Host.cpp', 'NoHeap.cpp', 'Command.cpp',
                                                 'CommandNoHeap.cpp', 'Service.cpp', 'ServiceNoHeap.cpp',
                                                 'Standalone.cpp', 'Negative.cpp')]
        example = ROOT / 'examples/inspector/Host.cpp'
        if example.is_file():
            sources.append(example)
        dependencies = [isolated, workbench_isolated] + [run('dependencies-' + str(index), flags + includes + ['-MM', source])
                                     for index, source in enumerate(sources + implementation)]
        for text in dependencies:
            for part in shlex.split(text.replace('\\\n', ' '), posix=os.name != 'nt')[1:]:
                path = (ROOT / part.strip('"')).resolve()
                if path.is_file():
                    if ROOT not in path.parents:
                        raise RuntimeError('Unexpected non-repository dependency: ' + str(path))
                    inputs.add(path)
        report['source_sha256_before'] = snapshot()
        host = build('Host', HERE / 'Host.cpp', native_link=True)
        output = run('execute-Host', [host])
        count('Host', output, 1313)
        timeout_phases('Host', output)
        lifetime = build('Lifetime', HERE / 'Lifetime.cpp', native_link=True)
        count('Lifetime', run('execute-Lifetime', [lifetime]), 348)
        snapshot_test = build('Snapshot', HERE / 'Snapshot.cpp', native_link=True)
        count('Snapshot', run('execute-Snapshot', [snapshot_test]), 516)
        # Allocation traps run without ASan replacement allocators, even in a sanitizer profile.
        noheap = build('NoHeap', HERE / 'NoHeap.cpp', native_link=True, allocation_trap=True)
        output = run('execute-NoHeap', [noheap])
        count('NoHeap', output, report['executions']['Host'])
        timeout_phases('NoHeap', output)
        control = build('NoHeapControl', HERE / 'NoHeap.cpp', ['-DINSPECTOR_HEAP_CONTROL=1'],
                        native_link=True, allocation_trap=True)
        output = run('execute-NoHeapControl', [control], expected=86)
        if 'inspector heap allocation refused' not in output:
            raise RuntimeError('Allocation negative control did not reach its trap')
        report['noheap_negative_exit'] = 86
        standalone = build('Standalone', HERE / 'Standalone.cpp')
        run('execute-Standalone', [standalone])
        positive = build('NegativePositiveControl', HERE / 'Negative.cpp')
        run('execute-NegativePositiveControl', [positive])
        refusals = ('WRITE', 'COMMAND', 'SERVICE', 'SUBMIT', 'CORE', 'RAW', 'FORGE',
                    'UNWRAP', 'CLIENT', 'BACKEND', 'TYPED_WRITE')
        for macro in refusals:
            run('negative-' + macro, flags + ['-DINSPECTOR_' + macro, '-fsyntax-only', HERE / 'Negative.cpp'], failure=True)
        report['negative_compile_refusals'] = len(refusals)
        workbench_host = build('WorkbenchHost', workbench / 'Host.cpp', native_link=True)
        output = run('execute-WorkbenchHost', [workbench_host])
        count('WorkbenchHost', output, 1647)
        timeout_phases('WorkbenchHost', output, workbench=True)
        workbench_noheap = build('WorkbenchNoHeap', workbench / 'NoHeap.cpp', native_link=True,
                                allocation_trap=True)
        output = run('execute-WorkbenchNoHeap', [workbench_noheap])
        count('WorkbenchNoHeap', output, report['executions']['WorkbenchHost'])
        timeout_phases('WorkbenchNoHeap', output, workbench=True)
        workbench_control = build('WorkbenchNoHeapControl', workbench / 'NoHeap.cpp',
                                  ['-DWORKBENCH_HEAP_CONTROL=1'], native_link=True, allocation_trap=True)
        output = run('execute-WorkbenchNoHeapControl', [workbench_control], expected=86)
        if 'workbench heap allocation refused' not in output:
            raise RuntimeError('Workbench allocation negative control did not reach its trap')
        report['workbench_noheap_negative_exit'] = 86
        command_host = build('WorkbenchCommand', workbench / 'Command.cpp', native_link=True)
        count('WorkbenchCommand', run('execute-WorkbenchCommand', [command_host]), 980)
        command_noheap = build('WorkbenchCommandNoHeap', workbench / 'CommandNoHeap.cpp',
                               native_link=True, allocation_trap=True)
        count('WorkbenchCommandNoHeap', run('execute-WorkbenchCommandNoHeap', [command_noheap]),
              report['executions']['WorkbenchCommand'])
        command_control = build('WorkbenchCommandNoHeapControl', workbench / 'CommandNoHeap.cpp',
                                ['-DWORKBENCH_COMMAND_HEAP_CONTROL=1'], native_link=True,
                                allocation_trap=True)
        output = run('execute-WorkbenchCommandNoHeapControl', [command_control], expected=86)
        if 'workbench heap allocation refused' not in output:
            raise RuntimeError('Command allocation negative control did not reach its trap')
        report['command_noheap_negative_exit'] = 86
        service_host = build('WorkbenchService', workbench / 'Service.cpp', native_link=True)
        count('WorkbenchService', run('execute-WorkbenchService', [service_host]), 1228)
        service_noheap = build('WorkbenchServiceNoHeap', workbench / 'ServiceNoHeap.cpp',
                               native_link=True, allocation_trap=True)
        count('WorkbenchServiceNoHeap', run('execute-WorkbenchServiceNoHeap', [service_noheap]),
              report['executions']['WorkbenchService'])
        service_control = build('WorkbenchServiceNoHeapControl', workbench / 'ServiceNoHeap.cpp',
                                ['-DWORKBENCH_SERVICE_HEAP_CONTROL=1'], native_link=True,
                                allocation_trap=True)
        output = run('execute-WorkbenchServiceNoHeapControl', [service_control], expected=86)
        if 'workbench heap allocation refused' not in output:
            raise RuntimeError('Service allocation negative control did not reach its trap')
        report['service_noheap_negative_exit'] = 86
        workbench_standalone = build('WorkbenchStandalone', workbench / 'Standalone.cpp')
        run('execute-WorkbenchStandalone', [workbench_standalone])
        workbench_positive = build('WorkbenchNegativePositiveControl', workbench / 'Negative.cpp')
        run('execute-WorkbenchNegativePositiveControl', [workbench_positive])
        workbench_refusals = ('COMMAND', 'SERVICE', 'CLIENT', 'CORE', 'RAW', 'INSPECTOR',
                             'NULL_CALLBACK', 'NULL_COMMAND_CALLBACK', 'NULL_SERVICE_CALLBACK')
        for macro in workbench_refusals:
            run('workbench-negative-' + macro, flags + ['-DWORKBENCH_' + macro, '-fsyntax-only',
                                                       workbench / 'Negative.cpp'], failure=True)
        report['workbench_negative_compile_refusals'] = len(workbench_refusals)
        if example.is_file():
            demo = build('Example', example, native_link=True)
            run('execute-Example', [demo])
            cmake, ninja = tool(args.cmake), tool(args.ninja)
            run('cmake', [cmake, '--version'])
            run('ninja', [ninja, '--version'])
            # A distinct spelling avoids colliding with the Example executable on NTFS.
            prefix, package, consumer = out / 'prefix', out / 'package', out / 'example-cmake'
            common = ['-G', 'Ninja', '-DCMAKE_MAKE_PROGRAM=' + str(ninja),
                      '-DCMAKE_CXX_COMPILER=' + str(compiler), '-DCMAKE_BUILD_TYPE=Release',
                      '-DCMAKE_CXX_FLAGS=' + ' '.join(str(flag) for flag in flags[1:] if flag != '-Ilib')]
            # Include switches are supplied by the exported targets, not by the source tree.
            run('configure-package', [cmake, '-S', ROOT, '-B', package, *common,
                                      '-DTELEMETRY_COMPONENTS=inspector;encoded_backend;encoded_server;core',
                                      '-DCMAKE_INSTALL_PREFIX=' + str(prefix)])
            run('build-package', [cmake, '--build', package, '-j2'])
            run('install-package', [cmake, '--install', package])
            run('configure-example', [cmake, '-S', example.parent, '-B', consumer, *common,
                                      '-DCMAKE_PREFIX_PATH=' + str(prefix)])
            run('build-installed-example', [cmake, '--build', consumer, '-j2'])
            ctest = tool(str(cmake.with_name('ctest' + cmake.suffix)))
            output = run('ctest-example', [ctest, '--test-dir', consumer, '--output-on-failure'])
            if not re.search(r'100% tests passed, 0 tests failed out of 3', output):
                raise RuntimeError('The installed Inspector example did not execute all three CTests')
            report['installed_example_ctests'] = 3
            report['cmake_sha256'], report['ninja_sha256'] = digest(cmake), digest(ninja)
        if args.arm_cxx:
            arm = tool(args.arm_cxx)
            run('arm-compiler', [arm, '--version'])
            image = out / 'inspector-arm.o'
            run('offline-arm-compile', [arm, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
                                        '-pedantic-errors', '-Ilib', '-mcpu=cortex-m7', '-mthumb',
                                        '-fno-exceptions', '-fno-rtti', '-fstack-usage',
                                        '-DTINY_DELEGATE_ENABLE_HEAP_FALLBACK=0',
                                        '-c', HERE / 'Standalone.cpp', '-o', image])
            nm = tool(str(arm).replace('g++', 'nm'))
            symbols = run('offline-arm-symbols', [nm, '-uC', image])
            if re.search(r'operator new|operator delete|\b(?:malloc|calloc|realloc|free)\b', symbols):
                raise RuntimeError('Offline ARM probe references heap allocation')
            report['offline_arm'] = dict(compiled=True, executed=False, allocation_symbols=0,
                                         stack_usage={path.name: path.read_text() for path in out.glob('*.su')})
            workbench_image = out / 'workbench-arm.o'
            run('workbench-offline-arm-compile', [arm, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
                                                 '-pedantic-errors', '-Ilib', '-mcpu=cortex-m7', '-mthumb',
                                                 '-fno-exceptions', '-fno-rtti', '-fstack-usage',
                                                 '-DTINY_DELEGATE_ENABLE_HEAP_FALLBACK=0',
                                                 '-c', workbench / 'Standalone.cpp', '-o', workbench_image])
            workbench_symbols = run('workbench-offline-arm-symbols', [nm, '-uC', workbench_image])
            if re.search(r'operator new|operator delete|\b(?:malloc|calloc|realloc|free)\b', workbench_symbols):
                raise RuntimeError('Offline Workbench probe references heap allocation')
            report['workbench_offline_arm'] = dict(compiled=True, executed=False, allocation_symbols=0,
                                                   stack_usage={path.name: path.read_text()
                                                                for path in out.glob('workbench*.su')})
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
    print('Inspector qualification passed:', report['executions'], flush=True)


if __name__ == '__main__':
    main()
