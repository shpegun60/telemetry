#!/usr/bin/env python3
"""Qualify actual Qt loopback TCP discovery and reads in a fresh external tree (MIT)."""
import argparse
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


def sources():
    files = []
    for directory in (HERE, ROOT / 'tests/inspector_qml', ROOT / 'examples/inspector_qml',
                      ROOT / 'lib', ROOT / 'cmake'):
        files += [path for path in directory.rglob('*') if path.is_file() and
                  path.suffix in ('.py', '.cpp', '.hpp', '.h', '.qml', '.txt', '.cmake', '.pri') and
                  '__pycache__' not in path.parts]
    files += [ROOT / 'CMakeLists.txt', ROOT / '.github/workflows/ci.yml']
    return {path.relative_to(ROOT).as_posix(): digest(path) for path in sorted(set(files))}


def executable(name):
    path = Path(shutil.which(name) or name).absolute()
    if not path.is_file():
        raise ValueError('Required executable not found: ' + str(path))
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--qt-prefix', type=Path, required=True)
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
        parser.error('--build-dir must be outside the repository')
    if output.exists() and any(output.iterdir()):
        parser.error('--build-dir must be fresh and empty')
    output.mkdir(parents=True, exist_ok=True)
    report = dict(passed=False, hardware=False, sanitize=args.sanitize,
                  started_utc=datetime.now(timezone.utc).isoformat(),
                  commands=[], executions={}, source_sha256_before=sources())
    environment = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', QT_QPA_PLATFORM='offscreen',
                       QT_QUICK_BACKEND='software', QT_FORCE_STDERR_LOGGING='1')
    if args.sanitize:
        environment['ASAN_OPTIONS'] = 'detect_leaks=0:halt_on_error=1'
        environment['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
        report['sanitizers'] = ['address', 'undefined']
        report['leak_checking'] = False
        report['leak_checking_reason'] = 'Qt offscreen plugin process-global allocation lifetime'

    def run(name, command):
        command = list(map(str, command))
        started = time.monotonic()
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
        tools = {name: executable(getattr(args, name)) for name in ('cxx', 'cmake', 'ninja')}
        qt = args.qt_prefix.resolve()
        environment['PATH'] = os.pathsep.join((str(tools['cxx'].parent), str(qt / 'bin'),
                                               environment.get('PATH', '')))
        if os.name == 'nt':
            fonts = Path(os.environ.get('WINDIR', r'C:\Windows')) / 'Fonts'
            if not fonts.is_dir():
                raise RuntimeError('Windows offscreen font directory is unavailable')
            environment['QT_QPA_FONTDIR'] = str(fonts)
        report['tool_sha256'] = {str(path): digest(path) for path in tools.values()}
        for name, path in tools.items():
            run(name + '-version', [path, '--version'])
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
        check = build / ('telemetry_inspector_tcp_check.exe' if os.name == 'nt'
                         else 'telemetry_inspector_tcp_check')
        runtime = run('runtime', [check])
        counts = re.search(r'Inspector TCP checks=(\d+) reads=(\d+) connections=(\d+) Qt=(\S+)', runtime)
        if not counts or int(counts[1]) < 669 or int(counts[2]) < 32 or int(counts[3]) < 26:
            raise RuntimeError('Runtime did not execute the maintained real-socket scenarios')
        disconnected = re.findall(r'TCP explicit disconnect phase=(prepared-lost-ack|confirmed-write) '
                                  r'callbacks=(\d+) busy=(\d+) outcome=(.*?) writes=(\d+)', runtime)
        if (len(disconnected) != 2 or
                [(item[0], item[1], item[2], item[4]) for item in disconnected] !=
                [('prepared-lost-ack', '1', '0', '1'), ('confirmed-write', '1', '0', '1')] or
                'OutcomeUnknown' not in disconnected[0][3] or
                'Write applied' not in disconnected[1][3] or 'OutcomeUnknown' in disconnected[1][3]):
            raise RuntimeError('Explicit Disconnect did not preserve both real-socket mutation outcomes')
        report['executions'] = dict(zip(('checks', 'reads', 'connections'), map(int, counts.groups()[:3])))
        report['executions']['apply_disconnect_phases'] = [phase[0] for phase in disconnected]
        if ('Inspector TCP Command outcomes: executed=1 accepted=1 unknown=1 callbacks=3 '
                'replay=0 history=3') not in runtime:
            raise RuntimeError('TCP Command statuses, Disconnect completion or no-replay proof missing')
        report['executions']['command_outcomes'] = ['executed', 'accepted', 'unknown']
        report['executions']['command_terminal_callbacks'] = 3
        report['qt_version'] = counts[4]
        app = build / 'example' / ('telemetry-inspector-qml.exe' if os.name == 'nt'
                                   else 'telemetry-inspector-qml')
        peer_log = output / 'tcp-demo-server.log'
        peer_command = [str(app), '--serve-demo', '0']
        peer_started = time.monotonic()
        with peer_log.open('w', encoding='utf-8') as stream:
            peer = subprocess.Popen(peer_command, cwd=ROOT, env=environment,
                                    stdout=stream, stderr=subprocess.STDOUT,
                                    creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
            try:
                port = None
                while time.monotonic() - peer_started < 20:
                    if peer.poll() is not None:
                        raise RuntimeError('TCP demo server exited before admission: ' + peer_log.read_text())
                    match = re.search(r'Inspector TCP demo listening port=(\d+)',
                                      peer_log.read_text(encoding='utf-8'))
                    if match:
                        port = int(match[1])
                        break
                    time.sleep(0.05)
                if port is None:
                    raise RuntimeError('TCP demo server did not publish its ephemeral port')
                screenshot = output / 'tcp-window.png'
                smoke = run('tcp-qml-smoke', [app, '--tcp', '127.0.0.1:' + str(port),
                                               '--smoke', '--screenshot', screenshot])
                views = re.search(r'Inspector QML smoke: published=1 roots=1 endpointRows=(\d+) valueRows=(\d+)', smoke)
                if not views or int(views[1]) < 1 or int(views[2]) < 4:
                    raise RuntimeError('TCP QML smoke did not materialize the received Field')
                if peer.poll() is not None:
                    raise RuntimeError('TCP demo server exited during QML qualification')
                report['executions']['tcp_offscreen_smoke'] = 1
                report['executions']['visible_endpoint_rows'], report['executions']['visible_value_rows'] = map(int, views.groups())
                png = screenshot.read_bytes()
                if png[:8] != b'\x89PNG\r\n\x1a\n' or len(png) < 24 or png[12:16] != b'IHDR':
                    raise RuntimeError('TCP QML screenshot is not a PNG image')
                width, height = struct.unpack('>II', png[16:24])
                if width < 1 or height < 1:
                    raise RuntimeError('TCP QML screenshot is empty')
                report['screenshot'] = dict(path=screenshot.name, sha256=digest(screenshot),
                                            width=width, height=height, bytes=len(png))
            finally:
                peer.terminate()
                try:
                    peer.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    peer.kill()
                    peer.wait(timeout=5)
        report['peer_process'] = dict(command=peer_command, exit=peer.returncode,
                                      termination='runner closes persistent demo listener after smoke',
                                      seconds=time.monotonic() - peer_started,
                                      log=peer_log.name, sha256=digest(peer_log))
        report['binary_sha256'] = {str(path.relative_to(output)): digest(path) for path in (check, app)}
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
