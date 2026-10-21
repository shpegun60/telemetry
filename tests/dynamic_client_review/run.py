#!/usr/bin/env python3
"""Run independent core admission and reentrant lifecycle acceptance probes.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""
import argparse
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


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True, type=Path)
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    out = args.build_dir.resolve()
    if out == ROOT or ROOT in out.parents or out in ROOT.parents:
        parser.error('Use an external directory that does not contain the checkout')
    out.mkdir(parents=True, exist_ok=True)
    compiler = str(Path(shutil.which(args.cxx) or args.cxx).absolute())
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1')
    env.setdefault('ASAN_OPTIONS', 'detect_leaks=1:halt_on_error=1')
    env.setdefault('UBSAN_OPTIONS', 'halt_on_error=1:print_stacktrace=1')
    commands = []

    def run(name, command):
        command = list(map(str, command))
        started = time.monotonic()
        result = subprocess.run(command, cwd=ROOT, env=env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=600)
        log = out / (name + '.log')
        log.write_text('COMMAND ' + repr(command) + '\nEXIT ' + str(result.returncode)
                       + '\n' + result.stdout, encoding='utf-8')
        commands.append(dict(name=name, command=command, exit=result.returncode,
                             seconds=time.monotonic() - started, log=log.name,
                             log_sha256=sha256(log)))
        if result.returncode:
            raise RuntimeError(name + ' failed\n' + result.stdout)
        return result.stdout

    version = run('compiler', [compiler, '--version'])
    flags = [compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-pedantic-errors',
             '-DTINY_DELEGATE_ENABLE_HEAP_FALLBACK=0', '-Ilib']
    flags += (['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
               '-fno-omit-frame-pointer'] if args.sanitize else ['-O2'])
    native_flags = flags + ['-Ilib/boost_pfr/include', '-Ilib/magic_enum']
    native_sources = [ROOT / 'lib/integration/encoded/Dispatcher.cpp',
                      ROOT / 'lib/telemetry/model/Adapter.cpp',
                      ROOT / 'lib/telemetry/abi/StructuredAbi.cpp']
    dependency_text = run('dependencies', flags + ['-MM', HERE / 'CorePolicy.cpp'])
    dependency_text += run('integration-dependencies', native_flags + ['-MM', HERE / 'Integration.cpp', *native_sources])
    sources = {HERE / 'CorePolicy.cpp', HERE / 'Integration.cpp', HERE / 'run.py', HERE / 'README.md', *native_sources}
    for item in dependency_text.replace('\\\n', ' ').split()[1:]:
        path = (ROOT / item).resolve()
        if path.is_file():
            sources.add(path)

    def snapshot():
        return {p.relative_to(ROOT).as_posix(): sha256(p) for p in sorted(sources)}

    before = snapshot()
    binary = out / ('CorePolicy.exe' if os.name == 'nt' else 'CorePolicy')
    run('build', flags + [HERE / 'CorePolicy.cpp', '-o', binary])
    output = run('execute', [binary])
    match = re.search(r'independent core policy: checks=(\d+) failures=0', output)
    if not match or int(match[1]) < 152:
        raise RuntimeError('Missing complete core-policy acceptance count')
    integration = out / ('Integration.exe' if os.name == 'nt' else 'Integration')
    run('integration-build', native_flags + [HERE / 'Integration.cpp', *native_sources, '-o', integration])
    integration_output = run('integration-execute', [integration])
    integration_match = re.search(r'independent dynamic integration: checks=(\d+) failures=0', integration_output)
    if not integration_match or int(integration_match[1]) < 60:
        raise RuntimeError('Missing complete independent integration count')
    after = snapshot()
    if before != after:
        raise RuntimeError('Source inputs changed during qualification')
    summary = dict(passed=True, hardware=False, sanitize=args.sanitize,
                   compiler=version, checks=int(match[1]), integration_checks=int(integration_match[1]), commands=commands,
                   source_sha256_before=before, source_sha256_after=after,
                   binary_sha256=sha256(binary), integration_binary_sha256=sha256(integration))
    (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    print(output.strip(), flush=True)
    print(integration_output.strip(), flush=True)


if __name__ == '__main__':
    main()
