#!/usr/bin/env python3
"""Typed get/forEach/visit, native As access and ARM storage/codegen checks. MIT."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
    parser.add_argument('--arm', action='store_true')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--null-checks', action='store_true')
    parser.add_argument('--build-dir', type=Path, required=True)
    args = parser.parse_args()
    out = args.build_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    counts = {'passed': 0, 'rejected': 0}

    def run(command, label, diagnostic=''):
        start = time.perf_counter()
        command = list(map(str, command))
        result = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace', timeout=180)
        log = f'COMMAND {command!r}\nEXIT {result.returncode}\n{result.stdout}'
        (out / (label + '.log')).write_text(log, encoding='utf-8')
        if diagnostic:
            if not result.returncode or not re.search(diagnostic, result.stdout, re.I | re.S):
                raise RuntimeError(f'{label}: expected diagnostic missing\n{log}')
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
    if args.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    if args.null_checks:
        flags += ['-fno-delete-null-pointer-checks']
    if args.arm:
        flags += ['-mcpu=cortex-m7', '-mthumb', '-mfpu=fpv5-d16', '-mfloat-abi=hard',
            '-fno-exceptions', '-fno-rtti', '-ffunction-sections', '-fdata-sections', '-fstack-usage']

    for header in ('Traversal', 'FieldAccess'):
        source = out / (header + '-standalone.cpp')
        source.write_text(f'#include <telemetry_structured/detail/{header}.hpp>\n')
        run([flag for flag in flags if flag != '-fstack-usage'] + ['-fsyntax-only', source],
            'header-' + header)

    for stem in ('Check', 'NoHeap'):
        if args.arm:
            run(flags + ['-O2', '-c', HERE / (stem + '.cpp'), '-o', out / (stem + '.o')], 'compile-' + stem)
        else:
            exe = out / (stem + ('.exe' if os.name == 'nt' else ''))
            run(flags + ['-O2', HERE / (stem + '.cpp'), '-o', exe], 'compile-' + stem)
            run([exe], 'execute-' + stem)

    if not args.arm:
        # A retained visitor executes after LTO/section GC. Clang needs lld on
        # hosts without LLVMgold; suppress its compile-only linker warning.
        version = subprocess.check_output([args.cxx, '--version'], text=True)
        linker = ['-fuse-ld=lld', '-Wno-unused-command-line-argument'] if 'clang' in version.lower() else []
        exe = out / ('lto-check' + ('.exe' if os.name == 'nt' else ''))
        run(flags + ['-O2', '-flto', '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
            *linker, HERE / 'Check.cpp', '-o', exe], 'build-lto')
        run([exe], 'execute-lto')

    no_match = r'no matching (?:member )?function|no viable overloaded'
    diagnostics = {i: r'deleted.*(?:function|member)|(?:function|member).*deleted' for i in range(1, 37)}
    diagnostics.update({37: 'Field position is outside', 38: 'Command position is outside',
        39: 'Service position is outside', 40: 'Field group is outside',
        41: 'Command group is outside', 42: 'Service group is outside',
        43: 'Packed ID must be an integer', **{i: no_match for i in range(44, 49)},
        49: 'Field readAs permits numeric conversion', 50: 'Field writeAs permits numeric conversion',
        51: 'Field readAs permits numeric conversion', 52: 'Field position is outside',
        53: 'Typed table position must be non-negative',
        **{i: r'deleted.*(?:function|member)|(?:function|member).*deleted' for i in range(54, 62)},
        62: 'Packed ID is outside the 32-bit range', 63: 'Command position is outside',
        64: 'Service position is outside'})
    for case, diagnostic in diagnostics.items():
        run([flag for flag in flags if flag != '-fstack-usage'] +
            ['-O2', f'-DCASE={case}', '-fsyntax-only', HERE / 'Negative.cpp'],
            f'negative-{case}', diagnostic)

    if args.arm:
        tools = Path(shutil.which(args.cxx) or args.cxx).resolve().parent
        suffix = '.exe' if os.name == 'nt' else ''
        objdump = tools / ('arm-none-eabi-objdump' + suffix)
        objcopy = tools / ('arm-none-eabi-objcopy' + suffix)
        size = tools / ('arm-none-eabi-size' + suffix)
        results = {}
        for opt in ('O2', 'Os', 'Og'):
            obj = out / (opt + '-Arm.o')
            # Disable identical-function folding only in this comparison probe:
            # otherwise -Os may replace equivalent exported wrappers with tail
            # branches, obscuring the body whose instructions are compared.
            run(flags + ['-' + opt, '-fno-ipa-icf', '-c', HERE / 'Arm.cpp', '-o', obj], 'compile-' + opt)
            asm = run([objdump, '-drC', obj], 'assembly-' + opt)
            # Scalar conversion templates may be included, but no Scalar object
            # or erased legacy dispatch may appear in the emitted native code.
            assert not re.search(r'telemetry::(?:Scalar|Getter|Setter)::', asm)
            record = {}
            if opt != 'Og' and not args.null_checks:
                for operation, routes in (
                    ('read', ('direct', 'get', 'global_get', 'as', 'global_as')),
                    ('write', ('direct', 'get', 'as', 'global_as')),
                    ('command', ('direct', 'get', 'global_get')),
                    ('service', ('direct', 'get', 'global_get'))):
                    payloads = []
                    for route in routes:
                        symbol = operation + '_' + route
                        binary = out / (opt + '-' + symbol + '.bin')
                        run([objcopy, '--dump-section', f'.text.{symbol}={binary}', obj], 'bytes-' + opt + '-' + symbol)
                        payloads.append(binary.read_bytes())
                    if not payloads[0] or any(p != payloads[0] for p in payloads):
                        raise RuntimeError(f'{opt}: {operation} direct/get/As code differs')
                    record[operation + '_identical_bytes'] = len(payloads[0])
            frames = {}
            for line in (out / (opt + '-Arm.su')).read_text().splitlines():
                match = re.search(r'\b(visit_local|visit_global|visit_large_service|get_large_name)\([^\t]*\t(\d+)\t', line)
                if match:
                    frames[match[1]] = int(match[2])
            assert len(frames) == 4 and max(frames.values()) <= 64, frames
            record['visitor_frames'] = frames
            scales = []
            for rows in (32, 128):
                pair = []
                for visitor in (0, 1):
                    scale = out / f'{opt}-scale-{rows}-{visitor}.o'
                    label = f'scale-{opt}-{rows}-{visitor}'
                    run(flags + ['-' + opt, f'-DROWS={rows}', f'-DWITH_VISITOR={visitor}',
                        '-c', HERE / 'Scale.cpp', '-o', scale], label)
                    sections = run([size, '-A', scale], label + '-sections')
                    parsed = {name: int(count) for name, count in re.findall(r'^(\S+)\s+(\d+)\s+\d+\s*$', sections, re.M)}
                    pair.append({kind: sum(count for name, count in parsed.items() if name.startswith('.' + kind))
                        for kind in ('text', 'rodata', 'data', 'bss')})
                scales.append({'rows': rows, 'erased': pair[0], 'visitor': pair[1],
                    'delta': {kind: pair[1][kind] - pair[0][kind] for kind in pair[0]}})
            record['object_sections'] = scales
            results[opt] = record
        (out / 'arm-results.json').write_text(json.dumps(results, indent=2) + '\n')
    (out / 'summary.json').write_text(json.dumps(counts, indent=2) + '\n')
    print('Typed traversal and As checks passed:', counts, flush=True)


if __name__ == '__main__':
    main()
