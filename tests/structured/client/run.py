#!/usr/bin/env python3
"""Build a fake Device and verify the JS v3 client against native C++ bytes.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
Generated inputs, requests, results and logs stay in --build-dir.
"""
import argparse
import json
import os
from pathlib import Path
import runpy
import shutil
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
    parser.add_argument('--node', default=os.environ.get('NODE', 'node'))
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--null-checks', action='store_true')
    parser.add_argument('--browser', action='store_true')
    args = parser.parse_args()
    out = args.build_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    compiler = Path(shutil.which(args.cxx) or args.cxx).resolve()
    # Windows MinGW's runtime DLLs must match the compiler, not a different kit.
    env['PATH'] = str(compiler.parent) + os.pathsep + env.get('PATH', '')
    commands = 0

    def run(command, label):
        nonlocal commands
        result = subprocess.run(list(map(str, command)), cwd=ROOT, env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, timeout=180)
        (out / (label + '.log')).write_text(result.stdout, encoding='utf-8')
        if result.returncode:
            raise RuntimeError(f'{label}: {result.stdout}')
        commands += 1
        return result.stdout

    flags = [args.cxx, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Werror']
    if args.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g']
    if args.null_checks:
        flags += ['-fno-delete-null-pointer-checks']
    flags += ['-I' + str(ROOT / name) for name in ('lib', 'lib/boost_pfr/include', 'lib/magic_enum')]
    sources = [ROOT / 'lib/telemetry_structured/abi/StructuredAbi.cpp',
               ROOT / 'lib/telemetry_structured/model/Adapter.cpp',
               ROOT / 'lib/resource/structured/detail/Values.cpp']
    suffix = '.exe' if os.name == 'nt' else ''
    for source, name in (('Device.cpp', 'client-device'), ('Resources.cpp', 'client-resources')):
        program = out / (name + suffix)
        run(flags + [HERE / source, *sources, '-o', program], 'build-' + name)
        run([program, out], 'generate-' + name)
    oracle = runpy.run_path(str(HERE.parent / 'descriptor/parser.py'))['parse']
    for name in ('descriptor.bin', 'resources-descriptor.bin'):
        oracle((out / name).read_bytes())
    report = json.loads(run([args.node, HERE / 'Check.mjs', out,
                            out / ('client-device' + suffix)], 'js-check'))
    if args.browser:
        run([os.sys.executable, HERE / 'Browser.py', '--device', out / ('client-device' + suffix),
             '--build-dir', out], 'browser-check')
    report['successful_commands'] = commands
    report['independent_descriptors'] = 2
    (out / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')
    print('Structured client checks passed:', report, flush=True)


if __name__ == '__main__':
    main()
