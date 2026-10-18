#!/usr/bin/env python3
"""Prove client/common compile without local Model, Codec, Registry or Resource.

Artifacts and dependency manifests are written only to an external directory.
Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
CORE = ROOT / 'lib/client'


def inputs():
    paths = list(CORE.glob('*.hpp')) + list((ROOT / 'lib/common').glob('*.hpp'))
    paths += [ROOT / 'lib/delegate/tiny_delegate.hpp', HERE / 'Smoke.cpp', HERE / 'run.py',
              ROOT / 'tests/async_client/Fixture.hpp']
    return {path.relative_to(ROOT).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted(paths)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True)
    parser.add_argument('--cxx', default='g++')
    args = parser.parse_args()
    build = Path(args.build_dir).resolve()
    if build == ROOT or ROOT in build.parents:
        parser.error('use an external build directory')
    build.mkdir(parents=True, exist_ok=True)
    stage = build / 'include'
    names = []
    for source, relative in ((CORE, Path('client')),
                             (ROOT / 'lib/common', Path('common'))):
        target = stage / relative
        target.mkdir(parents=True, exist_ok=True)
        for path in sorted(source.glob('*.hpp')):
            shutil.copy2(path, target / path.name)
            names.append((relative / path.name).as_posix())
    (stage / 'delegate').mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / 'lib/delegate/tiny_delegate.hpp', stage / 'delegate/tiny_delegate.hpp')
    report = dict(passed=False, started_at_utc=datetime.now(timezone.utc).isoformat(),
                  input_sha256=inputs(), commands=[], stage_headers=sorted(names))

    def run(command, name):
        command = list(map(str, command))
        result = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=180)
        (build / (name + '.log')).write_text(result.stdout, encoding='utf8')
        report['commands'].append(dict(name=name, command=command, returncode=result.returncode))
        if result.returncode:
            raise RuntimeError(name + ' failed:\n' + result.stdout)
        return result.stdout

    try:
        run([args.cxx, '--version'], 'compiler')
        compiler = Path(shutil.which(args.cxx) or args.cxx).resolve()
        report['compiler_sha256'] = hashlib.sha256(compiler.read_bytes()).hexdigest()
        flags = [args.cxx, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror']
        for name in names:
            probe = build / 'Header.cpp'
            probe.write_text('#include <' + name + '>\n', encoding='utf8')
            run([*flags, '-I', stage, '-fsyntax-only', probe], 'header-' + name.replace('/', '_'))
        suffix = '.exe' if sys.platform == 'win32' else ''
        for label, include in (('isolated', stage), ('source', ROOT / 'lib')):
            executable = build / (label + suffix)
            dep = build / (label + '.d')
            run([*flags, '-I', include, '-MMD', '-MF', dep, HERE / 'Smoke.cpp', '-o', executable],
                'compile-' + label)
            output = run([executable], 'run-' + label)
            match = re.fullmatch(r'client module checks=(\d+) failures=0\s*', output)
            if not match or int(match[1]) < 19:
                raise RuntimeError('missing meaningful positive check count')
            report[label + '_checks'] = int(match[1])
            if label == 'isolated':
                dependencies = dep.read_text(encoding='utf8').replace('\\\n', ' ')
                forbidden = str(ROOT / 'lib').replace('\\', '/') + '/'
                if forbidden in dependencies.replace('\\', '/'):
                    raise RuntimeError('isolated compilation reached repository library headers')
        report['removed_client_paths'] = []
        for name in ('telemetry/client/DeviceClient.hpp', 'integration/telemetry/v3/RpcClient.hpp'):
            if (ROOT / 'lib' / name).exists():
                raise RuntimeError('obsolete client header remains: ' + name)
            probe = build / 'Removed.cpp'
            probe.write_text('#include <' + name + '>\n', encoding='utf8')
            command = [*flags, '-I', ROOT / 'lib', '-fsyntax-only', probe]
            result = subprocess.run(list(map(str, command)), cwd=ROOT, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
            (build / ('removed-' + name.replace('/', '_') + '.log')).write_text(result.stdout, encoding='utf8')
            if not result.returncode or name not in result.stdout:
                raise RuntimeError('obsolete include did not fail as expected: ' + name)
            report['removed_client_paths'].append(dict(header=name, returncode=result.returncode))
        report['final_input_sha256'] = inputs()
        if report['input_sha256'] != report['final_input_sha256']:
            raise RuntimeError('source changed during qualification')
        report['passed'] = True
        print('client module canonical headers/runtime and removed-path controls passed', flush=True)
    finally:
        report['finished_at_utc'] = datetime.now(timezone.utc).isoformat()
        (build / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')


if __name__ == '__main__':
    main()
