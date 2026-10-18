#!/usr/bin/env python3
"""Resource-free descriptor consumer, frozen bytes and actual Resource adapter checks.

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

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    output = args.build_dir.resolve()
    if output == ROOT or ROOT in output.parents:
        parser.error('Build directory must be outside the source checkout')
    output.mkdir(parents=True, exist_ok=True)
    compiler = shutil.which(args.cxx) or args.cxx
    flags = [compiler, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
             '-pedantic-errors', '-Ilib', '-Ilib/boost_pfr/include', '-Ilib/magic_enum']
    if args.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    commands = []

    def run(command, label):
        command = list(map(str, command))
        result = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True,
                                encoding='utf-8', errors='replace', timeout=180)
        (output / (label + '.log')).write_text(
            f'COMMAND {command!r}\nEXIT {result.returncode}\n{result.stdout}', encoding='utf-8')
        commands.append(dict(label=label, command=command, returncode=result.returncode))
        if result.returncode:
            raise RuntimeError(f'{label} failed\n{result.stdout}')
        print(f'{label}: pass', flush=True)
        return result.stdout

    run([compiler, '--version'], 'compiler')
    dependencies = run(flags + ['-MM', HERE / 'CoreOnly.cpp'], 'core-dependencies')
    if re.search(r'(?:^|[/\\])(?:resource|client)[/\\]', dependencies):
        raise RuntimeError('Resource-free descriptor consumer includes Resource or Client')
    def dependency_paths(text):
        return [Path(part).resolve() for part in text.replace('\\\n', ' ').split()[1:]
                if Path(part).is_file()]

    dependency_files = dependency_paths(dependencies)
    if not dependency_files:
        raise RuntimeError('Compiler did not report dependency closure')
    adapter_dependencies = run(flags + ['-MM', HERE / 'ResourceAdapter.cpp'],
                               'adapter-dependencies')
    source_files = set(dependency_files + dependency_paths(adapter_dependencies))
    source_files.update(HERE.glob('*'))
    source_files.update((ROOT / 'lib/telemetry/descriptor').rglob('*.hpp'))
    source_files.update(ROOT / 'tests/structured/descriptor' / (name + '.hex')
                        for name in ('mixed', 'edge', 'empty'))

    def hashes():
        return {str(path.relative_to(ROOT) if path.is_relative_to(ROOT) else path):
                hashlib.sha256(path.read_bytes()).hexdigest()
                for path in sorted(source_files) if path.is_file()}

    before = hashes()
    executions = {}
    for stem, floor in (('CoreOnly', 100), ('ResourceAdapter', 1000)):
        program = output / (stem + ('.exe' if os.name == 'nt' else ''))
        run(flags + [HERE / (stem + '.cpp'), '-o', program], 'build-' + stem)
        arguments = [output / (name + '.bin') for name in ('mixed', 'edge', 'empty')]
        text = run([program, *arguments] if stem == 'ResourceAdapter' else [program], 'execute-' + stem)
        count = re.search(r': (\d+) (?:equivalent )?slice checks passed', text)
        if not count or int(count[1]) < floor:
            raise RuntimeError(f'{stem}: runtime check count is absent or below floor')
        executions[stem] = int(count[1])
    for name in ('mixed', 'edge', 'empty'):
        expected = bytes.fromhex((ROOT / 'tests/structured/descriptor' / (name + '.hex')).read_text())
        if (output / (name + '.bin')).read_bytes() != expected:
            raise RuntimeError('Canonical descriptor differs from frozen bytes: ' + name)
    after = hashes()
    if before != after:
        raise RuntimeError('Descriptor source inputs changed during qualification')
    summary = dict(passed=True, executions=executions, commands=commands,
                   resource_free_dependencies=len(dependency_files), source_sha256=after)
    (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    print('Descriptor core checks passed:', executions, flush=True)


if __name__ == '__main__':
    main()
