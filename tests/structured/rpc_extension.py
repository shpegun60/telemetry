#!/usr/bin/env python3
"""Shared counted host and offline Cortex-M7 gates for optional RPC extensions.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
The runners select their actual production inputs separately from loopback
fixtures, so a client-only image cannot acquire a server dependency unnoticed.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def input_hashes(spec):
    paths = [path for path in (ROOT / 'lib').rglob('*') if path.is_file()]
    for directory in (spec['here'], ROOT / spec['example'].parent):
        paths += [path for path in directory.rglob('*')
                  if path.is_file() and '__pycache__' not in path.parts]
    paths += [Path(__file__).resolve(), ROOT / '.github/workflows/ci.yml',
              ROOT / spec['contract'], ROOT / 'tests/structured/qualification/gates.py',
              ROOT / 'tests/structured/qualification/SymbolControl.cpp']
    return {path.relative_to(ROOT).as_posix(): digest(path) for path in sorted(set(paths))}


def counted(text, prefix, label):
    matches = re.findall(r'^' + re.escape(prefix) + r' checks=(\d+) failures=(\d+)\s*$', text, re.M)
    if len(matches) != 1 or int(matches[0][0]) == 0 or int(matches[0][1]) != 0:
        raise RuntimeError(label + ': missing unique positive condition report or nonzero failures')
    return int(matches[0][0])


def coverage_floor(actual, minimum, label):
    if actual < minimum:
        raise RuntimeError(f'RPC_EXTENSION_COVERAGE_FLOOR: {label}: measured {actual}; minimum {minimum}')


def frames(text, ceiling, required=None, limits=()):
    rows = []
    for line in text.splitlines():
        if not line.strip():
            continue
        parts = line.rsplit('\t', 2)
        if len(parts) != 3 or not parts[1].isdigit():
            raise RuntimeError('Malformed stack-usage row: ' + line)
        name, amount, kind = parts
        bound = next((limit for needle, limit in limits if needle in name), ceiling)
        if kind != 'static' or int(amount) > bound:
            raise RuntimeError(f'Nonstatic or excessive RPC extension frame (limit {bound}): ' + line)
        rows.append(dict(function=name, bytes=int(amount), kind=kind, limit=bound))
    if not rows or (required and not any(required in row['function'] for row in rows)):
        raise RuntimeError('Missing stack rows or required function: ' + str(required))
    return rows


def controls(spec):
    prefix = spec['prefix']
    counted(prefix + ' checks=19 failures=0\n', prefix, 'positive')
    refused = 0
    for text in ('', prefix + ' checks=0 failures=0', prefix + ' checks=19 failures=1',
                 (prefix + ' checks=19 failures=0\n') * 2):
        try:
            counted(text, prefix, 'condition-control')
        except RuntimeError:
            refused += 1
        else:
            raise RuntimeError('Invalid runtime report accepted')
    good = 'RpcExtension.cpp:1:1:operation()\t192\tstatic'
    frames(good, 192, 'operation')
    for text in ('', 'not a stack report', good.replace('192', '193'),
                 good.replace('static', 'dynamic'), good.replace('static', 'dynamic,bounded')):
        try:
            frames(text, 192)
        except RuntimeError:
            refused += 1
        else:
            raise RuntimeError('Invalid frame control accepted')
    coverage = {'positive': [], 'refused': []}
    for label, minimum in spec['minima'].items():
        for actual in (minimum, minimum + 1):
            coverage_floor(actual, minimum, label)
            coverage['positive'].append(dict(label=label, measured=actual))
        try:
            coverage_floor(minimum - 1, minimum, label)
        except RuntimeError as error:
            if not str(error).startswith('RPC_EXTENSION_COVERAGE_FLOOR: ' + label + ':'):
                raise RuntimeError('Coverage control lost its intended diagnostic') from error
            coverage['refused'].append(dict(label=label, measured=minimum - 1))
        else:
            raise RuntimeError('Reduced runtime coverage accepted: ' + label)
    return dict(report_positives=2, report_refused=refused, coverage=coverage)


def run_suite(spec):
    suite_started = time.perf_counter()
    parser = argparse.ArgumentParser(description=spec['description'])
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--arm', action='store_true')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--null-checks', action='store_true')
    parser.add_argument('--objdump')
    parser.add_argument('--size')
    args = parser.parse_args()
    if args.arm and args.sanitize:
        parser.error('ARM qualification compiles and links only; no sanitizer runtime is provided')
    output = args.build_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    compiler = Path(shutil.which(args.cxx) or args.cxx).absolute()
    if not compiler.is_file():
        parser.error('Compiler is not an executable file: ' + str(compiler))
    environment = os.environ.copy()
    environment['PATH'] = str(compiler.parent) + os.pathsep + environment.get('PATH', '')
    environment.setdefault('ASAN_OPTIONS', 'detect_leaks=1:detect_stack_use_after_return=1')
    environment.setdefault('UBSAN_OPTIONS', 'halt_on_error=1')
    report = dict(commands=0, rejections=0, executions=0, conditions=0,
                  execution='compile/link only' if args.arm else 'host runtime',
                  compiler_path=str(compiler), compiler_sha256=digest(compiler),
                  headers=[], runtime={}, controls=controls(spec),
                  coverage_minima=dict(spec['minima']), coverage_checked=False,
                  production_sources=[path.relative_to(ROOT).as_posix() for path in spec['sources'].values()],
                  compiled_layout_boundary=spec.get('abi', 'none; new component does not use layout settings'),
                  success=False)

    def run(command, label, *, diagnostic=None, input_text=None):
        command = list(map(str, command))
        started = time.perf_counter()
        result = subprocess.run(command, cwd=ROOT, env=environment, input=input_text,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                encoding='utf-8', errors='replace', timeout=240)
        (output / (label + '.log')).write_text(
            f'COMMAND {command!r}\nEXIT {result.returncode}\n{result.stdout}', encoding='utf-8')
        report['commands'] += 1
        elapsed = time.perf_counter() - started
        report.setdefault('command_durations_seconds', {})[label] = elapsed
        if diagnostic:
            if result.returncode == 0 or not re.search(diagnostic, result.stdout, re.I):
                raise RuntimeError(label + ': intended diagnostic missing\n' + result.stdout)
            report['rejections'] += 1
        elif result.returncode:
            raise RuntimeError(label + ' failed\n' + result.stdout)
        print(f'{label}: pass ({elapsed:.2f}s)', flush=True)
        return result.stdout

    def tool(name, override=None):
        candidate = Path(override) if override else compiler.with_name(
            'arm-none-eabi-' + name + ('.exe' if os.name == 'nt' else ''))
        if not candidate.is_file():
            raise RuntimeError('Compiler companion is missing: ' + str(candidate))
        report.setdefault('tools', {})[name] = dict(path=str(candidate), sha256=digest(candidate))
        return candidate

    try:
        report['source_head'] = run(['git', 'rev-parse', 'HEAD'], 'source-head').strip()
        report['source_dirty'] = run(
            ['git', 'status', '--porcelain=v1', '--untracked-files=all'], 'source-dirty').splitlines()
        report['input_sha256'] = input_hashes(spec)
        report['input_identity_sha256'] = hashlib.sha256(
            json.dumps(report['input_sha256'], sort_keys=True).encode('utf-8')).hexdigest()
        report['compiler_version'] = run([compiler, '--version'], 'compiler').strip()
        flags = [compiler, '-std=c++20', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                 '-pedantic-errors', '-fdiagnostics-color=never', '-Ilib',
                 '-Ilib/boost_pfr/include', '-Ilib/magic_enum']
        if args.null_checks:
            flags += ['-fno-delete-null-pointer-checks']
        if args.sanitize:
            flags += ['-g', '-fno-omit-frame-pointer', '-fsanitize=address,undefined,float-cast-overflow',
                      '-fsanitize-address-use-after-scope', '-fno-sanitize-recover=all']
        if args.arm:
            flags += ['-mcpu=cortex-m7', '-mthumb', '-mfpu=fpv5-d16', '-mfloat-abi=hard',
                      '-fno-exceptions', '-fno-rtti', '-ffunction-sections', '-fdata-sections',
                      '-D' + spec['arm_define']]
        report['flags'] = list(map(str, flags[1:]))
        for index, header in enumerate(spec['headers']):
            run(flags + ['-x', 'c++', '-fsyntax-only', '-'], 'header-' + str(index),
                input_text='#include <' + header + '>\n')
            report['headers'].append(header)
        for name, diagnostic in spec['negatives'].items():
            run(flags + ['-O2', '-D' + name, '-fsyntax-only', spec['here'] / 'Negative.cpp'],
                name.lower(), diagnostic=diagnostic)

        # Each TU includes and instantiates the public API independently. A
        # normal link retains both consumers and detects non-inline definitions.
        odr_objects = []
        odr_return_type = spec.get('odr_return_type', 'unsigned')
        for suffix in ('a', 'b'):
            source = output / ('odr-' + suffix + '.cpp')
            includes = ''.join('#include <' + header + '>\n' for header in spec['headers'])
            source.write_text(includes + '\n' + odr_return_type + ' rpc_extension_odr_' + suffix + '() noexcept {\n' +
                              spec['odr_body'] + '\n}\n', encoding='utf-8')
            obj = output / ('odr-' + suffix + '.o')
            run(flags + ['-O2', '-c', source, '-o', obj], 'odr-compile-' + suffix)
            odr_objects.append(obj)
        main_source = output / 'odr-main.cpp'
        main_source.write_text(odr_return_type + ' rpc_extension_odr_a() noexcept;\n' +
                               odr_return_type + ' rpc_extension_odr_b() noexcept;\n' +
                               'int main() { return rpc_extension_odr_a() != rpc_extension_odr_b(); }\n',
                               encoding='utf-8')
        odr_image = output / ('odr.elf' if args.arm else 'odr' + ('.exe' if os.name == 'nt' else ''))
        odr_link = ['--specs=nano.specs', '--specs=nosys.specs'] if args.arm else []
        run(flags + ['-O2', *odr_objects, main_source, *spec['sources'].values(),
                     *odr_link, '-o', odr_image], 'odr-link')
        report['odr'] = dict(translation_units=3, linked=True, execution='not executed' if args.arm else 'host runtime')
        if not args.arm:
            run([odr_image], 'odr-execute')
            report['executions'] += 1

        example = ROOT / spec['example']
        if not args.arm:
            totals = []
            for stem, budget in (('Check', 0), ('Check', 32), ('NoHeap', 32)):
                label = stem + '-' + str(budget)
                executable = output / (label + ('.exe' if os.name == 'nt' else ''))
                run(flags + ['-O1' if args.sanitize else '-O2',
                             '-DTELEMETRY_STRUCTURED_LOCAL_BYTES=' + str(budget),
                             spec['here'] / (stem + '.cpp'), *spec['runtime_sources'].values(),
                             '-o', executable], 'build-' + label)
                actual = counted(run([executable], 'execute-' + label), spec['prefix'], label)
                totals.append(actual)
                report['runtime'][label] = actual
                report['conditions'] += actual
                report['executions'] += 1
                coverage_floor(actual, spec['minima'][label], label)
            if len(set(totals)) != 1:
                raise RuntimeError('Storage-policy/no-heap variants executed different condition totals')
            for extra in spec.get('extra_runs', ()):
                label = extra['stem']
                executable = output / (label + ('.exe' if os.name == 'nt' else ''))
                run(flags + ['-O1' if args.sanitize else '-O2',
                             spec['here'] / (label + '.cpp'),
                             *extra.get('sources', spec['runtime_sources']).values(),
                             '-o', executable], 'build-' + label)
                actual = counted(run([executable], 'execute-' + label), extra['prefix'], label)
                report['runtime'][label] = actual
                report['conditions'] += actual
                report['executions'] += 1
                coverage_floor(actual, spec['minima'][label], label)
            executable = output / ('example' + ('.exe' if os.name == 'nt' else ''))
            run(flags + ['-O1' if args.sanitize else '-O2', example,
                         *spec.get('example_sources', spec['runtime_sources']).values(),
                         '-o', executable], 'build-example')
            example_text = run([executable], 'execute-example')
            matches = re.findall(spec['example_pattern'], example_text, re.M)
            if len(matches) != 1 or int(matches[0]) == 0:
                raise RuntimeError('Example did not report its actual executed checks')
            actual = int(matches[0])
            report['runtime']['example'] = actual
            report['conditions'] += actual
            report['executions'] += 1
            coverage_floor(actual, spec['minima']['example'], 'example')
            coverage_floor(report['conditions'], spec['minima']['aggregate'], 'aggregate')
            report['coverage_checked'] = True
        else:
            module_spec = importlib.util.spec_from_file_location(
                'rpc_extension_symbol_gates', ROOT / 'tests/structured/qualification/gates.py')
            gates = importlib.util.module_from_spec(module_spec)
            module_spec.loader.exec_module(gates)
            report['symbol_controls'] = gates.controls()
            nm = tool('nm')
            objdump, size = tool('objdump', args.objdump), tool('size', args.size)
            report['linked_symbol_control'] = gates.linked_control(flags, output, run, nm)
            report['arm'] = {}
            for optimization in ('O2', 'Os', 'Og'):
                objects, stack = [], {}
                for name, source in {'roots': spec['here'] / 'Arm.cpp', **spec['sources']}.items():
                    obj = output / (optimization + '-' + name + '.o')
                    run(flags + ['-' + optimization, '-fstack-usage', '-c', source, '-o', obj],
                        'build-' + optimization + '-' + name)
                    objects.append(obj)
                    usage = obj.with_suffix('.su').read_text(encoding='utf-8')
                    if usage.strip():
                        limit = 320 if name == 'adapter' else spec.get('frame_ceiling', 192)
                        workers = (('RpcMailbox::processImpl(', 256),) if name == 'mailbox' else ()
                        workers += tuple(spec.get('frame_limits', {}).get(name, ()))
                        required = spec.get('required_frames', {}).get(name)
                        stack[name] = frames(usage, limit, required, workers)
                    elif name != 'abi':
                        raise RuntimeError('Missing stack report for ' + name)
                image = output / (optimization + '-' + spec['name'] + '.elf')
                retained = ['-Wl,--undefined=' + symbol for symbol in spec['roots']]
                run(flags + [*objects, '-nostdlib', '-Wl,-e,' + spec['entry'], '-Wl,--gc-sections',
                             *retained, '-Wl,--start-group', '-lc', '-lgcc', '-Wl,--end-group',
                             '-o', image], 'link-' + optimization)
                symbols = run([nm, '-C', image], 'symbols-' + optimization)
                forbidden = gates.forbidden_symbols(symbols)
                if forbidden:
                    raise RuntimeError('Production roots retain allocation/formatting symbols: ' + repr(forbidden))
                for symbol in spec['roots']:
                    if not re.search(r'^\s*[0-9a-fA-F]+\s+[Tt]\s+' + symbol + r'$', symbols, re.M):
                        raise RuntimeError('Required real RPC operation was stripped: ' + symbol)
                for needle in spec.get('required_symbols', ()):
                    if needle not in symbols:
                        raise RuntimeError('Required production method was stripped: ' + needle)
                sections = run([size, '-A', image], 'size-' + optimization)
                run([objdump, '-drC', image], 'assembly-' + optimization)
                report['arm'][optimization] = dict(
                    frames=stack, elf_sha256=digest(image),
                    sections={name: int(amount) for name, amount in
                              re.findall(r'^(\.[^\s]+)\s+(\d+)\s+\d+\s*$', sections, re.M)},
                    execution='not executed', retained_roots=list(spec['roots']),
                    required_symbols=list(spec.get('required_symbols', ())))
                run(flags + ['-' + optimization, example,
                             *spec.get('example_sources', spec['runtime_sources']).values(),
                             '--specs=nano.specs', '--specs=nosys.specs', '-Wl,--gc-sections',
                             '-o', output / (optimization + '-example.elf')],
                    'example-' + optimization)
            run(flags + ['-O2', '-c', spec['here'] / 'Check.cpp', '-o', output / 'Check.o'],
                'compile-host-fixture')
            for extra in spec.get('extra_runs', ()):
                run(flags + ['-O2', '-c', spec['here'] / (extra['stem'] + '.cpp'),
                             '-o', output / (extra['stem'] + '.o')],
                    'compile-host-fixture-' + extra['stem'])
            control = output / 'frame-control.o'
            run(flags + ['-O2', '-D' + spec['frame_define'], '-fstack-usage',
                         '-c', spec['here'] / 'Arm.cpp', '-o', control], 'frame-control-build')
            usage = control.with_suffix('.su').read_text(encoding='utf-8')
            rows = [line.rsplit('\t', 2) for line in usage.splitlines()
                    if spec['frame_function'] in line]
            if (len(rows) != 1 or len(rows[0]) != 3 or not rows[0][1].isdigit() or
                    int(rows[0][1]) < 4096):
                raise RuntimeError('Frame control did not retain its real 4 KiB frame')
            try:
                frames(usage, spec.get('frame_ceiling', 192))
            except RuntimeError:
                report['frame_control_rejected'] = True
            else:
                raise RuntimeError('Real excessive-frame control was accepted')
        report['input_sha256_after'] = input_hashes(spec)
        report['inputs_unchanged'] = report['input_sha256'] == report['input_sha256_after']
        if not report['inputs_unchanged']:
            before, after = report['input_sha256'], report['input_sha256_after']
            changed = [name for name in sorted(before.keys() | after.keys())
                       if before.get(name) != after.get(name)]
            raise RuntimeError('Qualification inputs changed during the run: ' + repr(changed))
        report['success'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        report['duration_seconds'] = time.perf_counter() - suite_started
        if 'input_sha256' in report and 'input_sha256_after' not in report:
            report['input_sha256_after'] = input_hashes(spec)
            report['inputs_unchanged'] = report['input_sha256'] == report['input_sha256_after']
        (output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({key: report[key] for key in
                     ('commands', 'rejections', 'executions', 'conditions', 'execution', 'success')}), flush=True)
