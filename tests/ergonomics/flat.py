#!/usr/bin/env python3
"""Qualify one-level native outcomes without changing the existing dispatch.

Host runs retain strict type/status/lifetime controls; Cortex-M7 builds retain
manual, detailed and flat code sections and individual frames. This runner
never accesses hardware. All generated data uses the explicit output directory.
Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
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

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
ABI = ROOT / 'lib/telemetry/abi/StructuredAbi.cpp'
NEGATIVES = {
    **{n: r'no matching (?:member )?function'
       for n in (*range(1, 19), *range(25, 34), 38, 46, 47, 48, 55, 56, 57)},
    **{n: r'(?:use of deleted function|call to deleted (?:member )?function)'
       for n in (*range(19, 25), *range(49, 55))},
    34: r'(?:conversion from|no viable conversion|cannot convert)',
    35: r'(?:cannot convert|no viable conversion)',
    36: r'(?:cannot convert|no viable conversion)',
    37: r'(?:read-only object|not assignable|read-only variable|cannot assign to return value)',
    39: 'ServiceCallResult factory must return the exact native result type',
    40: 'BorrowedServiceCallResult factory must return the exact native result type',
    41: 'ServiceCallResult requires an unqualified payload type or void',
    42: 'BorrowedServiceCallResult requires an unqualified nonvoid payload type',
    43: 'BorrowedServiceCallResult requires an unqualified nonvoid payload type',
    44: 'ServiceCallResult factory must return the exact native result type',
    45: 'BorrowedServiceCallResult factory must return the exact native result type',
}
SCALE_FUNCTIONS = tuple('flat_' + mode + '_' + kind
                        for kind in ('command', 'service')
                        for mode in ('manual', 'native', 'api'))
LARGE_FUNCTIONS = tuple('flat_' + mode + '_' + kind + '_' + scope
                        for kind in ('large', 'borrowed')
                        for scope in ('local', 'global')
                        for mode in ('manual', 'native', 'api'))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--null-checks', action='store_true')
    parser.add_argument('--arm', action='store_true')
    args = parser.parse_args()
    if args.arm and args.sanitize:
        parser.error('Sanitizers require host execution')
    out = args.build_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    (out / 'summary.json').write_text('{"completed": false}\n', encoding='utf-8')
    compiler = Path(shutil.which(args.cxx) or args.cxx).absolute()
    environment = os.environ.copy()
    environment['PATH'] = str(compiler.parent) + os.pathsep + environment.get('PATH', '')
    environment.setdefault('ASAN_OPTIONS', 'detect_leaks=1:detect_stack_use_after_return=1')
    environment.setdefault('UBSAN_OPTIONS', 'halt_on_error=1')
    inputs = sorted(set([Path(__file__).resolve(), ABI,
                         *HERE.glob('Flat*.cpp'),
                         *ROOT.joinpath('lib/telemetry').rglob('*.hpp'),
                         *ROOT.joinpath('lib/telemetry').rglob('*.h')]))
    before = {p.relative_to(ROOT).as_posix(): digest(p) for p in inputs}
    report = {'completed': False, 'commands': 0, 'rejections': 0,
              'arm': args.arm, 'sanitize': args.sanitize, 'null_checks': args.null_checks,
              'compiler': str(compiler), 'compiler_sha256': digest(compiler),
              'source_head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
              'source_dirty': bool(subprocess.check_output(['git', 'status', '--porcelain=v1'], cwd=ROOT, text=True)),
              'measurement_scope': 'Combined manual/detailed/flat fixtures; not incremental production Flash or device cycles',
              'executions': {}, 'images': {}, 'sections': {}, 'frames': {}, 'relocations': {}}
    # Invalidate a previous successful report before starting any new command.
    # A failed or interrupted rerun must never retain completed=true.
    (out / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    objects = []

    def run(command, label, diagnostic=None, stdin=None):
        command = list(map(str, command))
        start = time.monotonic()
        result = subprocess.run(command, cwd=ROOT, env=environment, input=stdin,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, encoding='utf-8', errors='replace', timeout=300)
        (out / (label + '.log')).write_text(
            f'COMMAND {command!r}\nEXIT {result.returncode}\nSECONDS {time.monotonic()-start:.3f}\n{result.stdout}',
            encoding='utf-8')
        report['commands'] += 1
        if diagnostic is not None:
            if result.returncode == 0 or not re.search(diagnostic, result.stdout, re.I | re.S):
                raise RuntimeError(f'{label}: intended rejection missing\n{result.stdout}')
            report['rejections'] += 1
        elif result.returncode:
            raise RuntimeError(f'{label}: failed\n{result.stdout[-3000:]}')
        print(label + ': pass', flush=True)
        return result.stdout

    report['compiler_version'] = run([compiler, '--version'], 'compiler').strip()
    flags = [compiler, '-std=c++20', '-UNDEBUG', '-Wall', '-Wextra', '-Werror', '-pedantic-errors',
             '-fdiagnostics-color=never', '-fno-elide-constructors', '-Ilib',
             '-Ilib/boost_pfr/include', '-Ilib/magic_enum']
    if args.null_checks:
        flags += ['-fno-delete-null-pointer-checks']
    if args.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    if args.arm:
        flags += ['-mcpu=cortex-m7', '-mthumb', '-mfpu=fpv5-d16', '-mfloat-abi=hard',
                  '-fno-exceptions', '-fno-rtti', '-ffunction-sections', '-fdata-sections',
                  '-fstack-usage']
    report['flags'] = list(map(str, flags))
    for n, header in enumerate(('telemetry/result/ServiceCallResult.hpp',
                                'telemetry/result/CommandCallStatus.hpp')):
        run([*flags, '-x', 'c++', '-fsyntax-only', '-'], f'header-{n}', stdin=f'#include <{header}>\n')
    for case, diagnostic in sorted(NEGATIVES.items()):
        run([*flags, '-O2', f'-DCASE={case}', '-fsyntax-only', HERE / 'FlatNegative.cpp'],
            f'negative-{case}', diagnostic)
    opts = ('O2', 'Os', 'Og') if args.arm else ('O2',)
    for opt in opts:
        if args.arm:
            run([*flags, '-' + opt, '-c', HERE / 'FlatCalls.cpp', '-o', out / (opt + '-calls.o')],
                opt + '-calls')
        else:
            image = out / ('calls.exe' if os.name == 'nt' else 'calls')
            run([*flags, '-' + opt, HERE / 'FlatCalls.cpp', ABI, '-o', image], 'build-calls')
            report['executions']['calls'] = run([image], 'execute-calls').strip()
            report['images']['calls'] = {'sha256': digest(image)}
    for count in (128, 256):
        for opt in opts:
            name = f'scale-{opt}-{count}'
            image = out / (name + ('.o' if args.arm else '.exe' if os.name == 'nt' else ''))
            run([*flags, '-' + opt, f'-DTARGETS={count}',
                 *(['-c'] if args.arm else []), HERE / 'FlatScale.cpp',
                 *([] if args.arm else [ABI]), '-o', image], 'build-' + name)
            report['images'][name] = {'sha256': digest(image)}
            if args.arm:
                objects.append(image)
            if not args.arm:
                report['executions'][name] = run([image], 'execute-' + name).strip()
    for size in (1024, 4096):
        for opt in opts:
            name = f'large-{opt}-{size}'
            image = out / (name + ('.o' if args.arm else '.exe' if os.name == 'nt' else ''))
            run([*flags, '-' + opt, f'-DBIG_BYTES={size}',
                 *(['-c'] if args.arm else []), HERE / 'FlatLargeCodegen.cpp',
                 *([] if args.arm else [ABI]), '-o', image], 'build-' + name)
            report['images'][name] = {'sha256': digest(image)}
            if args.arm:
                objects.append(image)
            if not args.arm:
                report['executions'][name] = run([image], 'execute-' + name).strip()

    if args.arm:
        suffix = '.exe' if os.name == 'nt' else ''
        objdump = compiler.parent / ('arm-none-eabi-objdump' + suffix)
        nm = compiler.parent / ('arm-none-eabi-nm' + suffix)
        for obj in objects:
            label = obj.stem
            raw = obj.read_bytes()
            if raw[:6] != b'\x7fELF\x01\x01':
                raise RuntimeError('Expected ARM ELF32 LE object: ' + label)
            header = struct.unpack_from('<16sHHIIIIIHHHHHH', raw)
            table = [struct.unpack_from('<10I', raw, header[6] + n * header[11]) for n in range(header[12])]
            names = table[header[13]]
            strings = raw[names[4]:names[4] + names[5]]
            sections = {}
            for entry in table:
                name = strings[entry[0]:].split(b'\0', 1)[0].decode('utf-8')
                if name.startswith(('.text', '.rodata')) and entry[5]:
                    payload = raw[entry[4]:entry[4] + entry[5]]
                    sections[name] = {'bytes': len(payload), 'sha256': hashlib.sha256(payload).hexdigest()}
            report['sections'][label] = sections
            frames = {}
            for line in obj.with_suffix('.su').read_text(encoding='utf-8').splitlines():
                parts = line.rsplit('\t', 2)
                if len(parts) == 3:
                    name = re.sub(r'^.*?:\d+:\d+:', '', parts[0])
                    frames[name] = {'bytes': int(parts[1]), 'kind': parts[2]}
            report['frames'][label] = frames
            if label.startswith('large-') and (not frames or any(
                    x['bytes'] > 256 or x['kind'] != 'static' for x in frames.values())):
                raise RuntimeError(label + ': response-sized stack frame detected')
            symbols = run([nm, '-u', obj], 'undefined-' + label)
            if re.search(r'\b(?:_Zn[aw]|(?:__wrap_)?(?:malloc|calloc|realloc|aligned_alloc|posix_memalign)|(?:__aeabi_)?mem(?:cpy|move))', symbols):
                raise RuntimeError(label + ': allocation or hidden payload copy symbol')
            functions = SCALE_FUNCTIONS if label.startswith('scale-') else LARGE_FUNCTIONS
            for function in functions:
                if '.text.' + function not in sections:
                    raise RuntimeError(label + ': missing measured body ' + function)
                if not any(re.search(r'\b' + function + r'\(', name) for name in frames):
                    raise RuntimeError(label + ': missing measured frame ' + function)
            # ELF relocation targets are separate evidence from raw code bytes.
            # Equal instruction sections alone do not prove identical callees.
            def string_at(payload, offset):
                return payload[offset:].split(b'\0', 1)[0].decode('utf-8')

            relocations = {}
            for entry in table:
                if entry[1] not in (4, 9):  # SHT_RELA / SHT_REL
                    continue
                target = string_at(strings, table[entry[7]][0])
                if target not in {'.text.' + name for name in functions}:
                    continue
                symtab = table[entry[6]]
                symstr = table[symtab[6]]
                payload = raw[symstr[4]:symstr[4] + symstr[5]]
                rows = []
                for offset in range(entry[4], entry[4] + entry[5], entry[9]):
                    address, info = struct.unpack_from('<II', raw, offset)
                    symbol = struct.unpack_from('<IIIBBH', raw, symtab[4] + (info >> 8) * symtab[9])
                    symbol_name = string_at(payload, symbol[0])
                    if not symbol_name and symbol[5] < len(table):
                        symbol_name = string_at(strings, table[symbol[5]][0])
                    row = {'offset': address, 'type': info & 255, 'symbol': symbol_name}
                    if entry[1] == 4:
                        row['addend'] = struct.unpack_from('<i', raw, offset + 8)[0]
                    rows.append(row)
                relocations[target] = rows
            report['relocations'][label] = relocations
            if label.startswith(('large-O2-', 'large-Os-')):
                # Final-storage construction must not introduce a new large
                # response move or a wider entrypoint in optimized builds.
                for kind in ('large', 'borrowed'):
                    for scope in ('local', 'global'):
                        old = sections[f'.text.flat_native_{kind}_{scope}']
                        new = sections[f'.text.flat_api_{kind}_{scope}']
                        if old != new:
                            raise RuntimeError(label + ': old/flat large entrypoint bytes differ')
            if label.startswith('scale-'):
                # Bound output: GNU objdump scans huge mangled names even for
                # one named function, so collect all six in one invocation.
                selected = [argument for name in functions for argument in ('-j', '.text.' + name)]
                disassembly = run([objdump, '-dr', *selected, obj], 'disassembly-' + label)
                bodies = []
                for function in functions:
                    match = re.search(r'^\w+ <' + function + r'>:\n.*?(?=\nDisassembly of section|\Z)',
                                      disassembly, re.M | re.S)
                    if not match:
                        raise RuntimeError(label + ': missing named disassembly ' + function)
                    bodies.append(match.group())
                (out / ('named-' + label + '.log')).write_text('\n'.join(bodies), encoding='utf-8')
            else:
                run([objdump, '-dr', obj], 'disassembly-' + label)
    after = {p.relative_to(ROOT).as_posix(): digest(p) for p in inputs}
    if before != after:
        raise RuntimeError('Source/test inputs changed during qualification')
    report['input_hashes'] = before
    report['completed'] = True
    (out / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f"Flat native: {report['commands']} commands, {report['rejections']} intended rejections; complete", flush=True)


if __name__ == '__main__':
    main()
