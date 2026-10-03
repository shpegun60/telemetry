#!/usr/bin/env python3
"""Borrowed Field/Service qualification, without device access. MIT.

Authors: Ruslan Kovtun (shpegun60), codexAi.
Each invocation requires a fresh output directory. ARM executes no C++ probes.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import runpy
import shutil
import struct
import subprocess
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SUITE = ('Result', 'Native', 'Encoded', 'Status', 'Wire', 'AddressProbe')
BUDGETS = (0, 16, 32, 64)
COUNTS = {'Result': 24, 'Native': 125 if os.name == 'nt' else 127,
          'Encoded': 201, 'Status': 215, 'Wire': 27, 'AddressProbe': 53}
LIBRARY = ('lib/telemetry/model/Adapter.cpp', 'lib/telemetry/abi/StructuredAbi.cpp',
           'lib/resource/telemetry/v3/detail/Values.cpp')
GATES = 'tests/structured/qualification/gates.py'
SYMBOL_CONTROL = 'tests/structured/qualification/SymbolControl.cpp'
FACTORY = r'(?:no matching|no viable|constraints not satisfied|deleted).*\b{}\b'
DIAGNOSTICS = {**{n: FACTORY.format('from') for n in range(1, 9)},
               **{n: FACTORY.format('success') for n in range(9, 17)}}
DIAGNOSTICS.update({17: 'Structured wire structs must be standard-layout trivial aggregates',
    **{n: 'Response must be a value or void' for n in (*range(18, 24), *range(26, 32), 35)},
    24: 'Field getter must return an unqualified native value',
    25: 'Field getter must return an unqualified native value',
    32: 'Service response must be an aggregate struct or void',
    33: 'Service response must be an aggregate struct or void',
    34: 'Response must be a value or void',
    36: 'Structured wire structs must be standard-layout trivial aggregates',
    37: 'Structured endpoints must be noexcept', 38: 'Structured endpoints must be noexcept',
    39: 'Field setter must accept the exact getter type',
    40: 'Request must be by value or const lvalue reference',
    41: 'Field setter must return telemetry::WriteResult',
    42: 'Request must be by value or const lvalue reference',
    43: 'Request must be by value or const lvalue reference',
    44: 'Service request must be an aggregate struct or void',
    45: 'Use one request structure',
    46: r'(?:no matching|no viable|constraints not satisfied|deleted).*\bfield\b',
    47: r'deleted.*\bservice\b',
    **{n: 'BorrowedValue requires an unqualified object type' for n in range(48, 52)},
    52: 'Service must return Response, void or the exact ServiceResult<Response>, BorrowedServiceResult<Response>, or const Response&',
    53: 'Service response must be an aggregate struct or void',
    54: 'Service response must be an aggregate struct or void',
    **{n: 'BorrowedValue requires an unqualified object type' for n in range(55, 58)},
    58: FACTORY.format('from'), 59: FACTORY.format('success'),
    **{n: 'Service response payload must be an unqualified native type' for n in (60, 61, 62)},
    63: 'Borrowed Service response must be a non-void aggregate'})


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def inputs():
    paths = {p for directory in (ROOT / 'lib', HERE) for p in directory.rglob('*')
             if p.is_file() and p.suffix in ('.hpp', '.h', '.cpp', '.c', '.pri', '.py')}
    paths.update(ROOT / p for p in (GATES, SYMBOL_CONTROL))
    return {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes().replace(b'\r\n', b'\n')).hexdigest()
            for p in sorted(paths)}


def counted(text, expected, *, failures=0):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError('Duplicate count key')
            result[key] = value
        return result
    result = json.loads(text, object_pairs_hook=unique)
    if (not isinstance(result, dict) or set(result) != {'checks', 'failures'} or
            type(result['checks']) is not int or type(result['failures']) is not int or
            result['checks'] != expected or result['failures'] != failures):
        raise ValueError('Wrong condition count or failures: ' + repr(result))
    return result


def parser_controls():
    counted('{"checks":24,"failures":0}', 24)
    examples = ('{"checks":true,"failures":0}', '{"checks":24,"failures":false}',
                '{"checks":23,"failures":0}', '{"checks":24,"failures":1}',
                '{"checks":24}', '{"checks":24,"failures":0,"extra":1}',
                '{"checks":24,"checks":24,"failures":0}', '[24,0]',
                '{"checks":24,"failures":0} trailing',
                '{"checks":24.0,"failures":0}')
    for example in examples:
        try:
            counted(example, 24)
        except (ValueError, TypeError):
            continue
        raise AssertionError('Invalid count report accepted')
    return {'accepted': 1, 'rejected': len(examples)}


def frames(text, ceiling=192):
    result = {}
    for line in text.splitlines():
        name, size, kind = line.rsplit('\t', 2)
        if kind != 'static' or not 0 <= int(size) <= ceiling:
            raise RuntimeError('Borrowed root has a large/dynamic individual frame: ' + line)
        result[name] = int(size)
    if not result:
        raise RuntimeError('No compiler frames recorded')
    return result


def same_bodies(bodies):
    if not bodies[0] or any(body != bodies[0] for body in bodies):
        raise RuntimeError('Native borrowed path differs from the manual pointer/view baseline')


def relocations(text, symbol):
    section = '.text.' + symbol
    header = 'RELOCATION RECORDS FOR [' + section + ']:'
    blocks = text.split(header)
    if len(blocks) != 2:
        raise RuntimeError('Expected exactly one relocation block for ' + symbol)
    result = []
    for line in blocks[1].split('\n\n', 1)[0].splitlines():
        match = re.fullmatch(r'([0-9a-fA-F]+)\s+(R_\S+)\s+(\S+)', line.strip())
        if match:
            offset, kind, target = match.groups()
            if target == symbol or target.startswith(symbol + '+') or target == section or target.startswith(section + '+'):
                target = '$self' + target.removeprefix(section).removeprefix(symbol)
            result.append((int(offset, 16), kind, target))
    if not result:
        raise RuntimeError('No native owner/function relocation found for ' + symbol)
    return result


def same_relocations(rows):
    if not rows[0] or any(row != rows[0] for row in rows):
        raise RuntimeError('Native borrowed relocations differ from the manual pointer/view baseline')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
    parser.add_argument('--arm', action='store_true')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--null-checks', action='store_true')
    parser.add_argument('--build-dir', type=Path, required=True)
    args = parser.parse_args()
    if args.arm and args.sanitize:
        parser.error('ARM qualification is compile/link only')
    out = args.build_dir.absolute()
    if out.exists() and any(out.iterdir()):
        parser.error('Use a fresh build directory; existing reports are never overwritten')
    out.mkdir(parents=True, exist_ok=True)
    compiler = Path(shutil.which(args.cxx) or args.cxx).absolute()
    if not compiler.is_file():
        parser.error('C++ compiler is not a file')
    before = inputs()
    captured = out / 'inputs'
    for relative, digest in before.items():
        destination = captured / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        data = (ROOT / relative).read_bytes().replace(b'\r\n', b'\n')
        if hashlib.sha256(data).hexdigest() != digest:
            raise RuntimeError('Input changed during capture: ' + relative)
        destination.write_bytes(data)
    gates = runpy.run_path(str(captured / GATES))
    report = {'format_version': 1, 'scope': 'borrowed native/encoded Field and Service qualification',
              'captured_at_utc': datetime.now(timezone.utc).isoformat(),
              'source_head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
              'source_status': subprocess.check_output(['git', 'status', '--porcelain', '-uall'], cwd=ROOT, text=True),
              'input_lf_sha256': before, 'compiler_path': str(compiler), 'compiler_sha256': sha(compiler),
              'execution': 'compile/link only' if args.arm else 'host',
              'commands': 0, 'checks': 0, 'compile_rejections': 0, 'runtime_rejections': 0,
              'parser_controls': parser_controls(), 'runs': [], 'arm': {}, 'completed': False}
    report['source_dirty'] = bool(report['source_status'])
    environment = os.environ.copy()
    environment['PATH'] = str(compiler.parent) + os.pathsep + environment.get('PATH', '')

    def run(command, label, diagnostic=None, *, rejected=False):
        command = list(map(str, command))
        started = time.perf_counter()
        result = subprocess.run(command, cwd=ROOT, env=environment, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace', timeout=180)
        log = f'COMMAND {command!r}\nEXIT {result.returncode}\nSECONDS {time.perf_counter()-started:.6f}\n{result.stdout}'
        (out / (label + '.log')).write_text(log, encoding='utf-8')
        if diagnostic is not None:
            if result.returncode == 0 or re.search(diagnostic, result.stdout, re.I | re.S) is None:
                raise RuntimeError(label + ': intended diagnostic missing\n' + log)
            report['compile_rejections'] += 1
        elif rejected:
            if result.returncode == 0:
                raise RuntimeError(label + ': required runtime rejection missing')
            report['runtime_rejections'] += 1
        elif result.returncode:
            raise RuntimeError(label + ': failed\n' + log)
        report['commands'] += 1
        print(label + ': pass', flush=True)
        return result.stdout

    version = run([compiler, '--version'], 'compiler').splitlines()[0]
    report['compiler'] = version
    flags = [compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-pedantic-errors', '-fdiagnostics-color=never',
             '-ffunction-sections', '-fdata-sections', '-I' + str(captured / 'lib'),
             '-I' + str(captured / 'lib/boost_pfr/include'), '-I' + str(captured / 'lib/magic_enum')]
    if args.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    if args.null_checks:
        flags += ['-fno-delete-null-pointer-checks']
    if args.arm:
        flags += ['-mcpu=cortex-m7', '-mthumb', '-mfpu=fpv5-d16', '-mfloat-abi=hard', '-fno-exceptions', '-fno-rtti',
                  '-fstack-usage', '-DBORROWED_ARM']
    report['flags'] = list(map(str, flags))
    options = ('O2', 'Os', 'Og') if args.arm else ('O2',)
    suffix = '.exe' if os.name == 'nt' else ''
    def tool(name):
        path = compiler.with_name(('arm-none-eabi-' if args.arm else '') + name + suffix)
        resolved = str(path) if path.exists() else shutil.which(name)
        if not resolved:
            raise RuntimeError('Required compiler companion is missing: ' + name)
        return resolved
    source = lambda name: captured / HERE.relative_to(ROOT) / (name + '.cpp')
    for opt in options:
        for budget in BUDGETS:
            common = flags + ['-' + opt, f'-DTELEMETRY_STRUCTURED_LOCAL_BYTES={budget}']
            libraries = []
            if not args.arm:
                for relative in LIBRARY:
                    obj = out / f'{budget}-{opt}-{Path(relative).stem}.o'
                    run(common + ['-c', captured / relative, '-o', obj], obj.stem)
                    libraries.append(obj)
            for name in SUITE:
                label = f'{budget}-{opt}-{name}'
                obj = out / (label + '.o')
                run(common + ['-c', source(name), '-o', obj], 'compile-' + label)
                row = {'budget': budget, 'optimization': opt, 'probe': name,
                       'object_sha256': sha(obj), 'execution': report['execution'], 'checks': 0}
                if not args.arm:
                    executable = out / (label + suffix)
                    extra = libraries if name == 'Wire' else []
                    linking = ['-fuse-ld=lld'] if 'clang' in version.lower() else []
                    run(common + [obj, *extra, *linking, '-o', executable], 'link-' + label)
                    row['executable_sha256'] = sha(executable)
                    arguments = []
                    if name == 'Wire':
                        arguments = [out / (label + '-wire.bin')]
                    result = counted(run([executable, *arguments], 'execute-' + label), COUNTS[name])
                    row['checks'] = result['checks']
                    report['checks'] += result['checks']
                    if name == 'Wire':
                        row['wire_sha256'] = sha(arguments[0])
                    if name == 'Result' and budget == 32:
                        counted(run([executable, 'allocation-control'], label + '-allocation-control', rejected=True), COUNTS[name], failures=1)
                        if os.name != 'nt':
                            for status in ('ok-control', 'invalid-control'):
                                run([executable, status], label + '-' + status, rejected=True)
                        else:
                            row['failure_factory_runtime_controls'] = 'performed by ELF host roles; no desktop abort dialogs requested'
                report['runs'].append(row)
    syntax = [flag for flag in flags if flag != '-fstack-usage']
    for case, diagnostic in DIAGNOSTICS.items():
        run(syntax + ['-O2', f'-DCASE={case}', '-fsyntax-only', source('Negative')], 'negative-' + str(case), diagnostic)

    if args.arm:
        report['linked_symbol_control'] = gates['linked_control'](flags, out, run, tool('nm'))
        for opt in options:
            common = flags + ['-' + opt, '-fno-ipa-icf']
            obj = out / (opt + '-Arm.o')
            run(common + ['-c', source('Arm'), '-o', obj], 'compile-' + opt + '-Arm')
            assembly = run([tool('objdump'), '-drC', obj], 'assembly-' + opt)
            relocation_text = run([tool('objdump'), '-r', obj], 'relocations-' + opt)
            usage = obj.with_suffix('.su')
            row = {'object_sha256': sha(obj), 'stack_usage_sha256': sha(usage),
                   'frame_scope': 'isolated native/Entry/thunk roots, individual static frames <=192 bytes',
                   'frames': frames(usage.read_text(encoding='utf-8')), 'native_body_bytes': {},
                   'native_relocations': {}}
            for family in ('field', 'service', 'status'):
                for size in ('kib4', 'kib64'):
                    bodies = []
                    native_relocations = []
                    for route in ('direct', 'local', 'global'):
                        symbol = f'{family}_{route}_{size}'
                        binary = out / f'{opt}-{symbol}.bin'
                        run([tool('objcopy'), '--dump-section', f'.text.{symbol}={binary}', obj], 'body-' + opt + '-' + symbol)
                        bodies.append(binary.read_bytes())
                        row['native_body_bytes'][symbol] = {'bytes': len(bodies[-1]), 'sha256': sha(binary)}
                        native_relocations.append(relocations(relocation_text, symbol))
                        row['native_relocations'][symbol] = native_relocations[-1]
                    if opt != 'Og' and not args.null_checks:
                        same_bodies(bodies)
                        same_relocations(native_relocations)
            layout = out / (opt + '-layout.bin')
            run([tool('objcopy'), '--dump-section', f'.rodata.borrowed_layout={layout}', obj], 'layout-' + opt)
            row['layout'] = list(struct.unpack('<12I', layout.read_bytes()))
            if row['layout'] != [4, 4, 4, 4, 8, 4, 8, 4, 32, 4, 24, 28]:
                raise RuntimeError('Borrowed ARM layout changed: ' + repr(row['layout']))
            if re.search(r'\.(?:init_array|preinit_array|ctors)\b', run([tool('objdump'), '-h', obj], 'sections-' + opt)):
                raise RuntimeError('Borrowed metadata requires startup constructors')
            model_obj = out / (opt + '-ModelRoots.o')
            run(common + ['-c', source('ModelRoots'), '-o', model_obj], 'compile-' + opt + '-ModelRoots')
            run([tool('objdump'), '-drC', model_obj], 'assembly-' + opt + '-ModelRoots')
            model_usage = model_obj.with_suffix('.su')
            row['compiled_model_wrappers'] = {
                'object_sha256': sha(model_obj), 'stack_usage_sha256': sha(model_usage),
                'frame_scope': 'compiled ModelView by-value wrapper boundary, individual static frames <=320 bytes',
                'frames': frames(model_usage.read_text(encoding='utf-8'), 320)}
            libraries = []
            for relative in LIBRARY:
                library_obj = out / (opt + '-' + Path(relative).stem + '.o')
                run(common + ['-c', captured / relative, '-o', library_obj], 'compile-' + library_obj.stem)
                libraries.append(library_obj)
            row['library_objects_sha256'] = {p.name: sha(p) for p in libraries}
            image = out / (opt + '-borrowed.elf')
            run(common + [obj, *libraries, '-nostdlib', '-Wl,-e,borrowed_roots', '-Wl,--gc-sections',
                          '-lc', '-lgcc', '-o', image], 'link-' + opt)
            symbols = run([tool('nm'), '-C', image], 'symbols-' + opt)
            if gates['forbidden_symbols'](symbols):
                raise RuntimeError('Borrowed linked roots retain allocation/formatting/value-erasure symbols')
            row['elf_sha256'] = sha(image)
            report['arm'][opt] = row
        control = out / 'frame-control.o'
        run(flags + ['-O2', '-DBORROWED_FRAME_CONTROL', '-c', source('Arm'), '-o', control], 'frame-control')
        try:
            frames(control.with_suffix('.su').read_text())
        except RuntimeError:
            report['frame_control_rejected'] = True
        else:
            raise RuntimeError('Real 4 KiB frame control was accepted')
        if not args.null_checks:
            control = out / 'codegen-control.o'
            run(flags + ['-O2', '-fno-ipa-icf', '-DBORROWED_CODEGEN_CONTROL', '-c', source('Arm'), '-o', control], 'codegen-control')
            bodies = []
            for route in ('direct', 'local'):
                binary = out / ('codegen-control-' + route + '.bin')
                run([tool('objcopy'), '--dump-section', f'.text.field_{route}_kib4={binary}', control], 'codegen-control-' + route)
                bodies.append(binary.read_bytes())
            try:
                same_bodies(bodies)
            except RuntimeError:
                report['codegen_control_rejected'] = True
            else:
                raise RuntimeError('Real changed native root control was accepted')
            control = out / 'relocation-control.o'
            run(flags + ['-O2', '-fno-ipa-icf', '-DBORROWED_RELOCATION_CONTROL', '-c', source('Arm'), '-o', control], 'relocation-control')
            relocation_text = run([tool('objdump'), '-r', control], 'relocation-control-records')
            bodies, records = [], []
            for route in ('direct', 'local'):
                symbol = 'field_' + route + '_kib4'
                binary = out / ('relocation-control-' + route + '.bin')
                run([tool('objcopy'), '--dump-section', f'.text.{symbol}={binary}', control], 'relocation-control-' + route)
                bodies.append(binary.read_bytes())
                records.append(relocations(relocation_text, symbol))
            same_bodies(bodies)  # Demonstrate why equal unresolved bytes alone are insufficient.
            try:
                same_relocations(records)
            except RuntimeError:
                report['relocation_control_rejected'] = True
            else:
                raise RuntimeError('Real changed native owner relocation control was accepted')
    if before != inputs():
        raise RuntimeError('Build inputs changed; retain this directory and rerun in a fresh one')
    report['artifacts'] = {p.relative_to(out).as_posix(): {'sha256': sha(p), 'bytes': p.stat().st_size}
                           for p in sorted(out.glob('*')) if p.is_file() and p.name != 'summary.json'}
    report['completed'] = True
    (out / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('Borrowed qualification passed: ' + str(report['checks']) + ' host conditions; ARM execution is zero.', flush=True)


if __name__ == '__main__':
    main()
