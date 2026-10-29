#!/usr/bin/env python3
"""Fresh external host qualification of the actual no-heap board dispatch gate. MIT."""
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

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--cobs-root', type=Path, required=True)
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--ninja', default='ninja')
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    out, cobs = args.build_dir.resolve(), args.cobs_root.resolve()
    if any(out == path or path in out.parents or out in path.parents for path in (ROOT, cobs)):
        parser.error('build-dir must be outside source trees and their parents')
    if out.exists() and any(out.iterdir()):
        parser.error('build-dir must be fresh and empty')
    out.mkdir(parents=True, exist_ok=True)
    # Preserve the invoked clang++ name: resolving its symlink to clang changes
    # the link driver's default language and drops the C++ runtime libraries.
    tools = {name: Path(shutil.which(getattr(args, name)) or getattr(args, name)).absolute()
             for name in ('cxx', 'cmake', 'ninja')}
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1')
    env['PATH'] = str(tools['cxx'].parent) + os.pathsep + env.get('PATH', '')
    if args.sanitize:
        env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                   UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')

    def inputs():
        files = []
        for directory in (HERE.parent, ROOT / 'lib', cobs / 'src'):
            for base, directories, names in os.walk(directory):
                excluded = {'out', 'build', '__pycache__'}
                if directory == cobs / 'src':
                    excluded.add('tests')
                directories[:] = [name for name in directories if name not in excluded]
                files += [Path(base) / name for name in names
                          if Path(name).suffix in ('.cpp', '.c', '.hpp', '.h', '.txt',
                                                   '.cmake', '.py', '.ps1', '.ld')]
        files += [ROOT / 'examples/inspector_serial/SerialProfile.hpp']
        return {str(path): digest(path) for path in sorted(set(files))}

    report = dict(passed=False, hardware=False, startedUtc=datetime.now(timezone.utc).isoformat(),
                  commands=[], inputHashesBefore=inputs())

    def run(name, command):
        result = subprocess.run(list(map(str, command)), cwd=ROOT, env=env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300)
        log = out / (name + '.log')
        log.write_text(result.stdout, encoding='utf-8')
        report['commands'].append(dict(name=name, command=list(map(str, command)),
                                        exit=result.returncode, log=log.name, sha256=digest(log)))
        print(name + ': exit ' + str(result.returncode), flush=True)
        if result.returncode:
            raise RuntimeError(result.stdout[-10000:])
        return result.stdout

    try:
        report['compilerVersion'] = run('compiler-version', [tools['cxx'], '--version'])
        flags = '-Wall -Wextra -Werror'
        if args.sanitize:
            flags += ' -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
        build = out / 'build'
        run('configure', [tools['cmake'], '-S', HERE, '-B', build, '-G', 'Ninja',
                          '-DCMAKE_CXX_COMPILER=' + str(tools['cxx']),
                          '-DCMAKE_MAKE_PROGRAM=' + str(tools['ninja']),
                          '-DCOBS_ROOT=' + str(cobs), '-DCMAKE_BUILD_TYPE=Release',
                          '-DCMAKE_CXX_FLAGS=' + flags])
        run('build', [tools['cmake'], '--build', build, '-j', '2'])
        binary = build / ('nucleo_h7s_host.exe' if os.name == 'nt' else 'nucleo_h7s_host')
        output = run('runtime', [binary])
        count = re.search(r'NUCLEO H7S host checks=(\d+) passed', output)
        if not count or int(count[1]) < 180:
            raise RuntimeError('Maintained protocol/gating checks did not run')
        report.update(checks=int(count[1]), binarySha256=digest(binary), inputHashesAfter=inputs())
        if report['inputHashesBefore'] != report['inputHashesAfter']:
            raise RuntimeError('Inputs changed during qualification')
        report['passed'] = True
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        report['error'] = str(error)
        print(str(error), flush=True)
    report['finishedUtc'] = datetime.now(timezone.utc).isoformat()
    (out / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
