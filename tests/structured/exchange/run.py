#!/usr/bin/env python3
"""Optional Bind/Exchange correctness, ABI, bounded integration and offline MCU gates. MIT."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time
from oracle import check

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BIND = ROOT / 'examples/structured_protocol/Bind.cpp'
EXCHANGE = ROOT / 'examples/structured_protocol/Exchange.cpp'
VALUES = ROOT / 'lib/resource/telemetry/v3/detail/Values.cpp'
PROTOCOL = ROOT / 'lib/resource/protocol/Protocol.cpp'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
    parser.add_argument('--arm', action='store_true')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--null-checks', action='store_true')
    parser.add_argument('--abi-only', action='store_true', help='Rerun only the compiled-boundary checks')
    parser.add_argument('--build-dir', type=Path, required=True)
    args = parser.parse_args()
    out = args.build_dir.resolve(); out.mkdir(parents=True, exist_ok=True)
    counts = dict(passed=0, rejected=0)

    def run(command, label, diagnostic=None):
        command = list(map(str, command)); start = time.perf_counter()
        result = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, encoding='utf-8', errors='replace', timeout=180)
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
             '-fdiagnostics-color=never', '-Ilib', '-Iexamples', '-Ilib/boost_pfr/include', '-Ilib/magic_enum']
    if args.null_checks: flags += ['-fno-delete-null-pointer-checks']
    if args.sanitize: flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    if args.arm:
        flags += ['-mcpu=cortex-m7', '-mthumb', '-mfpu=fpv5-d16', '-mfloat-abi=hard',
                  '-fno-exceptions', '-fno-rtti', '-ffunction-sections', '-fdata-sections', '-DARM_PROBE']

    if not args.abi_only and not args.arm:
        for stem in ('Check', 'Transport', 'NoHeap'):
            budgets = (0, 16, 32, 64) if stem == 'Check' else (32,)
            for budget in budgets:
                label = stem + '-' + str(budget)
                exe = out / (label + ('.exe' if os.name == 'nt' else ''))
                sources = [HERE / (stem + '.cpp'), BIND, EXCHANGE]
                if stem == 'Transport': sources += [VALUES, PROTOCOL]
                run(flags + ['-O2', f'-DTELEMETRY_STRUCTURED_LOCAL_BYTES={budget}', *sources, '-o', exe], 'build-' + label)
                run([exe, out / 'wire.bin'], 'execute-' + label)
                if stem == 'Check': counts['wire_bytes'] = check(out / 'wire.bin')
    elif not args.abi_only:
        folder = Path(shutil.which(args.cxx) or args.cxx).resolve().parent
        suffix = '.exe' if os.name == 'nt' else ''
        nm, objdump = [folder / ('arm-none-eabi-' + name + suffix) for name in ('nm', 'objdump')]
        evidence = {}
        for opt in ('O2', 'Os', 'Og'):
            objects = []
            for label, source in (('roots', HERE / 'Arm.cpp'), ('bind', BIND), ('exchange', EXCHANGE)):
                obj = out / (opt + '-' + label + '.o'); objects.append(obj)
                run(flags + ['-' + opt, '-fstack-usage', '-c', source, '-o', obj], 'build-' + opt + '-' + label)
                sections = run([objdump, '-h', obj], 'sections-' + opt + '-' + label)
                if re.search(r'\.(?:init_array|preinit_array|ctors)\b', sections):
                    raise RuntimeError('Static Bind/Exchange model requires startup code')
                if label == 'exchange':
                    symbols = run([nm, '-C', obj], 'data-symbols-' + opt)
                    if re.search(r'\b(?:Bind::|Descriptor|fingerprint|hash|fnv)', symbols, re.I):
                        raise RuntimeError('Ordinary Exchange depends on descriptor agreement/hash')
            exe = out / (opt + '.elf')
            run(flags + [*objects, '-nostdlib', '-Wl,-e,exchange_roots', '-Wl,--gc-sections', '-lc', '-lgcc', '-o', exe], 'link-' + opt)
            symbols = run([nm, '-C', exe], 'symbols-' + opt)
            if re.search(r'\b(?:malloc|calloc|realloc|free|operator new|operator delete|printf|to_chars|Scalar::)\b', symbols):
                raise RuntimeError('Unexpected allocation/formatting/legacy conversion retained')
            sections = run([objdump, '-h', exe], 'linked-sections-' + opt)
            run([objdump, '-drC', exe], 'assembly-' + opt)
            sizes = {n: int(s, 16) for n, s in re.findall(r'^\s*\d+\s+(\S+)\s+([0-9a-fA-F]+)\s', sections, re.M)}
            frames = {}
            for obj in objects:
                for line in obj.with_suffix('.su').read_text().splitlines():
                    name, size, kind = line.split('\t')
                    limit = 144 if opt == 'Og' else 112
                    if kind != 'static' or int(size) > limit:
                        raise RuntimeError(f'Exchange frame exceeded {limit} B: ' + line)
                    frames[name] = int(size)
            assert any('Exchange::processImpl(' in n for n in frames)
            evidence[opt] = dict(frames=frames, sections={s: sizes.get(s, 0) for s in ('.text', '.rodata', '.data', '.bss')})
        (out / 'arm-summary.json').write_text(json.dumps(evidence, indent=2) + '\n')
        for stem in ('Check', 'Transport'):
            run(flags + ['-O2', '-c', HERE / (stem + '.cpp'), '-o', out / (stem + '.o')], 'compile-' + stem)

    for case in (() if args.abi_only else range(1, 11)):
        diagnostic = r'(?:no matching|no viable).*process' if case <= 6 else r'(?:deleted).*Binding'
        run(flags + ['-O2', f'-DCASE={case}', '-fsyntax-only', HERE / 'Negative.cpp'], 'negative-' + str(case), diagnostic)

    # Call both compiled boundaries under GC/LTO. A mismatch in either object
    # must not be hidden by the other one's matching policy or by dead stripping.
    for lto in (False, True):
        label = 'abi-lto' if lto else 'abi-gc'
        extra = ['-flto'] if lto else []
        if lto and 'clang' in Path(args.cxx).name:
            extra += ['-fuse-ld=lld', '-Wno-unused-command-line-argument']
        options = flags + ['-O2', '-ffunction-sections', '-fdata-sections', *extra]
        app = out / (label + '-app.o')
        run(options + ['-DABI_MAIN', '-c', HERE / 'Arm.cpp', '-o', app], label + '-app')
        good, bad = [], []
        for name, source in (('bind', BIND), ('exchange', EXCHANGE)):
            a = out / (label + '-' + name + '-good.o'); b = out / (label + '-' + name + '-bad.o')
            good.append(a); bad.append(b)
            run(options + ['-c', source, '-o', a], label + '-' + name + '-good')
            run(options + ['-DTELEMETRY_STRUCTURED_LOCAL_BYTES=0', '-c', source, '-o', b], label + '-' + name + '-bad')
        linking = ['-nostdlib', '-Wl,-e,exchange_roots', '-lc', '-lgcc'] if args.arm else []
        run(options + [app, *good, '-Wl,--gc-sections', *linking, '-o', out / (label + '.elf')], label + '-match')
        for i, name in enumerate(('Bind', 'Exchange')):
            objects = list(good); objects[i] = bad[i]
            run(options + [app, *objects, '-Wl,--gc-sections', *linking, '-o', out / (label + '-' + name + '-mismatch.elf')],
                label + '-' + name + '-mismatch', rf'undefined (?:reference|symbol).*{name}::processImpl')

    (out / 'summary.json').write_text(json.dumps(counts, indent=2) + '\n')
    print('Stage 11 checks passed:', counts, flush=True)


if __name__ == '__main__':
    main()
