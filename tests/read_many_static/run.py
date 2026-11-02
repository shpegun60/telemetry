#!/usr/bin/env python3
"""Static ReadMany through generated SDKs and relocated installed packages. MIT."""
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
import time

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--producer-cxx', default='g++', help='Host compiler for the native Descriptor producer')
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--ninja', default='ninja')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--arm', action='store_true', help='Offline compilation/link only; select the intended ARM compiler explicitly')
    args = parser.parse_args()
    out = args.build_dir.resolve()
    if out == ROOT or ROOT in out.parents or out in ROOT.parents:
        parser.error('Use a dedicated external output directory')
    if out.exists() and (not out.is_dir() or any(out.iterdir())):
        parser.error('Use a fresh empty output directory; nothing is deleted')
    if args.arm and args.sanitize:
        parser.error('Sanitizers execute only on the host')
    out.mkdir(parents=True, exist_ok=True)
    tools = {name: Path(shutil.which(getattr(args, name)) or getattr(args, name)).absolute()
             for name in ('cxx', 'cmake', 'ninja', 'producer_cxx')}
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1',
               ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
               UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    env['PATH'] = str(tools['cxx'].parent) + os.pathsep + env.get('PATH', '')
    report = dict(passed=False, hardware=False, arm=args.arm, sanitize=args.sanitize,
                  installed_consumer=True, commands=[], executions={}, executables={}, profiles=[],
                  started_utc=datetime.now(timezone.utc).isoformat())

    def save():
        (out / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')

    def run(name, command, expected=0):
        command = list(map(str, command))
        start = time.monotonic()
        try:
            result = subprocess.run(command, cwd=ROOT, env=env, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, timeout=600)
            output, code = result.stdout, result.returncode
        except subprocess.TimeoutExpired as error:
            output, code = error.stdout or b'', None
        log = out / (name + '.log')
        log.write_bytes(output)
        report['commands'].append(dict(name=name, command=command, exit=code, expected=expected,
                                       seconds=time.monotonic()-start, log=log.name, sha256=digest(log)))
        save()
        print(name + ': exit ' + str(code), flush=True)
        if code is None or (code == 0 if expected == 'nonzero' else code != expected):
            raise RuntimeError(name + ' failed\n' + output.decode(errors='replace')[-12000:])
        return output.decode('utf-8', errors='strict')

    def snapshot():
        paths = [ROOT/'CMakeLists.txt']
        for folder in ('lib', 'cmake', 'tests/read_many_static', 'tools/telemetry_codegen'):
            paths += [path for path in (ROOT/folder).rglob('*') if path.is_file() and
                      path.suffix in {'.hpp', '.h', '.cpp', '.py', '.txt', '.cmake', '.in', '.pri'}]
        return {path.relative_to(ROOT).as_posix(): digest(path) for path in sorted(paths)}

    def common(cxx, optimization, arm=False, sanitize=False):
        flags = '-Wall -Wextra -Werror -pedantic-errors -fstack-usage -DTINY_DELEGATE_ENABLE_HEAP_FALLBACK=0'
        options = ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
                   '-DCMAKE_CXX_COMPILER='+str(cxx), '-DCMAKE_MAKE_PROGRAM='+str(tools['ninja']),
                   '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
                   '-DCMAKE_CXX_FLAGS_RELEASE=-'+optimization+' -DNDEBUG']
        if arm:
            flags += ' -mcpu=cortex-m7 -mthumb -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections'
            options += ['-DCMAKE_SYSTEM_NAME=Generic', '-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY']
        elif sanitize:
            flags += ' -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
            options += ['-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined']
        return [*options, '-DCMAKE_CXX_FLAGS='+flags]

    def package(name, options, components):
        directory = out/name
        prefix = directory/'prefix'
        run(name+'-configure', [tools['cmake'], '-S', ROOT, '-B', directory/'build', *options,
                               '-DTELEMETRY_COMPONENTS='+components, '-DCMAKE_INSTALL_PREFIX='+str(prefix)])
        run(name+'-build', [tools['cmake'], '--build', directory/'build', '-j2'])
        run(name+'-install', [tools['cmake'], '--install', directory/'build'])
        relocated = directory/'relocated'
        prefix.rename(relocated)
        return relocated

    def config(prefix):
        return '-DTelemetry_DIR='+str(prefix/'lib/cmake/Telemetry')

    def check_includes(build, prefix):
        entries = json.loads((build/'compile_commands.json').read_text())
        for entry in entries:
            command = entry['command'].replace('\\', '/').lower()
            if (ROOT/'lib').as_posix().lower() in command or (prefix/'include').as_posix().lower() not in command:
                raise RuntimeError('Consumer escaped relocated installed include roots')

    suffix = '.exe' if os.name == 'nt' else ''
    try:
        report['source_sha256_before'] = snapshot()
        report['source_head'] = run('head', ['git', 'rev-parse', 'HEAD']).strip()
        report['source_status'] = run('status', ['git', 'status', '--porcelain'])
        for name, tool in tools.items():
            run(name+'-version', [tool, '--version'])
        report['tool_sha256'] = {name: digest(path.resolve()) for name, path in tools.items()}
        producer_options = common(tools['producer_cxx'], 'O2')
        host_prefix = package('host-package', producer_options, 'core;encoded_server')
        producer = out/'producer'
        run('producer-configure', [tools['cmake'], '-S', HERE, '-B', producer,
                                  *producer_options, config(host_prefix), '-DSTATIC_MANY_PRODUCER_ONLY=ON'])
        run('producer-build', [tools['cmake'], '--build', producer, '-j2'])
        descriptor = out/'descriptor.bin'
        run('producer-execute', [producer/('static-many-descriptor'+suffix), descriptor])
        generated = out/'generated'
        run('generate', [sys.executable, '-B', '-m', 'tools.telemetry_codegen', 'generate', descriptor,
                         '--targets', 'cpp', '--namespace', 'static_many_api', '--output', generated])
        run('generate-check', [sys.executable, '-B', '-m', 'tools.telemetry_codegen', 'generate', descriptor,
                               '--targets', 'cpp', '--namespace', 'static_many_api', '--output', generated, '--check'])
        report['descriptor_sha256'] = digest(descriptor)
        report['generated_sha256'] = {path.relative_to(generated).as_posix(): digest(path)
                                      for path in generated.rglob('*') if path.is_file()}
        for optimization in (('Os', 'O2') if args.arm else ('O1' if args.sanitize else 'O2',)):
            options = common(tools['cxx'], optimization, args.arm, args.sanitize)
            prefix = package(optimization+'-client-package', options, 'read_many_static_client;encoded_backend')
            root = out/optimization
            root.mkdir()
            standalone = root/'standalone'
            run(optimization+'-standalone-configure', [tools['cmake'], '-S', HERE, '-B', standalone,
                *options, config(prefix), '-DSTATIC_MANY_GENERATED='+str(generated/'cpp'),
                '-DSTATIC_MANY_STANDALONE_ONLY=ON', '-DSTATIC_MANY_ARM='+('ON' if args.arm else 'OFF')])
            run(optimization+'-standalone-build', [tools['cmake'], '--build', standalone, '-j2'])
            check_includes(standalone, prefix)
            includes = [arg for path in sorted((prefix/'include').iterdir()) for arg in ('-I', path)]
            includes += ['-I', generated/'cpp']
            dependencies = run(optimization+'-dependencies', [tools['cxx'], '-std=c++20', *includes,
                                      '-MM', HERE/('Arm.cpp' if args.arm else 'Standalone.cpp')])
            normalized = dependencies.replace('\\', '/').lower()
            if re.search(r'telemetry/runtime/|integration/(dynamic|inspector|workbench)/|telemetry/model/|qtcore|qtqml', normalized):
                raise RuntimeError('Headless static consumer imported runtime/native Model/Qt')
            report[optimization+'_installed'] = {path.relative_to(prefix).as_posix(): digest(path)
                                                 for path in prefix.rglob('*') if path.is_file()}
            if args.arm:
                binary = standalone/'static-many-arm'
                arm_tools = {name: tools['cxx'].with_name('arm-none-eabi-'+name+tools['cxx'].suffix)
                             for name in ('size', 'nm', 'objdump')}
                symbols = run(optimization+'-symbols', [arm_tools['nm'], '-SC', binary])
                if re.search(r'operator new|operator delete|\b(malloc|calloc|realloc|free|_sbrk)\b', symbols):
                    raise RuntimeError('Selected ARM root references allocation')
                sizes = run(optimization+'-size', [arm_tools['size'], '-A', binary])
                run(optimization+'-objdump', [arm_tools['objdump'], '-drwC', binary])
                report['profiles'].append(dict(optimization=optimization, executed=False,
                    binary_sha256=digest(binary), sections={m[1]:int(m[2]) for m in
                    re.finditer(r'^\s*(\.\S+)\s+(\d+)\s+\d+', sizes, re.M)},
                    note='Offline linked selected root; compiler frames are not physical peak stack'))
            else:
                executable = standalone/('static-many-standalone'+suffix)
                run('standalone-execute', [executable])
                report['executables']['Standalone'] = dict(path=executable.relative_to(out).as_posix(),
                    sha256=digest(executable), bytes=executable.stat().st_size)
                full_prefix = package(optimization+'-full-package', options,
                                      'read_many_static_client;encoded_backend;encoded_server;core')
                consumer = root/'consumer'
                run('consumer-configure', [tools['cmake'], '-S', HERE, '-B', consumer, *options,
                                           config(full_prefix), '-DSTATIC_MANY_GENERATED='+str(generated/'cpp')])
                run('consumer-build', [tools['cmake'], '--build', consumer, '-j2'])
                check_includes(consumer, full_prefix)
                for name in ('Host', 'NoHeap', 'Example'):
                    executable = consumer/('static-many-'+name+suffix)
                    output = run('execute-'+name, [executable])
                    report['executables'][name] = dict(path=executable.relative_to(out).as_posix(),
                        sha256=digest(executable), bytes=executable.stat().st_size)
                    matches = re.findall(r'checks=(\d+) failures=0', output)
                    if len(matches) != 1 or int(matches[0]) < (1500 if name in ('Host', 'NoHeap') else 11):
                        raise RuntimeError('Missing counted result for '+name)
                    report['executions'][name] = int(matches[0])
                for control in ('new', 'aligned_new', 'malloc', 'calloc', 'realloc', 'free'):
                    run('noheap-'+control, [consumer/('static-many-NoHeap'+suffix), control], expected=86)
                report['negative_allocation_controls'] = 6
                for macro, marker in NEGATIVE_CONTROLS:
                    output = run('negative-'+macro, [tools['cxx'], '-std=c++20', *includes,
                                 '-D'+macro, '-fsyntax-only', HERE/'Negative.cpp'], expected='nonzero')
                    if marker not in output:
                        raise RuntimeError('Unrelated compile refusal for '+macro)
                report['negative_compile_controls'] = len(NEGATIVE_CONTROLS)
            usage = sorted(out.rglob('*.su'))
            if not usage:
                raise RuntimeError('Missing compiler stack records')
            report[optimization+'_stack_usage'] = {path.relative_to(out).as_posix(): digest(path) for path in usage}
        report['source_sha256_after'] = snapshot()
        if report['source_sha256_before'] != report['source_sha256_after']:
            raise RuntimeError('Source inputs changed during qualification')
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        report['finished_utc'] = datetime.now(timezone.utc).isoformat()
        save()
    print('Static ReadMany passed:', report['executions'], report['profiles'], flush=True)


NEGATIVE_CONTROLS = (
    ('STATIC_MANY_STRUCT', 'fields'), ('STATIC_MANY_COMMAND', 'fields'),
    ('STATIC_MANY_EMPTY', 'fields'), ('STATIC_MANY_33', 'fields'),
    ('STATIC_MANY_THROWING_CALL', 'noexcept'), ('STATIC_MANY_THROWING_COPY', 'ownership'),
    ('STATIC_MANY_CALLBACK_SMALL', 'CallbackBytes'), ('STATIC_MANY_FORGE', 'private'),
    ('STATIC_MANY_VALUE_BYPASS', 'private'), ('STATIC_MANY_WRONG_CALLBACK', 'completion'),
    ('STATIC_MANY_WIDE_ID', 'fields'), ('STATIC_MANY_WRONG_WIDTH', 'native scalar widths'),
    ('STATIC_MANY_NO_FINGERPRINT', 'Codec::fingerprint'),
)

if __name__ == '__main__':
    main()
