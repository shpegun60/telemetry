#!/usr/bin/env python3
"""Descriptor correctness, independent parser budgets and offline ARM representation checks.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time
from check_parser import check

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
DIAGNOSTICS = {
    **{case: 'invalidEndpointName' for case in (1, 2, 3, 4, 6, 7, 8)},
    5: r'deleted (?:function|constructor)',
    9: 'duplicateDescriptorName', 10: 'duplicateDescriptorName',
    11: r'deleted (?:function|member)', 12: 'Descriptor type ceiling exceeded',
    13: 'descriptorSizeExceeded', 14: 'invalidDescriptorName',
    **{case: 'Descriptor type resource ceiling exceeded' for case in range(15, 21)},
    21: 'Descriptor endpoint ceiling exceeded', 22: 'Descriptor catalog ceiling exceeded',
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
    parser.add_argument('--arm', action='store_true')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--null-checks', action='store_true')
    parser.add_argument('--build-dir', type=Path, required=True)
    args = parser.parse_args()
    if args.arm and args.sanitize:
        parser.error('ARM execution sanitizer is not provided')
    output = args.build_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    counts = dict(passed=0, rejected=0)

    def run(command, label, diagnostic=''):
        start = time.perf_counter()
        result = subprocess.run([str(x) for x in command], cwd=ROOT, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace', timeout=180)
        log = f'COMMAND {command!r}\nEXIT {result.returncode}\n{result.stdout}'
        (output / (label + '.log')).write_text(log, encoding='utf-8')
        if diagnostic:
            if result.returncode == 0 or re.search(diagnostic, result.stdout, re.I | re.S) is None:
                raise RuntimeError(f'{label}: intended rejection missing\n{log}')
            counts['rejected'] += 1
        elif result.returncode:
            raise RuntimeError(f'{label} failed\n{log}')
        else:
            counts['passed'] += 1
        print(f'{label}: pass ({time.perf_counter() - start:.2f}s)', flush=True)
        return result.stdout

    run([args.cxx, '--version'], 'compiler')
    flags = [args.cxx, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-pedantic-errors',
             '-fdiagnostics-color=never', '-Ilib', '-Ilib/boost_pfr/include', '-Ilib/magic_enum']
    if args.null_checks:
        flags += ['-fno-delete-null-pointer-checks']
    if args.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    if args.arm:
        flags += ['-mcpu=cortex-m7', '-mthumb', '-mfpu=fpv5-d16', '-mfloat-abi=hard',
                  '-fno-exceptions', '-fno-rtti', '-ffunction-sections', '-fdata-sections', '-fstack-usage']

    if not args.arm:
        for stem, sources in (('DescriptorCheck', ['DescriptorCheck.cpp', 'Other.cpp']), ('NoHeap', ['NoHeap.cpp'])):
            program = output / (stem + ('.exe' if os.name == 'nt' else ''))
            run(flags + ['-O2', *[HERE / source for source in sources], '-o', program], 'build-' + stem)
            run([program, *[output / (name + '.bin') for name in ('mixed', 'edge', 'empty')]], 'execute-' + stem)
        counts['parser'] = check(output)
    else:
        directory = Path(shutil.which(args.cxx) or args.cxx).resolve().parent
        suffix = '.exe' if os.name == 'nt' else ''
        objdump, objcopy, nm = (directory / ('arm-none-eabi-' + name + suffix) for name in ('objdump', 'objcopy', 'nm'))
        evidence = {}
        for optimization in ('O2', 'Os', 'Og'):
            object_file = output / (optimization + '-golden.o')
            run(flags + ['-' + optimization, '-DEXTRACT_GOLDENS', '-c', HERE / 'ArmDescriptor.cpp', '-o', object_file], 'build-' + optimization)
            sections = run([objdump, '-h', object_file], 'sections-' + optimization)
            if re.search(r'\.(?:init_array|preinit_array|ctors)\b', sections):
                raise RuntimeError('Descriptor requires startup initialization')
            if re.search(r'\.rodata\._ZN18descriptor_fixture4edgeE\b', sections) is None:
                raise RuntimeError('Expected immutable descriptor not emitted')
            for name in ('mixed', 'edge', 'empty'):
                run([objcopy, '--dump-section', f'.descriptor.{name}={output / (name + ".bin")}', object_file], 'extract-' + optimization + '-' + name)
            counts['parser'] = check(output)
            sizes = {}
            for variant in ('stream', 'packed'):
                program = output / f'{optimization}-{variant}.elf'
                extra = ['-DPACKED_ONLY'] if variant == 'packed' else []
                run(flags + ['-' + optimization, *extra, HERE / 'ArmDescriptor.cpp',
                     '-nostdlib', '-Wl,-e,descriptor_roots', '-Wl,--gc-sections', '-lc', '-lgcc', '-o', program],
                    'link-' + optimization + '-' + variant)
                sections = run([objdump, '-h', program], 'linked-sections-' + optimization + '-' + variant)
                section_sizes = {name: int(size, 16) for name, size in re.findall(r'^\s*\d+\s+(\S+)\s+([0-9a-fA-F]+)\s', sections, re.M)}
                symbols = run([nm, '-C', program], 'linked-symbols-' + optimization + '-' + variant)
                if re.search(r'\b(?:malloc|calloc|realloc|free|operator new|operator delete)\b', symbols):
                    raise RuntimeError('Linked descriptor reader retains dynamic allocation')
                sizes[variant] = {name: section_sizes.get(name, 0) for name in ('.text', '.rodata', '.data', '.bss')}
            usage = object_file.with_suffix('.su').read_text(encoding='utf-8')
            frames = {}
            for line in usage.splitlines():
                if re.search(r'\b(?:descriptor_read|emit|read)\b', line):
                    pieces = line.split('\t')
                    if len(pieces) != 3 or pieces[2] != 'static' or int(pieces[1]) > 96:
                        raise RuntimeError('Descriptor reader frame exceeds 96 B: ' + line)
                    frames[pieces[0]] = int(pieces[1])
            if not frames:
                raise RuntimeError('Missing reader stack evidence')
            evidence[optimization] = dict(sections=sizes, frames=frames)
        (output / 'arm-summary.json').write_text(json.dumps(evidence, indent=2) + '\n')

    negative_flags = [flag for flag in flags if flag != '-fstack-usage']
    for case, diagnostic in DIAGNOSTICS.items():
        run(negative_flags + ['-O2', f'-DCASE={case}', '-fsyntax-only', HERE / 'Negative.cpp'],
            'negative-' + str(case), diagnostic)
    (output / 'summary.json').write_text(json.dumps(counts, indent=2) + '\n')
    print('Stage 09 descriptor checks passed:', counts, flush=True)


if __name__ == '__main__':
    main()
