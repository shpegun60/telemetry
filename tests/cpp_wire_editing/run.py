#!/usr/bin/env python3
"""Generate and qualify the optional standalone C++ canonical editor writer.

All build, mutation, receipt and tool outputs belong in an external directory.
Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""
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

sys.dont_write_bytecode = True
HERE = Path(__file__).absolute().parent
ROOT = HERE.parents[1]
sys.path[:0] = [str(ROOT), str(HERE)]


def snapshot():
    files = [path for folder in (ROOT / 'tools/telemetry_codegen', ROOT / 'lib', HERE)
             for path in folder.rglob('*') if path.is_file() and path.suffix in
             {'.py', '.cpp', '.hpp', '.h', '.json'} and '__pycache__' not in path.parts]
    return {path.relative_to(ROOT).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted(files)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True)
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--arm-cxx')
    args = parser.parse_args()
    build = Path(args.build_dir).absolute()
    if build == ROOT or ROOT in build.parents:
        parser.error('use an external build directory')
    if build.exists():
        parser.error('use a fresh external build directory')
    build.mkdir(parents=True)
    report = {'passed': False, 'commands': [], 'source_sha256_start': snapshot(),
              'started_at_utc': datetime.now(timezone.utc).isoformat()}
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1')
    if args.sanitize:
        env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1')

    def run(command, name, expected=0):
        command = list(map(str, command))
        result = subprocess.run(command, cwd=build, env=env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        (build / (name + '.log')).write_text(result.stdout, encoding='utf-8')
        report['commands'].append({'name': name, 'argv': command, 'returncode': result.returncode})
        if result.returncode != expected:
            raise RuntimeError(name + ' failed: ' + result.stdout)
        return result.stdout

    flags = [args.cxx, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic-errors']
    if args.sanitize:
        flags += ['-g', '-fno-omit-frame-pointer', '-fsanitize=address,undefined', '-fno-sanitize-recover=all']
    suffix = '.exe' if os.name == 'nt' else ''
    try:
        report['tool_sha256'] = {}
        for name, command in (('host', args.cxx), ('python', sys.executable), ('arm', args.arm_cxx)):
            if command:
                tool = Path(shutil.which(command) or command).absolute()
                report['tool_sha256'][str(tool)] = hashlib.sha256(tool.read_bytes()).hexdigest()
                run([tool, '--version'], 'version-' + name)
        producer = build / ('producer' + suffix)
        run([*flags, '-I' + str(ROOT / 'lib'), '-I' + str(ROOT / 'lib/boost_pfr/include'),
             '-I' + str(ROOT / 'lib/magic_enum'), HERE / 'Producer.cpp',
             ROOT / 'lib/telemetry/abi/StructuredAbi.cpp', '-o', producer], 'compile-producer')
        descriptor = build / 'descriptor.bin'
        run([producer, descriptor], 'export-descriptor')
        from tools.telemetry_codegen import cli
        from tools.telemetry_codegen.schema import load_schema
        from Controls import check_controls
        schema = load_schema(descriptor.read_bytes(), 'wire_edit')
        report['cli_checks'] = check_controls(descriptor.read_bytes(), build)
        for name, preserving in (('baseline', False), ('sdk', True)):
            cli.publish(cli.render(schema, 'cpp', wire_editing=preserving), build / name, False)
        include = '-I' + str(build / 'sdk/cpp')
        report['checks'] = {}
        for source in ('Host.cpp', 'NoHeap.cpp'):
            executable = build / (source[:-4] + suffix)
            run([*flags, include, HERE / source, '-o', executable], 'compile-' + source[:-4])
            output = run([executable], source[:-4])
            match = re.search(r'checks=(\d+) failures=0', output)
            if not match or int(match[1]) < 200:
                raise RuntimeError('missing meaningful successful writer checks')
            report['checks'][source] = int(match[1])
        for number in range(1, 7):
            run([build / ('NoHeap' + suffix), number], 'allocation-control-' + str(number), 86)
        # Compile the actual public helper against default output: it must be absent.
        result = subprocess.run([*map(str, flags), '-I' + str(build / 'baseline/cpp'),
                                 str(HERE / 'Arm.cpp'), '-c', '-o', str(build / 'baseline.o')],
                                cwd=build, env=env, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=180)
        (build / 'baseline-absence.log').write_text(result.stdout, encoding='utf-8')
        if result.returncode == 0 or 'encodePreserving' not in result.stdout:
            raise RuntimeError('default output unexpectedly exposes helper')
        report['baseline_absence'] = True
        header = build / 'sdk/cpp/wire_edit/Codec.hpp'
        for name, before, after in (
                ('preservation', 'out.integer(old);', 'out.integer(bits);'),
                ('canonical', 'if (!codec_detail::Adapter<T>::valid(validator)) return Status::InvalidValue;',
                 '(void)codec_detail::Adapter<T>::valid(validator);')):
            mutated = build / ('mutated-' + name)
            shutil.copytree(build / 'sdk/cpp', mutated)
            target = mutated / 'wire_edit/Codec.hpp'
            content = header.read_text()
            # Canonical validation occurs in encodePreserving and decode; mutate only the first.
            if content.count(before) != (2 if name == 'canonical' else 1):
                raise RuntimeError('mutation anchor changed')
            target.write_text(content.replace(before, after, 1))
            executable = build / ('mutated-' + name + '-run' + suffix)
            run([*flags, '-I' + str(mutated), HERE / 'Host.cpp', '-o', executable], 'compile-mutated-' + name)
            output = run([executable], 'mutated-' + name, 1)
            if 'Host.cpp:' not in output:
                raise RuntimeError('mutation did not reach actual byte/refusal oracle')
        if args.arm_cxx:
            nm = shutil.which('arm-none-eabi-nm')
            if not nm:
                raise RuntimeError('arm-none-eabi-nm is required')
            report['arm'] = {}
            for optimization in ('Os', 'O2'):
                obj = build / ('arm-' + optimization + '.o')
                run([args.arm_cxx, '-std=c++20', '-' + optimization, '-mcpu=cortex-m7', '-mthumb',
                     '-mfpu=fpv5-d16', '-mfloat-abi=hard', '-fno-exceptions', '-fno-rtti',
                     '-fstack-usage', '-ffunction-sections', '-fdata-sections', '-Wall', '-Wextra',
                     '-Werror', '-pedantic-errors', include, '-c', HERE / 'Arm.cpp', '-o', obj], 'compile-arm-' + optimization)
                symbols = run([nm, '-C', obj], 'symbols-arm-' + optimization)
                if re.search(r'operator new|operator delete|\b(?:malloc|calloc|realloc|free|__cxa_atexit|__cxa_guard_acquire)\b', symbols):
                    raise RuntimeError('allocation or static-lifetime symbol in ARM root')
                report['arm'][optimization] = hashlib.sha256(obj.read_bytes()).hexdigest()
        report['source_sha256_end'] = snapshot()
        if report['source_sha256_end'] != report['source_sha256_start']:
            raise RuntimeError('source inputs changed during qualification')
        report['passed'] = True
        print(json.dumps({'passed': True, 'checks': report['checks'], 'cli_checks': report['cli_checks']}))
    finally:
        report['source_sha256_end'] = snapshot()
        (build / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
