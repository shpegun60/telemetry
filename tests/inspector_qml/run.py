#!/usr/bin/env python3
"""Execute real Qt Inspector models, controller and offscreen QML (MIT).

Requires Qt >= 6.4. All builds/logs/receipts remain in a fresh external directory.
Source and selected tool/runtime hashes identify inputs, not a hermetic SDK.
"""
import argparse
import ctypes
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def executable(name):
    # Preserve clang++ invocation spelling instead of switching its driver mode.
    path = Path(shutil.which(name) or name).absolute()
    if not path.is_file():
        raise ValueError('Required executable not found: ' + str(path))
    return path


def python_image():
    if os.name == 'nt':
        buffer = ctypes.create_unicode_buffer(32768)
        size = ctypes.windll.kernel32.GetModuleFileNameW(None, buffer, len(buffer))
        if not size or size >= len(buffer):
            raise OSError('Cannot identify Python executable image')
        return Path(buffer.value)
    return Path(sys.executable).resolve()


def sources():
    paths = []
    for directory in (HERE, ROOT / 'examples/inspector_qml', ROOT / 'lib', ROOT / 'cmake'):
        paths += [path for path in directory.rglob('*') if path.is_file() and
                  path.suffix in ('.py', '.cpp', '.hpp', '.h', '.qml', '.txt', '.cmake', '.pri') and
                  '__pycache__' not in path.parts]
    paths += [ROOT / 'CMakeLists.txt', ROOT / '.github/workflows/ci.yml']
    return {path.relative_to(ROOT).as_posix(): digest(path) for path in sorted(set(paths))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True, type=Path)
    parser.add_argument('--qt-prefix', required=True, type=Path)
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--ninja', default='ninja')
    parser.add_argument('--jobs', default='2')
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    if args.sanitize and os.name == 'nt':
        parser.error('--sanitize requires a Linux sanitizer toolchain')
    output = args.build_dir.resolve()
    if output == ROOT or ROOT in output.parents or output in ROOT.parents:
        parser.error('--build-dir must be a dedicated directory outside the repository')
    if output.exists() and any(output.iterdir()):
        parser.error('--build-dir must be fresh and empty')
    output.mkdir(parents=True, exist_ok=True)
    report = dict(passed=False, hardware=False, sanitize=args.sanitize,
                  started_utc=datetime.now(timezone.utc).isoformat(),
                  commands=[], executions={}, source_sha256_before=sources())
    environment = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', QT_QPA_PLATFORM='offscreen',
                       QT_QUICK_BACKEND='software', QSG_RHI_BACKEND='software',
                       QT_FORCE_STDERR_LOGGING='1')
    if args.sanitize:
        # Qt's offscreen plugin retains process-global allocations. This profile
        # checks ASan/UBSan errors and does not claim LeakSanitizer coverage.
        environment['ASAN_OPTIONS'] = 'detect_leaks=0:halt_on_error=1'
        environment['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
        report['sanitizers'] = ['address', 'undefined']
        report['leak_checking'] = False
        report['leak_checking_reason'] = 'Qt offscreen plugin process-global allocation lifetime'

    def run(name, command):
        started = time.monotonic()
        command = list(map(str, command))
        try:
            result = subprocess.run(command, cwd=ROOT, env=environment, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600)
            text, code = result.stdout, result.returncode
        except subprocess.TimeoutExpired as error:
            text = error.stdout or ''
            if isinstance(text, bytes):
                text = text.decode(errors='replace')
            code = None
        log = output / (name + '.log')
        log.write_text(text, encoding='utf-8')
        report['commands'].append(dict(name=name, command=command, exit=code,
                                       seconds=time.monotonic() - started, log=log.name,
                                       sha256=digest(log)))
        print(name + ': exit ' + str(code), flush=True)
        if code != 0:
            raise RuntimeError(name + ' failed\n' + text[-16000:])
        return text

    try:
        if os.name == 'nt':
            fonts = Path(os.environ.get('WINDIR', r'C:\Windows')) / 'Fonts'
            if not fonts.is_dir():
                raise RuntimeError('Windows offscreen font directory is unavailable: ' + str(fonts))
            environment['QT_QPA_FONTDIR'] = str(fonts)
            report['font_directory'] = str(fonts)
        for name in ('Models.hpp', 'Models.cpp', 'InspectorController.hpp', 'InspectorController.cpp',
                     'DemoPeer.hpp', 'DemoPeer.cpp', 'Main.qml', 'CMakeLists.txt', 'main.cpp'):
            if not (ROOT / 'examples/inspector_qml' / name).is_file():
                raise RuntimeError('Required runnable example input is missing: ' + name)
        tools = {name: executable(getattr(args, name)) for name in ('cxx', 'cmake', 'ninja')}
        qt = args.qt_prefix.resolve()
        environment['PATH'] = os.pathsep.join((str(tools['cxx'].parent), str(qt / 'bin'),
                                               environment.get('PATH', '')))
        report['tool_sha256'] = {str(path): digest(path) for path in (*tools.values(), python_image())}
        for name, path in tools.items():
            run(name + '-version', [path, '--version'])
        run('python-version', [sys.executable, '--version'])
        report['source_head'] = run('source-head', ['git', 'rev-parse', 'HEAD']).strip()
        build = output / 'build'
        configure = [tools['cmake'], '-S', HERE, '-B', build, '-G', 'Ninja',
                     '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_CXX_COMPILER=' + str(tools['cxx']),
                     '-DCMAKE_MAKE_PROGRAM=' + str(tools['ninja']), '-DCMAKE_PREFIX_PATH=' + str(qt)]
        flags = '-Wall -Wextra -Werror'
        if args.sanitize:
            flags += ' -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
            configure += ['-DCMAKE_CXX_FLAGS_RELEASE=-O1 -DNDEBUG',
                          '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined']
        run('configure', configure + ['-DCMAKE_CXX_FLAGS=' + flags])
        run('build', [tools['cmake'], '--build', build, '-j', args.jobs])
        suffix = '.exe' if os.name == 'nt' else ''
        check = build / ('telemetry_inspector_qml_check' + suffix)
        runtime = run('runtime', [check])
        counts = re.search(r'Inspector Qt checks=(\d+) reads=(\d+) Qt=(\S+)', runtime)
        if not counts or int(counts[1]) < 719 or int(counts[2]) < 20:
            raise RuntimeError('Runtime did not execute the maintained model/controller checks')
        disconnected = re.findall(r'Inspector Apply disconnect phase=(queued|offered|prepared|executed|write_confirmed) '
                                  r'finished=(\d+) writes=(\d+)', runtime)
        if disconnected != [('queued', '1', '0'), ('offered', '1', '0'), ('prepared', '1', '0'),
                            ('executed', '1', '1'), ('write_confirmed', '1', '1')]:
            raise RuntimeError('Explicit Disconnect did not cover all five Apply phases exactly once')
        report['executions']['apply_disconnect_phases'] = [phase[0] for phase in disconnected]
        report['executions']['checks'], report['executions']['reads'] = map(int, counts.groups()[:2])
        report['qt_version'] = counts[3]
        physical_input = build / ('telemetry_inspector_qml_input_check' + suffix)
        typed = run('qml-physical-input', [physical_input])
        typed_counts = re.search(r'Inspector QML physical input checks=(\d+) writes=2 noEnter=2 '
                                 r'gain=4\.25 counter=18446744073709551614 Qt=(\S+)', typed)
        if not typed_counts or int(typed_counts[1]) < 58 or typed_counts[2] != counts[3]:
            raise RuntimeError('Actual keyboard/mouse input did not complete both no-Enter Apply workflows')
        report['executions']['physical_input_checks'] = int(typed_counts[1])
        report['executions']['keyboard_mouse_no_enter_applies'] = 2
        app = build / 'example' / ('telemetry-inspector-qml' + suffix)
        screenshot = output / 'window.png'
        smoke = run('qml-smoke', [app, '--smoke', '--screenshot', screenshot])
        views = re.search(r'Inspector QML smoke: published=1 roots=1 endpointRows=(\d+) valueRows=(\d+)', smoke)
        if not views or int(views[1]) < 1 or int(views[2]) < 4:
            raise RuntimeError('Offscreen QML did not confirm actual endpoint and value delegates')
        report['executions']['offscreen_smoke'] = 1
        report['executions']['visible_endpoint_rows'], report['executions']['visible_value_rows'] = map(int, views.groups())
        png = screenshot.read_bytes()
        if png[:8] != b'\x89PNG\r\n\x1a\n' or len(png) < 24 or png[12:16] != b'IHDR':
            raise RuntimeError('QML window screenshot is not a PNG image')
        width, height = struct.unpack('>II', png[16:24])
        if width < 1 or height < 1:
            raise RuntimeError('QML window screenshot is empty')
        report['screenshot'] = dict(path=screenshot.name, sha256=digest(screenshot),
                                    width=width, height=height, bytes=len(png))
        edit_screenshot = output / 'edit-window.png'
        edited = run('qml-edit-smoke', [app, '--edit-smoke', '--screenshot', edit_screenshot])
        if 'Inspector QML edit smoke: writes=1 refreshed=1 gain=10 controls=Edit,TextField,Apply' not in edited:
            raise RuntimeError('QML controls did not complete the actual Write and normalized readback')
        edit_png = edit_screenshot.read_bytes()
        if edit_png[:8] != b'\x89PNG\r\n\x1a\n' or len(edit_png) < 24 or edit_png[12:16] != b'IHDR':
            raise RuntimeError('Workbench screenshot is not a PNG image')
        edit_width, edit_height = struct.unpack('>II', edit_png[16:24])
        if edit_width < 1 or edit_height < 1:
            raise RuntimeError('Workbench screenshot is empty')
        report['executions']['offscreen_edit_smoke'] = 1
        report['edit_screenshot'] = dict(path=edit_screenshot.name, sha256=digest(edit_screenshot),
                                       width=edit_width, height=edit_height, bytes=len(edit_png))
        report['binary_sha256'] = {str(path.relative_to(output)): digest(path)
                                  for path in (check, physical_input, app)}
        report['qt_runtime_sha256'] = {str(path.relative_to(qt)): digest(path)
                                       for path in (qt / 'bin').glob('Qt6*.dll') if path.is_file()}
        report['source_sha256_after'] = sources()
        if report['source_sha256_before'] != report['source_sha256_after']:
            raise RuntimeError('Source inputs changed during qualification')
        report['passed'] = True
    except (ValueError, OSError, RuntimeError, subprocess.SubprocessError) as error:
        report['error'] = str(error)
        print(report['error'], flush=True)
    report['finished_utc'] = datetime.now(timezone.utc).isoformat()
    (output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
