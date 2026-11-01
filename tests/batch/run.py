#!/usr/bin/env python3
"""Qualify optional Mixed Batch through a relocated SDK; no hardware access. MIT."""
import argparse
from datetime import datetime, timezone
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


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True, type=Path)
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--ninja', default='ninja')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--arm', action='store_true')
    args = parser.parse_args()
    out = args.build_dir.resolve()
    if out == ROOT or ROOT in out.parents or out in ROOT.parents:
        parser.error('Use a dedicated external output directory')
    if out.exists() and (not out.is_dir() or any(out.iterdir())):
        parser.error('Use a fresh empty output directory; nothing is deleted')
    if args.arm and args.sanitize:
        parser.error('Sanitizers run on the host only')
    tools = {}
    for name in ('cxx', 'cmake', 'ninja'):
        value = getattr(args, name)
        resolved = shutil.which(value)
        if resolved is None and not Path(value).is_file():
            parser.error('Tool not found: ' + value)
        # clang++ is a C++ driver even when its symlink target is named clang.
        # Preserve argv[0]'s driver identity; digest() follows the symlink.
        tools[name] = Path(os.path.abspath(resolved or value))
    out.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1')
    env['PATH'] = str(tools['cxx'].parent) + os.pathsep + env.get('PATH', '')
    env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
               UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    report = dict(passed=False, hardware=False, arm=args.arm, sanitize=args.sanitize,
                  installed_consumer=True, started_utc=datetime.now(timezone.utc).isoformat(),
                  commands=[], executions={}, profiles=[])

    def save():
        (out / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')

    def run(name, command, expected=0):
        command = list(map(str, command))
        started = time.monotonic()
        try:
            result = subprocess.run(command, cwd=ROOT, env=env, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, timeout=600)
            output, code = result.stdout, result.returncode
        except subprocess.TimeoutExpired as error:
            output, code = error.stdout or b'', None
        log = out / (name + '.log')
        log.write_bytes(output)
        report['commands'].append(dict(name=name, command=command, exit=code, expected=expected,
            seconds=time.monotonic()-started, log=log.name, sha256=digest(log)))
        save()
        print(name + ': exit ' + str(code), flush=True)
        if code is None or (code == 0 if expected == 'nonzero' else code != expected):
            raise RuntimeError(name + ' failed\n' + output.decode(errors='replace')[-12000:])
        return output.decode('utf-8', errors='strict')

    def source_snapshot():
        paths = [ROOT/'CMakeLists.txt', ROOT/'.github/workflows/ci.yml']
        for folder in ('lib', 'cmake', 'tests/batch', 'examples/batch'):
            paths += [path for path in (ROOT/folder).rglob('*') if path.is_file() and
                      path.suffix in {'.hpp', '.h', '.cpp', '.py', '.txt', '.cmake', '.in', '.pri'}]
        paths.append(ROOT/'examples/read_many/Model.hpp')
        paths.append(ROOT/'tests/dynamic_client/Fixture.hpp')
        return {path.relative_to(ROOT).as_posix(): digest(path) for path in sorted(paths)}

    try:
        report['source_sha256_before'] = source_snapshot()
        report['source_head'] = run('head', ['git', 'rev-parse', 'HEAD']).strip()
        report['source_status'] = run('status', ['git', 'status', '--porcelain'])
        for name, tool in tools.items():
            run(name+'-version', [tool, '--version'])
        report['tools'] = {name: dict(path=str(path), sha256=digest(path)) for name, path in tools.items()}
        for opt in (('Os', 'O2') if args.arm else ('O1' if args.sanitize else 'O2',)):
            root = out/opt
            package, prefix, moved = root/'package', root/'install', root/'relocated'
            flags = '-Wall -Wextra -Werror -pedantic-errors -fstack-usage -DTINY_DELEGATE_ENABLE_HEAP_FALLBACK=0'
            common = ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
                '-DCMAKE_CXX_COMPILER='+str(tools['cxx']), '-DCMAKE_MAKE_PROGRAM='+str(tools['ninja']),
                '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', '-DCMAKE_CXX_FLAGS_RELEASE=-'+opt+' -DNDEBUG']
            if args.arm:
                flags += ' -mcpu=cortex-m7 -mthumb -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections'
                common += ['-DCMAKE_SYSTEM_NAME=Generic', '-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY']
            elif args.sanitize:
                flags += ' -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
                common += ['-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined']
            common += ['-DCMAKE_CXX_FLAGS='+flags]
            components = 'batch_client;batch_server;encoded_backend' + ('' if args.arm else ';rpc_backend;rpc_sessions')
            run(opt+'-package-configure', [tools['cmake'], '-S', ROOT, '-B', package, *common,
                '-DTELEMETRY_COMPONENTS='+components, '-DCMAKE_INSTALL_PREFIX='+str(prefix)])
            run(opt+'-package-build', [tools['cmake'], '--build', package, '-j2'])
            run(opt+'-package-install', [tools['cmake'], '--install', package])
            prefix.rename(moved)
            installed = {path.relative_to(moved).as_posix(): digest(path)
                         for path in moved.rglob('*') if path.is_file()}
            build = root/'consumer'
            run(opt+'-consumer-configure', [tools['cmake'], '-S', HERE, '-B', build, *common,
                '-DTelemetry_DIR='+str(moved/'lib/cmake/Telemetry'),
                '-DBATCH_ARM_PROBE='+('ON' if args.arm else 'OFF')])
            run(opt+'-consumer-build', [tools['cmake'], '--build', build, '-j2'])
            for entry in json.loads((build/'compile_commands.json').read_text()):
                command = entry['command'].replace('\\', '/').lower()
                if (ROOT/'lib').as_posix().lower() in command or (moved/'include').as_posix().lower() not in command:
                    raise RuntimeError('Consumer escaped relocated installed include roots')
            if args.arm:
                arm_tools = {name: tools['cxx'].with_name('arm-none-eabi-'+name+tools['cxx'].suffix)
                             for name in ('size', 'nm', 'objdump')}
                objects = list(build.rglob('ArmClient.cpp.o')) + list(build.rglob('ArmClient.cpp.obj'))
                if len(objects) != 1:
                    raise RuntimeError('Missing unique client API object probe')
                symbols = run(opt+'-client-symbols', [arm_tools['nm'], '-uC', objects[0]])
                if re.search(r'operator new|operator delete|\b(malloc|calloc|realloc|free)\b', symbols):
                    raise RuntimeError('Allocation symbols in client probe')
                report[opt+'_client_probe'] = dict(compiled=True, executed=False, object_sha256=digest(objects[0]))
                for mode in ('legacy', 'batch'):
                    binary = build/('batch-'+mode)
                    sections = run(opt+'-'+mode+'-size', [arm_tools['size'], '-A', binary])
                    symbols = run(opt+'-'+mode+'-nm', [arm_tools['nm'], '-SC', binary])
                    run(opt+'-'+mode+'-objdump', [arm_tools['objdump'], '-drwC', binary])
                    if re.search(r'operator new|\b(malloc|calloc|realloc|_sbrk)\b', symbols):
                        raise RuntimeError('Allocation symbols in linked root')
                    if ('telemetry::batchEncoded(' in symbols) != (mode == 'batch'):
                        raise RuntimeError('Optional executor leaked into legacy or was discarded')
                    sizes = {m[1]: int(m[2]) for m in re.finditer(r'^\s*(\.\S+)\s+(\d+)\s+\d+', sections, re.M)}
                    if not sizes.get('.text'):
                        raise RuntimeError('Missing linked text')
                    report['profiles'].append(dict(optimization=opt, mode=mode, sections=sizes,
                        binary_sha256=digest(binary), note='Selected roots, not firmware delta or peak stack'))
            else:
                suffix = '.exe' if os.name == 'nt' else ''
                for name, floor in (('Wire', 15327), ('Server', 87), ('Client', 436),
                                    ('CrossGroup', 200), ('NoHeap', 436)):
                    output = run('execute-'+name, [build/('batch-'+name+suffix)])
                    match = re.search(r'(?:checks=|: )(\d+)(?: checks, 0 failures| failures=0)', output)
                    if not match or int(match[1]) < floor:
                        raise RuntimeError(name+': verification count floor not reached')
                    report['executions'][name] = int(match[1])
                for control in ('new', 'aligned_new', 'malloc', 'calloc', 'realloc', 'free'):
                    run('noheap-'+control, [build/('batch-NoHeap'+suffix), control], expected=86)
                report['negative_allocation_controls'] = 6
                includes = [arg for path in sorted((moved/'include').iterdir()) for arg in ('-I', path)]
                for macro, marker in (('BATCH_CALLBACK_TOO_SMALL', 'CallbackBytes'), ('BATCH_FORGE', 'private')):
                    output = run('negative-'+macro, [tools['cxx'], '-std=c++20', *includes,
                        '-D'+macro, '-fsyntax-only', HERE/'Client.cpp'], expected='nonzero')
                    if marker not in output:
                        raise RuntimeError('Unrelated negative compile failure: '+macro)
                example = root/'example'
                run('example-configure', [tools['cmake'], '-S', ROOT/'examples/batch', '-B', example,
                    *common, '-DTelemetry_DIR='+str(moved/'lib/cmake/Telemetry')])
                run('example-build', [tools['cmake'], '--build', example, '-j2'])
                output = run('example-execute', [example/('batch-host'+suffix)])
                if 'Service=28' not in output or 'Read=100' not in output:
                    raise RuntimeError('Example did not confirm Service=28 and Read=100')
                report['example'] = True
            usage = sorted(root.rglob('*.su'))
            if not usage:
                raise RuntimeError('Missing compiler stack records')
            report[opt+'_stack_usage'] = {path.relative_to(out).as_posix(): digest(path) for path in usage}
            report[opt+'_installed'] = installed
            if installed != {path.relative_to(moved).as_posix(): digest(path)
                             for path in moved.rglob('*') if path.is_file()}:
                raise RuntimeError('Installed package changed during qualification')
        report['source_sha256_after'] = source_snapshot()
        if report['source_sha256_before'] != report['source_sha256_after']:
            raise RuntimeError('Source inputs changed during qualification')
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        report['finished_utc'] = datetime.now(timezone.utc).isoformat()
        save()
    print('Mixed Batch passed:', report['executions'], report['profiles'], flush=True)


if __name__ == '__main__':
    main()
