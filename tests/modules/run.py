#!/usr/bin/env python3
"""Build sealed modules, relocate their installation and exercise downstream consumers. MIT."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
MODULES = ('core', 'resource', 'resource_protocol', 'client', 'resource_adapter',
           'encoded_server', 'rpc_server', 'rpc_sessions', 'encoded_backend', 'rpc_client', 'rpc_backend')
FORBIDDEN = {
    'core': ('resource/Resource.hpp', 'client/DeviceClient.hpp', 'wire/EncodedWire.hpp', 'wire/RpcWire.hpp'),
    'resource': ('telemetry/Telemetry.hpp', 'client/DeviceClient.hpp'),
    'resource_protocol': ('telemetry/Telemetry.hpp', 'client/DeviceClient.hpp'),
    'client': ('telemetry/Telemetry.hpp', 'resource/Resource.hpp', 'wire/EncodedWire.hpp', 'wire/RpcWire.hpp'),
    'encoded_server': ('resource/Resource.hpp', 'client/DeviceClient.hpp'),
    'encoded_backend': ('telemetry/Telemetry.hpp', 'resource/Resource.hpp'),
    'rpc_client': ('telemetry/model/Model.hpp', 'integration/telemetry/v3/RpcMailbox.hpp'),
    'rpc_backend': ('telemetry/model/Model.hpp', 'integration/telemetry/v3/RpcMailbox.hpp'),
    'rpc_server': ('client/DeviceClient.hpp',),
    'resource_adapter': ('client/DeviceClient.hpp',),
    'rpc_sessions': ('client/DeviceClient.hpp',),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True, type=Path)
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--arm', action='store_true', help='Offline ARM compilation and linking; never execute images')
    parser.add_argument('--modules', nargs='+', choices=MODULES, default=MODULES)
    args = parser.parse_args()
    if args.arm and args.sanitize:
        parser.error('ARM and runtime sanitizers are separate runs')
    out = args.build_dir.resolve()
    if out == ROOT or ROOT in out.parents:
        parser.error('Use an external build directory')
    out.mkdir(parents=True, exist_ok=True)
    report = {'passed': False, 'commands': [], 'modules': {}}

    def snapshot():
        paths = [ROOT / 'CMakeLists.txt']
        for folder in ('lib', 'cmake', 'tests/modules'):
            paths += [path for path in (ROOT / folder).rglob('*')
                      if path.is_file() and '__pycache__' not in path.parts]
        return {path.relative_to(ROOT).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in sorted(paths)}

    report['source_sha256'] = snapshot()
    environment = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                       UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')

    def run(command, label, expected_failure=False):
        start = time.monotonic()
        completed = subprocess.run(list(map(str, command)), cwd=out, env=environment,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=600)
        (out / (label + '.log')).write_text(completed.stdout, encoding='utf-8')
        report['commands'].append({'name': label, 'command': list(map(str, command)),
                                   'exit': completed.returncode, 'seconds': time.monotonic() - start})
        if (completed.returncode == 0) == expected_failure:
            raise RuntimeError(label + '\n' + completed.stdout)
        return completed.stdout

    flags = '-Wall -Wextra -Werror -pedantic-errors'
    extra_config = []
    if args.arm:
        flags += ' -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -fno-exceptions -fno-rtti -DTINY_DELEGATE_ENABLE_HEAP_FALLBACK=0'
        extra_config = ['-DCMAKE_SYSTEM_NAME=Generic', '-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY',
                        '-DCMAKE_EXE_LINKER_FLAGS=-specs=nano.specs -specs=nosys.specs -nostartfiles -Wl,-e,main']
    if args.sanitize:
        flags += ' -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
    try:
        for module in args.modules:
            directory = out / module
            build, prefix = directory / 'build', directory / 'install'
            run([args.cmake, '-S', ROOT, '-B', build, '-G', 'Ninja',
                 '-DTELEMETRY_COMPONENTS=' + module, '-DCMAKE_CXX_COMPILER=' + args.cxx,
                 '-DCMAKE_CXX_FLAGS=' + flags, '-DCMAKE_BUILD_TYPE=Release',
                 '-DCMAKE_INSTALL_PREFIX=' + str(prefix), *extra_config], module + '-configure')
            run([args.cmake, '--build', build, '-j2'], module + '-build')
            run([args.cmake, '--install', build], module + '-install')
            staged = build / 'module-headers' / module / 'removed-header.hpp'
            staged.write_text('#error obsolete staged header\n')
            installed = prefix / 'include' / ('telemetry-' + module) / 'removed-header.hpp'
            installed.write_text('#error obsolete installed header\n')
            run([args.cmake, '-S', ROOT, '-B', build], module + '-reconfigure')
            if staged.exists():
                raise RuntimeError(module + ': obsolete staged header survived reconfigure')
            run([args.cmake, '--install', build], module + '-upgrade-install')
            if installed.exists():
                raise RuntimeError(module + ': obsolete installed header survived upgrade')
            # A new prefix and absent original installation catch absolute export paths.
            relocated = directory / 'relocated'
            if relocated.exists():
                raise RuntimeError('Use a fresh build directory: ' + str(relocated))
            shutil.move(prefix, relocated)
            headers = {path.relative_to(parent).as_posix() for parent in (relocated / 'include').iterdir()
                       for path in parent.rglob('*') if path.is_file()}
            licenses = [('LICENSE', 'LICENSE')]
            for header, source, installed_license in (
                    ('delegate/tiny_delegate.hpp', 'lib/delegate/LICENSE', 'dependencies/delegate/LICENSE'),
                    ('magic_enum.hpp', 'lib/magic_enum/LICENSE', 'dependencies/magic_enum/LICENSE'),
                    ('boost/pfr.hpp', 'lib/boost_pfr/LICENSE_1_0.txt', 'dependencies/boost_pfr/LICENSE_1_0.txt')):
                if header in headers:
                    licenses.append((source, installed_license))
            for source, installed_license in licenses:
                installed_license = relocated / 'share/telemetry' / installed_license
                if not installed_license.is_file() or installed_license.read_bytes() != (ROOT / source).read_bytes():
                    raise RuntimeError(module + ': missing or changed installed license: ' + source)
            for forbidden in FORBIDDEN[module]:
                if forbidden in headers:
                    raise RuntimeError(module + ': installed forbidden header ' + forbidden)
            consumer = directory / 'consumer'
            consumer.mkdir(parents=True)
            shutil.copy2(HERE / 'Probe.cpp', consumer / 'main.cpp')
            code = ['cmake_minimum_required(VERSION 3.20)', 'project(Consumer LANGUAGES CXX)',
                    'set(CMAKE_EXPORT_COMPILE_COMMANDS ON)',
                    'find_package(Telemetry REQUIRED COMPONENTS ' + module + ')',
                    'add_executable(probe main.cpp)',
                    'target_compile_definitions(probe PRIVATE MODULE_' + module.upper() + ')',
                    'target_link_libraries(probe PRIVATE Telemetry::' + module + ')',
                    'include(CheckCXXSourceCompiles)',
                    'set(CMAKE_REQUIRED_LIBRARIES Telemetry::' + module + ')',
                    'set(CMAKE_REQUIRED_FLAGS "-std=c++20")',
                    'check_cxx_source_compiles("#include <cstddef>\nint main() { std::byte value{}; (void)value; }" MODULE_CONTROL)',
                    'if(NOT MODULE_CONTROL)', 'message(FATAL_ERROR "Positive compile control failed")', 'endif()']
            for index, header in enumerate(FORBIDDEN[module]):
                code += ['check_cxx_source_compiles("#include <' + header + '>\nint main() { return 0; }" LEAK_' + str(index) + ')',
                         'if(LEAK_' + str(index) + ')', 'message(FATAL_ERROR "Dependency leaked: ' + header + '")', 'endif()']
            (consumer / 'CMakeLists.txt').write_text('\n'.join(code) + '\n', encoding='utf-8')
            downstream = directory / 'downstream'
            run([args.cmake, '-S', consumer, '-B', downstream, '-G', 'Ninja',
                 '-DCMAKE_CXX_COMPILER=' + args.cxx, '-DCMAKE_CXX_FLAGS=' + flags,
                 '-DCMAKE_PREFIX_PATH=' + str(relocated), *extra_config], module + '-downstream-configure')
            run([args.cmake, '--build', downstream, '-j2'], module + '-downstream-build')
            if not args.arm:
                run([downstream / ('probe.exe' if os.name == 'nt' else 'probe')], module + '-execute')
            commands = (downstream / 'compile_commands.json').read_text()
            if str(ROOT) in commands or str(build) in commands:
                raise RuntimeError(module + ': consumer can see source or build tree')
            # Ensure a required compiled module is genuinely linked, not only parsed.
            if not args.arm and module in ('core', 'rpc_client', 'resource_protocol'):
                compile_command = json.loads(commands)[0]
                import shlex
                command = shlex.split(compile_command['command'])
                object_path = directory / 'link-control.o'
                index = command.index('-o')
                command[index + 1] = str(object_path)
                run(command, module + '-link-control-compile')
                text = run([args.cxx, object_path, '-o', directory / 'missing-library'],
                           module + '-negative-link', expected_failure=True)
                expected = {'core': 'dispatchEncoded', 'rpc_client': 'RpcClient', 'resource_protocol': 'process'}[module]
                if expected not in text:
                    raise RuntimeError('Link rejection did not identify the missing module: ' + text)
            report['modules'][module] = {'headers': len(headers), 'negative_includes': len(FORBIDDEN[module]),
                                          'relocated_consumer': True, 'executed': not args.arm}
            print(module + ': passed', flush=True)
        report['source_sha256_after'] = snapshot()
        if report['source_sha256_after'] != report['source_sha256']:
            raise RuntimeError('Source inputs changed during qualification')
        report['passed'] = True
    finally:
        (out / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
