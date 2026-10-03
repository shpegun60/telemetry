#!/usr/bin/env python3
"""Run final C++20 shared ID, slot and numeric checks without Qt (MIT)."""
import argparse
import os
from pathlib import Path
import sys
from regression.checks import Commands, POSITIVES, syntax_contracts, header_and_fp_checks, runtime_count_gate_controls

ROOT = Path(__file__).resolve().parent.parent
HERE = Path(__file__).resolve().parent

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
    parser.add_argument('--std', choices=('c++20',), default='c++20')
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--source-root', type=Path, default=ROOT)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--null-checks', action='store_true')
    args = parser.parse_args()
    root, output = args.source_root.resolve(), args.build_dir.resolve()
    environment = os.environ.copy()
    environment.setdefault('ASAN_OPTIONS', 'detect_leaks=1:detect_stack_use_after_return=1')
    environment.setdefault('UBSAN_OPTIONS', 'halt_on_error=1')
    if os.name == 'nt':
        import ctypes
        ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002)
    runner = Commands(root, output, environment)
    runner.runtime_gate_controls = runtime_count_gate_controls()
    runner.write_summary()
    flags = [args.cxx, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-pedantic-errors', '-fdiagnostics-color=never',
        '-I' + str(root / 'lib'), '-I' + str(root / 'lib/boost_pfr/include'), '-I' + str(root / 'lib/magic_enum')]
    if args.null_checks:
        flags.append('-fno-delete-null-pointer-checks')
    options = ['-O2']
    if args.sanitize:
        options = ['-O1', '-g', '-fno-omit-frame-pointer', '-fsanitize=address,undefined,float-cast-overflow',
            '-fsanitize-address-use-after-scope', '-fno-sanitize-recover=all']
    version = runner.run([args.cxx, '--version'], 'compiler-version')
    for suite in POSITIVES:
        exe = output / (suite + ('.exe' if os.name == 'nt' else ''))
        runner.run([*flags, *options, HERE / 'regression' / (suite + '.cpp'), '-o', exe], suite + '-compile')
        runner.run([exe], suite + '-run', execute=True)
    for case in range(1, 5):
        exe = output / ('id-name-' + str(case) + ('.exe' if os.name == 'nt' else ''))
        runner.run([*flags, *options, '-DCASE=' + str(case), HERE / 'regression/IdNameCollisionCheck.cpp', '-o', exe], 'id-name-' + str(case) + '-compile')
        runner.run([exe], 'id-name-' + str(case) + '-run', execute=True)
    exe = output / ('DebugLevelCheck' + ('.exe' if os.name == 'nt' else ''))
    runner.run([*flags, *[option for option in options if option != '-O1' and option != '-O2'], '-Og', HERE / 'regression/DebugLevelCheck.cpp', '-o', exe], 'debug-Og-compile')
    runner.run([exe], 'debug-Og-run', execute=True)
    if os.name != 'nt':
        objects = []
        for part, define in (('default', 'TELEMETRY_WEAK_OVERRIDE_DEFAULT'), ('strong', 'TELEMETRY_WEAK_OVERRIDE_STRONG'), ('client', None)):
            obj = output / ('weak-' + part + '.o'); objects.append(obj)
            runner.run([*flags, *options, *(['-D' + define] if define else []), '-c', HERE / 'regression/WeakOverrideCheck.cpp', '-o', obj], 'weak-' + part + '-compile')
        exe = output / 'weak-override'
        runner.run([*flags, *options, *objects, '-o', exe], 'weak-override-link')
        runner.run([exe], 'weak-override-run', execute=True)
    syntax_contracts(flags, runner.run, HERE)
    headers = header_and_fp_checks(flags, runner.run, root, output, version)
    for case in range(1, 7):
        exe = output / ('id-abort-' + str(case) + ('.exe' if os.name == 'nt' else ''))
        runner.run([*flags, *options, '-DTELEMETRY_ID_BOUNDARY_ABORT_CASE=' + str(case), HERE / 'regression/IdBoundaryCheck.cpp', '-o', exe], 'id-abort-' + str(case) + '-compile')
        runner.run([exe], 'id-abort-' + str(case) + '-run', execute=True, abort=True)
    runner.write_summary(headers=headers, source_root=str(root), compiler_version=version.strip(), standard='c++20',
        sanitize=args.sanitize, null_checks=args.null_checks)
    print('Shared checks passed:', runner.counts, flush=True)

if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError) as error:
        sys.exit(str(error))
