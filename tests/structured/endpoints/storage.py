#!/usr/bin/env python3
"""Storage-policy correctness matrix and real cross-TU ABI mismatch check.

Authors: Ruslan Kovtun (shpegun60), codexAi. MIT license.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
ADAPTER = ROOT / 'lib/telemetry_structured/model/Adapter.cpp'
ABI = ROOT / 'lib/telemetry_structured/abi/StructuredAbi.cpp'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
    p.add_argument('--sanitize', action='store_true')
    p.add_argument('--null-checks', action='store_true')
    p.add_argument('--arm', action='store_true')
    p.add_argument('--build-dir', type=Path, required=True)
    args = p.parse_args()
    if args.arm and args.sanitize:
        p.error('ARM sanitizers are not available in this runner')
    output = args.build_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    counts = {'passed': 0, 'rejected': 0}

    def run(command, label, diagnostic=None):
        result = subprocess.run(list(map(str, command)), cwd=ROOT, capture_output=True,
                                text=True, encoding='utf-8', errors='replace', timeout=180)
        log = f'COMMAND {command!r}\nEXIT {result.returncode}\n{result.stdout}\n{result.stderr}'
        (output / (label + '.log')).write_text(log, encoding='utf-8')
        if diagnostic:
            if not result.returncode or not re.search(diagnostic, log, re.S | re.I):
                raise RuntimeError(label + ': missing intended rejection\n' + log)
            counts['rejected'] += 1
        elif result.returncode:
            raise RuntimeError(label + '\n' + log)
        else:
            counts['passed'] += 1
        print(label + ': pass', flush=True)

    run([args.cxx, '--version'], 'compiler')
    flags = [args.cxx, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-pedantic-errors',
             '-Ilib', '-Ilib/boost_pfr/include', '-Ilib/magic_enum']
    if args.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    if args.null_checks:
        flags += ['-fno-delete-null-pointer-checks']
    if args.arm:
        flags += ['-mcpu=cortex-m7', '-mthumb', '-mfpu=fpv5-d16', '-mfloat-abi=hard',
                  '-fno-exceptions', '-fno-rtti', '-ffunction-sections', '-fdata-sections', '-fstack-usage']
    for budget in (0, 16, 32, 64):
        for opt in (('O2', 'Os', 'Og') if args.arm else ('O2',)):
            policy = [f'-DTELEMETRY_STRUCTURED_LOCAL_BYTES={budget}', '-'+opt]
            probes = [HERE/'StoragePolicyProbe.cpp', HERE/'CompositeStorage.cpp', HERE/'LeafStorage.cpp', HERE/'ErasedBoundary.cpp',
                      HERE.parent/'model/ModelProbe.cpp', HERE.parent/'model/ModelEdges.cpp',
                      HERE.parent/'model/LargeResponse.cpp']
            for source in probes:
                label = f'{budget}-{opt}-{source.stem}'
                if args.arm:
                    run(flags + policy + ['-c', source, '-o', output/(label+'.o')], label)
                else:
                    exe = output / (label + ('.exe' if os.name == 'nt' else ''))
                    extra = [ADAPTER, ABI] if source.stem == 'ModelProbe' else []
                    run(flags + policy + [source, *extra, '-o', exe], 'compile-'+label)
                    run([exe], 'execute-'+label)

    # Different compiler definitions in real TUs, rather than a synthetic tag.
    objects = []
    for source, budget in ((HERE/'StorageAbiCaller.cpp', 0), (ADAPTER, 16), (ABI, 16)):
        obj = output / (source.stem+'.o')
        run(flags + ['-O2', f'-DTELEMETRY_STRUCTURED_LOCAL_BYTES={budget}',
                     '-c', source, '-o', obj], 'abi-'+source.stem)
        objects.append(obj)
    link = ['-nostdlib', '-Wl,-e,main', '-Wl,--gc-sections', '-lc', '-lgcc'] if args.arm else []
    run(flags + [*objects, *link, '-o', output/'mismatch'], 'budget-mismatch',
        r'undefined (?:reference|symbol).*readFieldEncoded')
    run([flag for flag in flags if flag != '-fstack-usage'] +
        ['-DTELEMETRY_STRUCTURED_LOCAL_BYTES=-1', '-fsyntax-only', HERE/'StorageAbiCaller.cpp'],
        'negative-budget', 'Local endpoint object budget cannot be negative')
    (output/'summary.json').write_text(json.dumps(counts, indent=2)+'\n')
    print('Storage policy checks passed:', counts, flush=True)


if __name__ == '__main__':
    main()
