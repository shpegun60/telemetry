#!/usr/bin/env python3
"""RpcMailbox transfer, dispatch, ownership and offline Cortex-M7 checks.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
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

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SOURCES = {
    'mailbox': ROOT / 'lib/integration/telemetry/v3/RpcMailbox.cpp',
    'protocol': ROOT / 'lib/resource/protocol/Protocol.cpp',
    'adapter': ROOT / 'lib/telemetry/model/Adapter.cpp',
    'abi': ROOT / 'lib/telemetry/abi/StructuredAbi.cpp',
}
HEADERS = ('wire/RpcWire.hpp', 'integration/telemetry/v3/RpcMailbox.hpp')
NEGATIVES = {
    'RPC_NEG_COPY': r'(?:deleted[^\n]*RpcMailbox|RpcMailbox[^\n]*deleted)',
    'RPC_NEG_MOVE': r'(?:deleted[^\n]*RpcMailbox|RpcMailbox[^\n]*deleted)',
    'RPC_NEG_MODEL_TEMPORARY': r'(?:deleted|no matching|no viable|cannot bind)[\s\S]*RpcMailbox|RpcMailbox[\s\S]*(?:deleted|no matching|no viable|cannot bind)',
    'RPC_NEG_WORKSPACE_TEMPORARY': r'(?:deleted|no matching|no viable|cannot bind)[\s\S]*(?:RpcMailbox|Workspace)|(?:RpcMailbox|Workspace)[\s\S]*(?:deleted|no matching|no viable|cannot bind)',
    'CASE=5': r'(?:no matching|no viable)[\s\S]*RpcMailbox',
    'CASE=6': r'(?:no matching|no viable)[\s\S]*RpcMailbox',
    'CASE=7': r'(?:deleted[^\n]*RpcMailbox|RpcMailbox[^\n]*deleted)',
    'CASE=8': r'(?:deleted[^\n]*RpcMailbox|RpcMailbox[^\n]*deleted)',
}
ROOT_SYMBOLS = ('rpc_write', 'rpc_read', 'rpc_process', 'rpc_reset', 'rpc_size', 'rpc_roots')
COVERAGE_MINIMA = {'Check-0': 2577, 'Check-32': 2577, 'NoHeap-32': 2577,
                   'example': 34, 'aggregate': 7765}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def input_hashes():
    """Capture exact inputs, including pinned vendor bytes and opt-in manifest."""
    paths = [path for path in (ROOT / 'lib').rglob('*') if path.is_file()]
    for directory in (HERE, ROOT / 'examples/rpc_mailbox'):
        paths += [path for path in directory.rglob('*')
                  if path.is_file() and '__pycache__' not in path.parts]
    paths += [ROOT / '.github/workflows/ci.yml', ROOT / 'doc/RpcMailboxV1.md',
              ROOT / 'tests/structured/qualification/gates.py',
              ROOT / 'tests/structured/qualification/SymbolControl.cpp']
    return {path.relative_to(ROOT).as_posix(): digest(path) for path in sorted(set(paths))}


def counted(text, label):
    """Read actual executed conditions; do not substitute a recorded suite total."""
    matches = re.findall(r'^rpc checks=(\d+) failures=(\d+)\s*$', text, re.M)
    if len(matches) != 1 or int(matches[0][0]) == 0 or int(matches[0][1]) != 0:
        raise RuntimeError(label + ': missing unique positive condition report or nonzero failures')
    return int(matches[0][0])


def coverage_floor(actual, minimum, label):
    """Gate the measured count without replacing it with an expected count."""
    if actual < minimum:
        raise RuntimeError(f'RPC_COVERAGE_FLOOR: {label}: measured {actual} checks; minimum {minimum}')


def coverage_controls():
    positives, rejections = [], []
    for label, minimum in COVERAGE_MINIMA.items():
        for actual in (minimum, minimum + 1):
            coverage_floor(actual, minimum, label)
            positives.append({'label': label, 'measured': actual})
        try:
            coverage_floor(minimum - 1, minimum, label)
        except RuntimeError as error:
            if not str(error).startswith('RPC_COVERAGE_FLOOR: ' + label + ':'):
                raise RuntimeError('Coverage control lost its named diagnostic') from error
            rejections.append({'label': label, 'measured': minimum - 1})
        else:
            raise RuntimeError('Reduced runtime coverage accepted: ' + label)
    return {'positive': len(positives), 'refused': len(rejections),
            'accepted_counts': positives, 'rejected_counts': rejections}


def frames(text, ceiling, required=None, limits=()):
    result = []
    for line in text.splitlines():
        if not line.strip():
            continue
        parts = line.rsplit('\t', 2)
        if len(parts) != 3 or not parts[1].isdigit():
            raise RuntimeError('Malformed stack-usage row: ' + line)
        name, amount, kind = parts
        limit = next((bound for needle, bound in limits if needle in name), ceiling)
        if kind != 'static' or int(amount) > limit:
            raise RuntimeError(f'Nonstatic or excessive RPC frame (limit {limit}): ' + line)
        result.append({'function': name, 'bytes': int(amount), 'kind': kind, 'limit': limit})
    if not result or (required and not any(required in row['function'] for row in result)):
        raise RuntimeError('Missing stack-usage rows or required function: ' + str(required))
    return result


def controls():
    counted('rpc checks=19 failures=0\n', 'positive')
    refused = 0
    for text in ('', 'rpc checks=0 failures=0', 'rpc checks=19 failures=1',
                 'rpc checks=19 failures=0\nrpc checks=19 failures=0'):
        try:
            counted(text, 'condition-control')
        except RuntimeError:
            refused += 1
        else:
            raise RuntimeError('Invalid runtime report accepted')
    good = 'RpcMailbox.cpp:1:1:RpcMailbox::process()\t192\tstatic'
    frames(good, 192, 'RpcMailbox::process')
    for text in ('', 'not a stack report', good.replace('192', '193'),
                 good.replace('static', 'dynamic'), good.replace('static', 'dynamic,bounded')):
        try:
            frames(text, 192)
        except RuntimeError:
            refused += 1
        else:
            raise RuntimeError('Invalid stack report accepted')
    return {'positive': 2, 'refused': refused}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
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
    report = {'commands': 0, 'rejections': 0, 'executions': 0, 'conditions': 0,
              'execution': 'compile/link only' if args.arm else 'host',
              'compiler_path': str(compiler), 'compiler_sha256': digest(compiler),
              'headers': [], 'runtime': {}, 'controls': controls(),
              'coverage_minima': dict(COVERAGE_MINIMA),
              'coverage_controls': coverage_controls(), 'coverage_checked': False,
              'success': False}

    def run(command, label, *, diagnostic=None, input_text=None):
        command = list(map(str, command))
        started = time.perf_counter()
        result = subprocess.run(command, cwd=ROOT, env=environment, input=input_text,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                encoding='utf-8', errors='replace', timeout=180)
        (output / (label + '.log')).write_text(
            f'COMMAND {command!r}\nEXIT {result.returncode}\n{result.stdout}', encoding='utf-8')
        report['commands'] += 1
        if diagnostic:
            if result.returncode == 0 or not re.search(diagnostic, result.stdout, re.I):
                raise RuntimeError(label + ': intended diagnostic missing\n' + result.stdout)
            report['rejections'] += 1
        elif result.returncode:
            raise RuntimeError(label + ' failed\n' + result.stdout)
        print(f'{label}: pass ({time.perf_counter() - started:.2f}s)', flush=True)
        return result.stdout

    def tool(name, override=None):
        if override:
            return override
        suffix = '.exe' if os.name == 'nt' else ''
        candidate = compiler.with_name('arm-none-eabi-' + name + suffix)
        if not candidate.is_file():
            raise RuntimeError('Compiler companion is missing: ' + str(candidate))
        return candidate

    try:
        report['source_head'] = run(['git', 'rev-parse', 'HEAD'], 'source-head').strip()
        report['source_dirty'] = run(
            ['git', 'status', '--porcelain=v1', '--untracked-files=all'], 'source-dirty').splitlines()
        report['input_sha256'] = input_hashes()
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
                      '-DRPC_ARM_PROBE']
        report['flags'] = list(map(str, flags[1:]))
        for index, header in enumerate(HEADERS):
            run(flags + ['-x', 'c++', '-fsyntax-only', '-'], 'header-' + str(index),
                input_text='#include <' + header + '>\n')
            report['headers'].append(header)
        for name, diagnostic in NEGATIVES.items():
            run(flags + ['-O2', '-D' + name, '-fsyntax-only', HERE / 'Negative.cpp'],
                name.lower(), diagnostic=diagnostic)

        example = ROOT / 'examples/rpc_mailbox/main.cpp'
        if not args.arm:
            totals = []
            for stem, budget in (('Check', 0), ('Check', 32), ('NoHeap', 32)):
                label = stem + '-' + str(budget)
                executable = output / (label + ('.exe' if os.name == 'nt' else ''))
                run(flags + ['-O1' if args.sanitize else '-O2',
                             '-DTELEMETRY_STRUCTURED_LOCAL_BYTES=' + str(budget),
                             HERE / (stem + '.cpp'), *SOURCES.values(), '-o', executable],
                    'build-' + label)
                text = run([executable], 'execute-' + label)
                actual = counted(text, label)
                totals.append(actual)
                report['runtime'][label] = actual
                report['conditions'] += actual
                report['executions'] += 1
                coverage_floor(actual, COVERAGE_MINIMA[label], label)
            if len(set(totals)) != 1:
                raise RuntimeError('Storage-policy/no-heap variants executed different condition totals')
            executable = output / ('RpcMailboxExample' + ('.exe' if os.name == 'nt' else ''))
            run(flags + ['-O1' if args.sanitize else '-O2', example, *SOURCES.values(),
                         '-o', executable], 'build-example')
            example_output = run([executable], 'execute-example')
            example_counts = re.findall(r'^RPC mailbox example: (\d+) checks,', example_output, re.M)
            if len(example_counts) != 1 or int(example_counts[0]) == 0:
                raise RuntimeError('Integration example did not report its executed checks')
            report['runtime']['example'] = int(example_counts[0])
            report['conditions'] += int(example_counts[0])
            report['executions'] += 1
            coverage_floor(report['runtime']['example'], COVERAGE_MINIMA['example'], 'example')
            coverage_floor(report['conditions'], COVERAGE_MINIMA['aggregate'], 'aggregate')
            report['coverage_checked'] = True
        else:
            spec = importlib.util.spec_from_file_location(
                'rpc_qualification_gates', ROOT / 'tests/structured/qualification/gates.py')
            gates = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(gates)
            report['symbol_controls'] = gates.controls()
            nm = tool('nm')
            objdump, size = tool('objdump', args.objdump), tool('size', args.size)
            report['linked_symbol_control'] = gates.linked_control(flags, output, run, nm)
            report['arm'] = {}
            for optimization in ('O2', 'Os', 'Og'):
                objects, stack = [], {}
                for name, source in {'roots': HERE / 'Arm.cpp', **SOURCES}.items():
                    obj = output / (optimization + '-' + name + '.o')
                    extra = ['-DABI_MAIN'] if name == 'roots' else []
                    run(flags + ['-' + optimization, '-fstack-usage', *extra,
                                 '-c', source, '-o', obj], 'build-' + optimization + '-' + name)
                    objects.append(obj)
                    usage = obj.with_suffix('.su').read_text(encoding='utf-8')
                    if usage.strip():
                        required = 'RpcMailbox::process' if name == 'mailbox' else None
                        # ModelView crosses this existing compiled boundary by
                        # value; isolated Entry/thunk/provider frames use 192 B.
                        worker = (('RpcMailbox::processImpl(', 256),) if name == 'mailbox' else ()
                        stack[name] = frames(usage, 320 if name == 'adapter' else 192, required, worker)
                    elif name != 'abi':
                        raise RuntimeError('Missing stack report for ' + name)
                image = output / (optimization + '-rpc.elf')
                retained = ['-Wl,--undefined=' + symbol for symbol in ROOT_SYMBOLS]
                run(flags + [*objects, '-nostdlib', '-Wl,-e,rpc_roots', '-Wl,--gc-sections',
                             *retained, '-Wl,--start-group', '-lc', '-lgcc', '-Wl,--end-group',
                             '-o', image], 'link-' + optimization)
                symbols = run([nm, '-C', image], 'symbols-' + optimization)
                forbidden = gates.forbidden_symbols(symbols)
                if forbidden:
                    raise RuntimeError('RPC roots retained allocation/formatting symbols: ' + repr(forbidden))
                for symbol in ROOT_SYMBOLS:
                    if not re.search(r'^\s*[0-9a-fA-F]+\s+[Tt]\s+' + symbol + r'$', symbols, re.M):
                        raise RuntimeError('Required RPC operation was stripped: ' + symbol)
                sections = run([size, '-A', image], 'size-' + optimization)
                run([objdump, '-drC', image], 'assembly-' + optimization)
                report['arm'][optimization] = {
                    'frames': stack, 'elf_sha256': digest(image),
                    'sections': {name: int(amount) for name, amount in
                                 re.findall(r'^(\.[^\s]+)\s+(\d+)\s+\d+\s*$', sections, re.M)},
                    'execution': 'not executed', 'retained_roots': list(ROOT_SYMBOLS)}
                # The runnable integration example is a separate linked
                # consumer, not part of the production no-heap root gate.
                run(flags + ['-' + optimization, example, *SOURCES.values(),
                             '--specs=nano.specs', '--specs=nosys.specs',
                             '-Wl,--gc-sections', '-o', output / (optimization + '-example.elf')],
                    'example-' + optimization)
            run(flags + ['-O2', '-c', HERE / 'Check.cpp', '-o', output / 'Check.o'], 'compile-host-fixture')
            control = output / 'frame-control.o'
            run(flags + ['-O2', '-DRPC_FRAME_CONTROL', '-fstack-usage',
                         '-c', HERE / 'Arm.cpp', '-o', control], 'frame-control-build')
            control_usage = control.with_suffix('.su').read_text(encoding='utf-8')
            control_rows = [line.rsplit('\t', 2) for line in control_usage.splitlines()
                            if 'rpc_frame_control' in line]
            if (len(control_rows) != 1 or len(control_rows[0]) != 3 or
                    not control_rows[0][1].isdigit() or int(control_rows[0][1]) < 4096):
                raise RuntimeError('Excessive-frame control did not retain its real 4 KiB frame')
            try:
                frames(control_usage, 192)
            except RuntimeError:
                report['frame_control_rejected'] = True
            else:
                raise RuntimeError('Real excessive-frame control was accepted')
        # Keep real read/write/process references under section GC: a caller
        # and mailbox built with different structured layouts cannot link.
        boundary = []
        for name, source in {'caller': HERE / 'Arm.cpp', **SOURCES}.items():
            obj = output / ('boundary-' + name + '.o')
            extra = ['-DABI_MAIN'] if name == 'caller' else []
            run(flags + ['-O2', '-ffunction-sections', '-fdata-sections', *extra,
                         '-c', source, '-o', obj], 'boundary-' + name)
            boundary.append(obj)
        bad_mailbox = output / 'boundary-mailbox-mismatch.o'
        run(flags + ['-O2', '-DTELEMETRY_STRUCTURED_LOCAL_BYTES=0', '-c',
                     SOURCES['mailbox'], '-o', bad_mailbox], 'boundary-mailbox-mismatch')
        linking = (['-nostdlib', '-Wl,-e,rpc_roots', '-Wl,--start-group',
                    '-lc', '-lgcc', '-Wl,--end-group'] if args.arm else [])
        run(flags + [*boundary, '-Wl,--gc-sections', *linking,
                     '-o', output / 'boundary-match.elf'], 'boundary-match')
        mismatch = [bad_mailbox if obj.name == 'boundary-mailbox.o' else obj for obj in boundary]
        diagnostic = run(flags + [*mismatch, '-Wl,--gc-sections', *linking,
                                 '-o', output / 'boundary-mismatch.elf'], 'boundary-mismatch',
                         diagnostic=r'undefined (?:reference|symbol)[\s\S]*RpcMailbox')
        for method in ('readImpl', 'writeImpl', 'processImpl'):
            if not re.search(r'RpcMailbox::' + method, diagnostic):
                raise RuntimeError('Mailbox ABI refusal omitted retained ' + method)
        report['mailbox_abi'] = {'matching_link': True,
                                 'mismatched_methods': ['readImpl', 'writeImpl', 'processImpl']}
        report['input_sha256_after'] = input_hashes()
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
        if 'input_sha256' in report and 'input_sha256_after' not in report:
            report['input_sha256_after'] = input_hashes()
            report['inputs_unchanged'] = report['input_sha256'] == report['input_sha256_after']
        (output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({key: report[key] for key in
                     ('commands', 'rejections', 'executions', 'conditions', 'execution', 'success')}), flush=True)


if __name__ == '__main__':
    main()
