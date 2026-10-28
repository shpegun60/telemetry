#!/usr/bin/env python3
"""Build the optional serial application and qualify it on host PTYs without hardware (MIT)."""
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

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sources(cobs):
    result = {}
    for base, label, directories in ((ROOT, '', (HERE, ROOT / 'examples/inspector_qml',
            ROOT / 'examples/inspector_serial', ROOT / 'lib', ROOT / 'cmake')),
            (cobs, 'COBS_ROOT/', (cobs / 'src',))):
        for directory in directories:
            for parent, names, files in os.walk(directory):
                names[:] = [name for name in names if name not in ('.git', '__pycache__', 'build')
                            and (not label or name != 'tests')]
                for name in files:
                    path = Path(parent) / name
                    if path.suffix in ('.h', '.hpp', '.cpp', '.py', '.txt', '.cmake', '.qml'):
                        result[label + path.relative_to(base).as_posix()] = digest(path)
    for path in (ROOT / 'CMakeLists.txt', ROOT / '.github/workflows/ci.yml',
                 ROOT / 'examples/nucleo_h7s3l8/Protocol.hpp'):
        result[path.relative_to(ROOT).as_posix()] = digest(path)
    return dict(sorted(result.items()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--qt-prefix', type=Path, required=True)
    parser.add_argument('--cobs-root', type=Path, required=True)
    parser.add_argument('--serial-prefix', type=Path)
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--ninja', default='ninja')
    parser.add_argument('--jobs', default='2')
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    output, cobs = args.build_dir.resolve(), args.cobs_root.resolve()
    if output == ROOT or ROOT in output.parents or output in ROOT.parents:
        parser.error('--build-dir must be a dedicated external directory')
    if output.exists() and any(output.iterdir()):
        parser.error('--build-dir must be fresh and empty')
    if not (cobs / 'src/cobs/Cobs.h').is_file():
        parser.error('--cobs-root must contain the actual external COBS sources')
    if args.sanitize and os.name == 'nt':
        parser.error('--sanitize requires a Linux sanitizer toolchain')
    output.mkdir(parents=True, exist_ok=True)
    report = dict(passed=False, hardware=False, sanitize=args.sanitize, commands=[],
                  started_utc=datetime.now(timezone.utc).isoformat(),
                  source_sha256_before=sources(cobs), external_cobs=str(cobs))
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', QT_QPA_PLATFORM='offscreen',
               QT_QUICK_BACKEND='software', QT_FORCE_STDERR_LOGGING='1')
    if args.sanitize:
        env['ASAN_OPTIONS'] = 'detect_leaks=0:halt_on_error=1'
        env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
        report['sanitizers'], report['leak_checking'] = ['address', 'undefined'], False

    def run(name, command):
        command = list(map(str, command))
        started = time.monotonic()
        try:
            result = subprocess.run(command, cwd=ROOT, env=env, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600)
            text, code = result.stdout, result.returncode
        except subprocess.TimeoutExpired as error:
            text = error.stdout or ''
            if isinstance(text, bytes):
                text = text.decode(errors='replace')
            code = None
        log = output / (name + '.log')
        log.write_text(text, encoding='utf-8')
        report['commands'].append(dict(command=command, name=name, exit=code,
            seconds=time.monotonic()-started, log=log.name, sha256=digest(log)))
        print(name + ': exit ' + str(code), flush=True)
        if code != 0:
            raise RuntimeError(name + ' failed\n' + text[-16000:])
        return text

    try:
        tools = {name: Path(shutil.which(getattr(args, name)) or getattr(args, name)).absolute()
                 for name in ('cxx', 'cmake', 'ninja')}
        for path in tools.values():
            if not path.is_file():
                raise ValueError('Missing required tool: ' + str(path))
        qt = args.qt_prefix.resolve()
        prefixes = [qt] + ([args.serial_prefix.resolve()] if args.serial_prefix else [])
        env['PATH'] = os.pathsep.join([str(tools['cxx'].parent)] +
            [str(path / 'bin') for path in prefixes] + [env.get('PATH', '')])
        report['tool_sha256'] = {str(path): digest(path) for path in tools.values()}
        report['source_head'] = run('source-head', ['git', 'rev-parse', 'HEAD']).strip()
        report['cobs_head'] = run('cobs-head', ['git', '-C', cobs, 'rev-parse', 'HEAD']).strip()
        build = output / 'build'
        flags = '-Wall -Wextra -Werror'
        configure = [tools['cmake'], '-S', HERE, '-B', build, '-G', 'Ninja',
            '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_CXX_COMPILER=' + str(tools['cxx']),
            '-DCMAKE_MAKE_PROGRAM=' + str(tools['ninja']),
            '-DCMAKE_PREFIX_PATH=' + ';'.join(map(str, prefixes)), '-DCOBS_ROOT=' + str(cobs)]
        if args.serial_prefix:
            configure += ['-DQt6SerialPort_DIR=' +
                          str(args.serial_prefix.resolve() / 'lib/cmake/Qt6SerialPort')]
        if args.sanitize:
            flags += ' -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
            configure += ['-DCMAKE_CXX_FLAGS_RELEASE=-O1 -DNDEBUG',
                          '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined']
        run('configure', configure + ['-DCMAKE_CXX_FLAGS=' + flags])
        run('build', [tools['cmake'], '--build', build, '-j', args.jobs])
        exe = build / ('telemetry_inspector_serial_check.exe' if os.name == 'nt'
                       else 'telemetry_inspector_serial_check')
        gui = build / 'telemetry-inspector-qml.exe' if os.name == 'nt' else build / 'example/telemetry-inspector-qml'
        receipts = output / 'device-check'
        runtime = run('runtime', [exe, gui, receipts])
        counted = re.search(r'Inspector Serial checks=(\d+) reads=(\d+) admissions=(\d+) pty=(\d) Qt=(\S+)', runtime)
        if not counted:
            raise RuntimeError('Serial test executable did not publish its result')
        checks, reads, admissions, pty = map(int, counted.groups()[:4])
        if os.name != 'nt' and (not pty or checks < 1204 or reads < 8 or admissions < 3):
            raise RuntimeError('The maintained real QSerialPort/PTY scenarios did not all execute')
        if os.name == 'nt' and (pty or checks < 2):
            raise RuntimeError('Native build-only serial checks did not execute')
        report['executions'] = dict(checks=checks, reads=reads, admissions=admissions, pty=bool(pty))
        report['qt_version'] = counted[5]
        report['binary_sha256'] = {str(path.relative_to(output)): digest(path) for path in (exe, gui)}
        if os.name != 'nt':
            report['device_check'] = []
            for index in range(4):
                path = receipts / ('device-check-' + str(index) + '.json')
                observed = json.loads(path.read_text(encoding='utf-8'))
                if (observed.get('passed') is not (index == 0) or
                        observed.get('voltage') is not (index == 0) or
                        not observed.get('probe') or not observed.get('exactCounter')):
                    raise RuntimeError('Headless checker acceptance/negative case did not execute')
                report['device_check'].append(dict(file=str(path.relative_to(output)),
                                                  sha256=digest(path), passed=observed['passed']))
        report['serial_runtime_sha256'] = {str(path): digest(path) for prefix in prefixes
            for path in (prefix / 'bin').glob('Qt6SerialPort.dll')}
        report['source_sha256_after'] = sources(cobs)
        if report['source_sha256_before'] != report['source_sha256_after']:
            raise RuntimeError('Source inputs changed during qualification')
        report['passed'] = True
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        report['error'] = str(error)
        print(report['error'], flush=True)
    report['finished_utc'] = datetime.now(timezone.utc).isoformat()
    (output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
