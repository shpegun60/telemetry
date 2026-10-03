#!/usr/bin/env python3
"""Stage 13: mixed multi-TU consumers, ABI, linked memory and specialization cost.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
Generated evidence stays in the caller-selected directory. Frame limits are
individual compiler frames, not a claimed whole-call-chain stack maximum.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import time

from gates import EXPECTED_CONSUMER_CHECKS, counted_checks, controls, forbidden_symbols

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
LIBRARY = [ROOT / name for name in (
    'lib/telemetry_structured/model/Adapter.cpp',
    'lib/telemetry_structured/abi/StructuredAbi.cpp',
    'lib/resource/structured/detail/Values.cpp')]
CONSUMER = [HERE / (name + '.cpp') for name in ('Check', 'Provider', 'Typed', 'Encoded')]


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
    # Preserve clang++'s driver name: resolving its symlink to clang would
    # silently stop the linker from selecting the C++ runtime libraries.
    compiler = Path(shutil.which(args.cxx) or args.cxx).absolute()
    env = os.environ.copy()
    env['PATH'] = str(compiler.parent) + os.pathsep + env.get('PATH', '')
    report = dict(commands=0, checks=0, link_rejections=0, compile_rejections=0,
                  frames={}, linked_sections={}, scaling=[], depth=[])
    report['gate_controls'] = controls()
    inputs = sorted(path for directory in (ROOT / 'lib', HERE)
                    for path in directory.rglob('*')
                    if path.is_file() and path.suffix in ('.h', '.hpp', '.cpp', '.c', '.pri', '.py', '.pro'))
    inputs += [HERE.parent / 'traversal/Fixture.hpp', HERE.parent / 'endpoints/h7s/Fixture.hpp']

    def input_hashes():
        return {str(path.relative_to(ROOT)).replace('\\', '/'):
                hashlib.sha256(path.read_bytes().replace(b'\r\n', b'\n')).hexdigest() for path in inputs}

    before = input_hashes()

    def run(command, label, diagnostic=''):
        command = list(map(str, command))
        start = time.perf_counter()
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, encoding='utf-8',
                                errors='replace', timeout=180)
        seconds = time.perf_counter() - start
        (out / (label + '.log')).write_text(
            f'COMMAND {command!r}\nEXIT {result.returncode}\nSECONDS {seconds:.6f}\n{result.stdout}',
            encoding='utf-8')
        if diagnostic:
            if not result.returncode or not re.search(diagnostic, result.stdout, re.I | re.S):
                raise RuntimeError(label + ': required link failure missing\n' + result.stdout)
            report['link_rejections' if label.startswith('mismatch-') else 'compile_rejections'] += 1
        elif result.returncode:
            raise RuntimeError(label + ': command failed\n' + result.stdout)
        else:
            report['commands'] += 1
        print(f'{label}: pass ({seconds:.2f}s)', flush=True)
        return result.stdout, seconds

    def execute(program, label, expected):
        text, _ = run([program], label)
        count = counted_checks(text, label, expected)
        report['checks'] += count
        return count

    version, _ = run([compiler, '--version'], 'compiler')
    report['compiler'] = version.splitlines()[0]
    flags = [compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-pedantic-errors',
             '-fdiagnostics-color=never', '-ffunction-sections', '-fdata-sections',
             '-Ilib', '-Ilib/boost_pfr/include', '-Ilib/magic_enum']
    if args.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    if args.null_checks:
        flags += ['-fno-delete-null-pointer-checks']
    if args.arm:
        flags += ['-mcpu=cortex-m7', '-mthumb', '-mfpu=fpv5-d16', '-mfloat-abi=hard',
                  '-fno-exceptions', '-fno-rtti', '-fstack-usage', '-DQUALIFICATION_ARM']
    report['flags'] = list(map(str, flags))
    clang = 'clang' in version.lower()
    suffix = '.exe' if os.name == 'nt' else ''
    link = ['-Wl,--gc-sections']
    if args.arm:
        link += ['-nostdlib', '-Wl,-e,main', '-lc', '-lgcc']
    elif clang:
        link += ['-fuse-ld=lld']
    syntax_flags = [flag for flag in flags if flag != '-fstack-usage']
    run(syntax_flags + ['-O2', '-fsyntax-only', HERE / 'NameNegative.cpp'],
        'null-name-rejected', r'invalidEndpointName')
    run(syntax_flags + ['-O2', '-fsyntax-only', HERE / 'Names.cpp'], 'constexpr-pointer-names')

    def compile_sources(mode, extra=(), source_files=(*CONSUMER, *LIBRARY)):
        objects = []
        for i, source in enumerate(source_files):
            obj = out / f'{mode}-{i}-{source.stem}.o'
            run(flags + list(extra) + ['-c', source, '-o', obj], f'build-{mode}-{i}-{source.stem}')
            objects.append(obj)
        return objects

    if args.arm:
        def tool(name):
            return compiler.with_name('arm-none-eabi-' + name + suffix)

        def inspect(objects, program, mode, include_frames=True):
            symbols, _ = run([tool('nm'), '-C', program], 'symbols-' + mode)
            rejected = forbidden_symbols(symbols, legacy=True)
            if rejected:
                raise RuntimeError(mode + ': allocation/formatting/legacy symbols retained: ' + str(rejected))
            sections, _ = run([tool('size'), '-A', program], 'sections-' + mode)
            parsed = {name: int(size) for name, size in re.findall(r'^(\S+)\s+(\d+)\s+\d+\s*$', sections, re.M)}
            report['linked_sections'][mode] = {kind: sum(size for name, size in parsed.items()
                if name.startswith('.' + kind)) for kind in ('text', 'rodata', 'data', 'bss')}
            if any(name.startswith(('.init_array', '.preinit_array', '.ctors')) and size
                   for name, size in parsed.items()):
                raise RuntimeError(mode + ': metadata acquired startup constructors')
            if not include_frames:
                return  # LTO's .ltrans frames are not the compile-time TU frames.
            frames = {}
            for obj in objects:
                for line in obj.with_suffix('.su').read_text().splitlines():
                    name, frame, kind = line.split('\t')
                    # This whole-consumer wrapper passes a ModelView by value
                    # through four compiled ABI boundaries. Its call argument
                    # area is distinct from a hidden 4 KiB endpoint object.
                    is_root = 'consumer_encoded(' in name or 'consumer_typed(' in name
                    limit = (768 if mode == 'Og' else 512) if is_root else 256
                    if kind != 'static' or int(frame) > limit:
                        raise RuntimeError(mode + ': large/dynamic individual frame: ' + line)
                    frames[name] = int(frame)
            for name in ('consumer_typed', 'consumer_encoded'):
                if not any(name in frame for frame in frames):
                    raise RuntimeError(mode + ': missing consumer frame')
            report['frames'][mode] = frames
            for stem in ('Typed', 'Encoded'):
                obj = next(obj for obj in objects if obj.stem.endswith('-' + stem))
                asm, _ = run([tool('objdump'), '-drC', obj], 'assembly-' + mode + '-' + stem)
                if 'requireStructuredAbi' in asm:
                    raise RuntimeError(mode + ': explicit ABI startup check entered a consumer')

    retained = {}
    for opt in ('O2', 'Os', 'Og'):
        objects = compile_sources(opt, ['-' + opt])
        program = out / (opt + ('.elf' if args.arm else suffix))
        run(flags + ['-' + opt, *objects, *link, '-o', program], 'link-' + opt)
        retained[opt] = objects
        if args.arm:
            inspect(objects, program, opt)
        else:
            execute(program, 'execute-' + opt, EXPECTED_CONSUMER_CHECKS)

    # All categories remain live under LTO/GC. A real policy mismatch in the
    # separately built Adapter must fail at each of its four compiled symbols.
    for mode, extra, linker in (
        ('gc', [], []), ('lto', ['-flto'], ['-flto']),
        ('pic', ['-fPIC'], ['-pie']), ('pie', ['-fPIE'], ['-pie'])):
        if os.name == 'nt' and not args.arm and mode in ('pic', 'pie'):
            continue  # These ELF modes are exercised on Linux/ARM, not PE.
        objects = compile_sources(mode, ['-O2', *extra]) if mode != 'gc' else retained['O2']
        program = out / (mode + ('.elf' if args.arm else suffix))
        run(flags + ['-O2', *objects, *linker, *link, '-o', program], 'link-' + mode)
        if not args.arm:
            execute(program, 'execute-' + mode, EXPECTED_CONSUMER_CHECKS)
        else:
            inspect(objects, program, mode, include_frames=False)
        bad = out / (mode + '-bad-adapter.o')
        run(flags + ['-O2', *extra, '-DTELEMETRY_STRUCTURED_LOCAL_BYTES=0',
                     '-c', LIBRARY[0], '-o', bad], 'bad-adapter-' + mode)
        supporting = [obj for obj in objects if not obj.stem.endswith(('-Check', '-Adapter'))]
        good = next(obj for obj in objects if obj.stem.endswith('-Adapter'))
        for case, symbol in enumerate(('readFieldEncoded', 'writeFieldEncoded',
                                       'executeCommandEncoded', 'callServiceEncoded'), 1):
            app = out / f'{mode}-abi-{case}.o'
            run(flags + ['-O2', *extra, f'-DCASE={case}', '-c', HERE / 'AbiClient.cpp', '-o', app],
                f'abi-client-{mode}-{case}')
            valid = out / f'{mode}-matching-{case}{suffix if not args.arm else ".elf"}'
            run(flags + ['-O2', app, *supporting, good, *linker, *link, '-o', valid],
                f'matching-{mode}-{case}')
            if not args.arm:
                execute(valid, f'execute-matching-{mode}-{case}', 1)
            run(flags + ['-O2', app, *supporting, bad, *linker, *link,
                         '-o', out / f'{mode}-mismatch-{case}.elf'], f'mismatch-{mode}-{case}',
                r'undefined (?:reference|symbol).*' + symbol)

    # Scaling records timings and sections; no machine-dependent compile-time
    # speed requirement is disguised as a correctness gate.
    opts = ('O2', 'Os', 'Og') if args.arm else ('O2',)
    for opt in opts:
        for rows in (32, 128):
            for visitors, reuse in ((0, 0), (1, 0), (2, 0), (4, 0), (4, 1)):
                label = f'scale-{opt}-{rows}-{visitors}-{reuse}'
                program = out / (label + ('.o' if args.arm else suffix))
                extra = ['-' + opt, f'-DROWS={rows}', f'-DVISITORS={visitors}', f'-DREUSE={reuse}']
                if args.arm:
                    extra += ['-c']
                else:
                    extra += ['-DSCALE_EXECUTE']
                # COFF section names use a narrow textual string-table offset.
                # Unique 128-row template targets can exceed it with one
                # named section per function. This host correctness probe
                # needs no section-size evidence; ARM keeps production flags.
                scale_flags = flags + (['-fno-function-sections', '-fno-data-sections']
                                       if os.name == 'nt' else [])
                _, seconds = run(scale_flags + [*extra, HERE / 'Scale.cpp', '-o', program], label)
                record = dict(optimization=opt, rows=rows, visitors=visitors, reuse=bool(reuse), seconds=seconds)
                if args.arm:
                    sections, _ = run([tool('size'), '-A', program], label + '-sections')
                    parsed = {name: int(size) for name, size in re.findall(r'^(\S+)\s+(\d+)\s+\d+\s*$', sections, re.M)}
                    record['sections'] = {kind: sum(size for name, size in parsed.items()
                        if name.startswith('.' + kind)) for kind in ('text', 'rodata', 'data', 'bss')}
                else:
                    record['checks'] = execute(program, label + '-execute', rows * max(1, visitors) + 2)
                report['scaling'].append(record)
        for depth in (4, 8, 16):
            label = f'depth-{opt}-{depth}'
            obj = out / (label + '.o')
            _, seconds = run(flags + ['-' + opt, f'-DDEPTH={depth}', '-c', HERE / 'Depth.cpp', '-o', obj], label)
            report['depth'].append(dict(optimization=opt, depth=depth, seconds=seconds, object_bytes=obj.stat().st_size))

    if args.arm:
        report['scalar_baseline'] = {}
        for opt in opts:
            obj = out / (opt + '-scalar.o')
            run(flags + ['-' + opt, '-fno-ipa-icf', '-c', HERE / 'ScalarComparison.cpp', '-o', obj], 'scalar-' + opt)
            asm, _ = run([tool('objdump'), '-drC', obj], 'scalar-' + opt + '-assembly')
            result = dict(body_bytes={})
            for symbol in ('scalar_direct', 'scalar_new', 'scalar_old', 'write_direct', 'write_new', 'write_old'):
                path = out / (opt + '-' + symbol + '.bin')
                run([tool('objcopy'), '--dump-section', f'.text.{symbol}={path}', obj], 'scalar-' + opt + '-' + symbol)
                result['body_bytes'][symbol] = len(path.read_bytes())
            if opt != 'Og' and not args.null_checks:
                for operation in ('scalar', 'write'):
                    direct = (out / f'{opt}-{operation}_direct.bin').read_bytes()
                    native = (out / f'{opt}-{operation}_new.bin').read_bytes()
                    if not direct or direct != native:
                        raise RuntimeError(opt + ': new scalar native path differs from direct')
            layout = out / (opt + '-layout.bin')
            run([tool('objcopy'), '--dump-section', f'.rodata.qualification_layout={layout}', obj], 'layout-' + opt)
            result['layout'] = list(struct.unpack('<10I', layout.read_bytes()))
            report['scalar_baseline'][opt] = result

    if input_hashes() != before:
        raise RuntimeError('Build inputs changed during qualification; rerun from a stable tree')
    report['input_hashes'] = before
    (out / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('Stage 13 qualification passed:',
          {key: report[key] for key in ('commands', 'checks', 'link_rejections', 'compile_rejections')}, flush=True)


if __name__ == '__main__':
    main()
