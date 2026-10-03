#!/usr/bin/env python3
"""Strict offline H7S borrowed-result evidence checks. No device access. MIT.

Authors: Ruslan Kovtun (shpegun60), codexAi.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import runpy
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
SCOPE = 'Borrowed H7S bounded DWT/PSP comparisons'
SERIAL, PORT, BOARD = '002A001F3033510135393935', 'COM6', 'NUCLEO-H7S3L8'
NAMES = ('field_own_native_4k', 'field_borrow_native_4k', 'field_own_encoded_4k',
         'field_borrow_encoded_4k', 'service_own_native_4k', 'service_borrow_native_4k',
         'service_own_encoded_4k', 'service_borrow_encoded_4k', 'field_borrow_native_64k',
         'field_own_encoded_64k', 'field_borrow_encoded_64k', 'service_borrow_native_64k',
         'service_own_encoded_64k', 'service_borrow_encoded_64k')
REQUIRED = {'tests/structured/borrowed/h7s/' + name for name in
            ('Fixture.hpp', 'Probe.cpp', 'Benchmark.cpp', 'run.py', 'verify.py')}
REQUIRED |= {'tests/structured/mcu/h7s/StackCall.S', 'tests/h7s_support/build.py',
             'lib/telemetry/abi/StructuredAbi.cpp'}
ARM = {'-std=c++20', '-mcpu=cortex-m7', '-mthumb', '-mfpu=fpv5-d16', '-mfloat-abi=hard',
       '-fno-exceptions', '-fno-rtti', '-ffunction-sections', '-fdata-sections', '-fstack-usage'}
FLASH_SECTIONS = runpy.run_path(str(ROOT / 'tests/h7s_support/build.py'))['check_flash_sections']
SCAFFOLD_REQUIRED = {'Boot/Core/Src/main.c', 'Boot/Core/Src/usart.c',
                     'Boot/Core/Inc/uart_bench.h', 'Boot/Core/Startup/startup_stm32h7s3l8hx.s',
                     'Boot/STM32H7S3L8HX_FLASH.ld', 'Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal.c'}

def require(value, reason):
    if not value:
        raise ValueError(reason)

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def valid_hash(value, length=64):
    return isinstance(value, str) and re.fullmatch('[0-9a-f]{' + str(length) + '}', value) is not None

def integer(value, lower=0, upper=0xffffffff):
    require(type(value) is int and lower <= value <= upper, 'Integer outside bounds')
    return value

def path(value):
    require(isinstance(value, str) and value and '\\' not in value and ':' not in value, 'Invalid relative path')
    result = PurePosixPath(value)
    require(not result.is_absolute() and all(part not in ('.', '..') for part in result.parts) and
            result.as_posix() == value, 'Path is not contained/canonical')
    return result

def manifest(value):
    require(isinstance(value, dict) and value, 'Missing hash manifest')
    for name, digest in value.items():
        path(name); require(valid_hash(digest), 'Invalid source/artifact digest')

def sequence(profile):
    ids = [0 if profile == 0 else i & 1 for i in range(128)]
    if profile == 2:
        state = 0x12345678
        for i in range(127, 0, -1):
            state ^= (state << 13) & 0xffffffff
            state ^= state >> 17
            state ^= (state << 5) & 0xffffffff
            j = state % (i + 1)
            ids[i], ids[j] = ids[j], ids[i]
    return ids

def expected_sum(size, count, profile):
    hashes = []
    for selection in range(2):
        value = 2166136261
        for i in range(size):
            byte = (i * 17 + (i >> 8) + selection * 29 + 3) & 255
            value = ((value ^ byte) * 16777619) & 0xffffffff
        hashes.append(value)
    return sum(hashes[index] for index in sequence(profile)[:count]) & 0xffffffff

EXPECTED = {(i, profile): expected_sum(4096 if i < 8 else 65536, 64 if i < 8 else 8, profile)
            for i in range(14) for profile in range(3)}

def verify_image(image):
    lines = image['uart_lines']
    require(isinstance(lines, list) and all(isinstance(line, str) for line in lines), 'UART lines missing')
    cursor = 0
    def take(tag, length):
        nonlocal cursor
        require(cursor < len(lines), 'Missing UART ' + tag)
        parts = lines[cursor].split(' '); cursor += 1
        require(len(parts) == length + 2 and parts[:2] == ['BR', tag] and all(parts), 'Malformed/ordered UART ' + tag)
        return parts[2:]
    def numbers(tag, length):
        values = take(tag, length)
        require(all(re.fullmatch(r'-?\d+', word) for word in values), 'Nondecimal UART field')
        return list(map(int, values))
    opt = image['optimization']
    require(opt in ('O2', 'Os'), 'Unexpected optimization')
    require(numbers('READY', 10) == [1, 2 if opt == 'O2' else 0, 600000000, 32768,
            16384, 256, 128, 7, 3, 14], 'Wrong clock/cache/profile/stack contract')
    memory = numbers('MEMORY', 13)
    require(0x20000000 <= memory[0] < memory[1] <= 0x20010000 and memory[0] % 32 == 0 and
            memory[1] - memory[0] == 16640, 'PSP bank/geometry changed')
    spans = []
    for i, size in zip((2, 4, 6, 8), (8192, 131072, 65536, 65601)):
        begin, end = memory[i:i + 2]
        require(begin % 32 == 0 and 0x24000000 <= begin < end <= 0x24071c00 and end - begin == size,
                'Cached response/output/scratch bank or extent changed')
        spans.append((begin, end))
    require(all(a[1] <= b[0] or b[1] <= a[0] for i, a in enumerate(spans) for b in spans[i + 1:]),
            'Static application/output/scratch buffers overlap')
    require(memory[10:] == [4097, 8, 8], 'Native owning/borrowed result layout changed')
    require(numbers('CORRECT', 3) == [127, 0, 626688], 'Correctness/full payload coverage failed')
    operations = []
    for i, name in enumerate(NAMES):
        row = take('OP', 8)
        size, calls = (4096, 64) if i < 8 else (65536, 8)
        require(row == [str(i), name, str(size), '7', str(calls),
                       *[str(EXPECTED[i, profile]) for profile in range(3)]], 'Operation/independent checksum changed')
        operations.append(dict(index=i, name=name, bytes=size, iterations=calls))
    stack, timing = [], []
    for pattern in range(2):
        for repeat in range(3):
            row = numbers('S', 7)
            require(row[:4] == [-1, 0, pattern, repeat] and 512 <= row[4] <= 1024 and row[5:] == [0, 0],
                    '512-byte PSP positive control failed')
            stack.append(row)
    for i, name in enumerate(NAMES):
        calls = 64 if i < 8 else 8
        for profile in range(3):
            for repeat in range(7):
                row = numbers('T', 7)
                require(row[:3] == [i, profile, repeat] and 0 < row[3] <= 1000000000 and
                        row[4:] == [calls, EXPECTED[i, profile], calls], 'Timing count/checksum/bound failed')
                timing.append(row)
            for pattern in range(2):
                for repeat in range(3):
                    row = numbers('S', 7)
                    require(row[:4] == [i, profile, pattern, repeat] and 0 < row[4] <= 16384 and
                            row[5:] == [EXPECTED[i, profile], calls], 'Stack guard/count/checksum failed')
                    if 'own_native' in name:
                        require(row[4] >= 4096, 'Owning 4 KiB native return disappeared')
                    else:
                        require(row[4] <= 1024, 'Bounded borrowed/encoded path gained a payload-sized stack object')
                    stack.append(row)
    require(take('DONE', 0) == [] and cursor == len(lines), 'Missing/duplicate/trailing UART rows')
    return dict(checks=127, payload_bytes=626688, operations=operations, timing=timing, stack=stack,
                timing_rows=len(timing), stack_rows=len(stack), memory=memory)

def ram_sections(text):
    banks = ((0x08000000, 0x08010000), (0x24000000, 0x24071c00), (0x24071c00, 0x24072000),
             (0x20000000, 0x20010000), (0, 0x10000), (0x30000000, 0x30008000), (0x38800000, 0x38801000))
    spans = []
    for row in re.finditer(r'^\s*\d+\s+(\S+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)[^\n]*\n([^\n]*)', text, re.M):
        name, size, address, _, flags = row.groups()
        size, address = int(size, 16), int(address, 16)
        if size and 'ALLOC' in flags:
            require(any(begin <= address < address + size <= end for begin, end in banks),
                    'ELF allocated section exceeds an internal memory bank: ' + name)
            spans.append(dict(section=name, begin=address, end=address + size))
    require(spans and any(row['section'] == '.dtcm_ids' and row['end'] - row['begin'] == 17152 for row in spans),
            'Independent PSP/sequence section is missing or changed')
    return spans

def current_inputs():
    files = {p for p in (ROOT / 'lib').rglob('*') if p.is_file()}
    files.update(ROOT / name for name in REQUIRED)
    return {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes().replace(b'\r\n', b'\n')).hexdigest()
            for p in sorted(files)}

def verify_build(receipt, current=True, artifacts=None):
    require(receipt['format_version'] == 1 and receipt['scope'] == SCOPE, 'Wrong receipt identity')
    require(valid_hash(receipt['source_head'], 40) and type(receipt['source_dirty']) is bool and
            isinstance(receipt['source_status'], str) and receipt['source_dirty'] == bool(receipt['source_status'].strip()),
            'Malformed source provenance')
    require(re.search(r'14\.3\.1(?:\s|$)', receipt['compiler']) and valid_hash(receipt['compiler_sha256']), 'Compiler provenance changed')
    manifest(receipt['input_lf_sha256']); manifest(receipt['scaffold_sha256'])
    require(REQUIRED.issubset(receipt['input_lf_sha256']), 'Concrete fixture inputs missing')
    require(SCAFFOLD_REQUIRED.issubset(receipt['scaffold_sha256']), 'Required Cube scaffold inputs missing')
    compiled = [PurePosixPath(name).stem + '.o' for name in receipt['scaffold_sha256']
                if PurePosixPath(name).parent.as_posix() in ('Boot/Core/Src', 'Drivers/STM32H7RSxx_HAL_Driver/Src')
                and name.endswith('.c')]
    require(len(compiled) == len(set(compiled)), 'Duplicate compiled scaffold object stem')
    expected_common = set(compiled) | {'startup.o'}
    if current:
        require(receipt['input_lf_sha256'] == current_inputs(), 'Captured/current source inputs differ')
    require(len(receipt['images']) == 2, 'Two release images required')
    seen = set()
    for image in receipt['images']:
        opt = image['optimization']
        require(opt in ('O2', 'Os') and opt not in seen, 'Wrong/duplicate optimization')
        seen.add(opt)
        integer(image['flash_bytes'], 1, 65536)
        require(image['flash_load_span'] == [0x08000000, 0x08000000 + image['flash_bytes']], 'Flash backup/load span mismatch')
        require(ARM.issubset(image['flags']) and '-' + opt in image['flags'] and
                all(not flag.startswith('-flto') for flag in image['flags']), 'Wrong ABI/optimization or LTO enabled')
        require(set(image['objects_sha256']) == {'Benchmark.o', 'Probe.o', 'StructuredAbi.o', 'StackCall.o'}, 'Fixture objects missing')
        for key in ('objects_sha256', 'common_objects_sha256', 'library_sources'):
            manifest(image[key])
        require(set(image['common_objects_sha256']) == expected_common, 'Compiled Cube object manifest incomplete')
        require(set(image['stack_usage']) == {'Benchmark.su', 'Probe.su', 'StructuredAbi.su'}, 'Exact stack-usage manifest incomplete')
        for name, identity in image['stack_usage'].items():
            path(name)
            require(isinstance(identity, dict) and set(identity) == {'sha256', 'bytes'} and valid_hash(identity['sha256']),
                    'Invalid stack-usage artifact identity')
            integer(identity['bytes'], 1)
        require(image['library_sources'] == {p: h for p, h in receipt['input_lf_sha256'].items() if p.startswith('lib/')},
                'Library source snapshot disagrees')
        require(valid_hash(image['elf_sha256']) and valid_hash(image['binary_sha256']) and
                valid_hash(image['linker_sha256']) and valid_hash(image['sections_sha256']),
                'Image/linker identity missing')
        require(image['ram_spans'] and image['no_owning_native_64k'] is True, 'Memory/codegen preflight absent')
        if artifacts is not None:
            directory = Path(artifacts)
            for key in ('elf', 'binary'):
                name = image[key]; path(name)
                require(sha(directory / name) == image[key + '_sha256'], 'Image artifact differs')
            role = directory / Path(image['elf']).parent
            require((directory / image['binary']).stat().st_size == image['flash_bytes'], 'Actual binary size differs from backed-up extent')
            sections_path = role / 'sections.log'
            require(sha(sections_path) == image['sections_sha256'], 'Section artifact identity differs')
            sections = sections_path.read_text()
            require(FLASH_SECTIONS(sections, image['flash_bytes']) == image['flash_load_span'], 'Authenticated ELF Flash load span differs')
            require(ram_sections(sections) == image['ram_spans'], 'RAM evidence differs')
            compiler = Path(receipt['compiler_path'])
            require(compiler.is_file() and sha(compiler) == receipt['compiler_sha256'], 'Compiler artifact identity differs')
            objdump = compiler.with_name('arm-none-eabi-objdump' + compiler.suffix)
            require(objdump.is_file(), 'Companion objdump is unavailable')
            live = subprocess.run([str(objdump), '-h', str(directory / image['elf'])], stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace', timeout=30)
            require(live.returncode == 0, 'Actual ELF section inspection failed')
            require(FLASH_SECTIONS(live.stdout, image['flash_bytes']) == image['flash_load_span'] and
                    ram_sections(live.stdout) == image['ram_spans'], 'Actual ELF memory spans differ')
            for name, digest in image['objects_sha256'].items():
                require(sha(role / name) == digest, 'Fixture object differs')
            for name, digest in image['common_objects_sha256'].items():
                require(sha(directory / 'common' / name) == digest, 'Cube object differs')
            require(sha(directory / 'common/benchmark.ld') == image['linker_sha256'], 'Linker differs')
            require({p.name for p in role.glob('*.su')} == set(image['stack_usage']), 'Retained stack-usage artifact set differs')
            for name, identity in image['stack_usage'].items():
                artifact = role / name
                require(artifact.stat().st_size == identity['bytes'] and sha(artifact) == identity['sha256'], 'Stack-usage artifact differs')
            require({p.relative_to(directory / 'scaffold').as_posix() for p in (directory / 'scaffold').rglob('*') if p.is_file()}
                    == set(receipt['scaffold_sha256']), 'Exact retained scaffold source manifest differs')
            for name, digest in receipt['input_lf_sha256'].items():
                require(sha(directory / 'inputs' / name) == digest, 'Captured LF source differs')
            for name, digest in receipt['scaffold_sha256'].items():
                require(sha(directory / 'scaffold' / name) == digest, 'Captured scaffold differs')
    return dict(images=2, input_files=len(receipt['input_lf_sha256']), execution=receipt['execution'])

def verify(receipt, current=True, artifacts=None):
    result = verify_build(receipt, current, artifacts)
    require(receipt['execution'] == 'device' and receipt['completed'] is True and receipt['restored_and_verified'] is True,
            'Receipt is not a completed restored measurement')
    require((receipt['board'], receipt['serial'], receipt['port']) == (BOARD, SERIAL, PORT), 'Wrong explicitly selected device')
    require(receipt['backup_bytes'] == receipt['restored_bytes'] == 65536 and
            valid_hash(receipt['backup_sha256']) and receipt['backup_sha256'] == receipt['restored_sha256'], 'Full restoration failed')
    result['configurations'] = {}
    for image in receipt['images']:
        parsed = verify_image(image)
        require(image.get('measurement') == parsed, 'Summary is detached from raw UART')
        result['configurations'][image['optimization']] = {key: parsed[key] for key in ('checks', 'payload_bytes', 'timing_rows', 'stack_rows')}
    if artifacts is not None:
        require(sha(Path(artifacts) / 'before.bin') == receipt['backup_sha256'] and
                sha(Path(artifacts) / 'after.bin') == receipt['restored_sha256'], 'Backup/readback artifact differs')
    return result

def control_image(opt='O2'):
    lines = [f'BR READY 1 {2 if opt == "O2" else 0} 600000000 32768 16384 256 128 7 3 14',
             'BR MEMORY 536870912 536887552 603979776 603987968 603987968 604119040 604119040 604184576 604184576 604250177 4097 8 8',
             'BR CORRECT 127 0 626688']
    for i, name in enumerate(NAMES):
        size, calls = (4096, 64) if i < 8 else (65536, 8)
        lines.append(f'BR OP {i} {name} {size} 7 {calls} ' + ' '.join(str(EXPECTED[i, p]) for p in range(3)))
    for pattern in range(2):
        for repeat in range(3):
            lines.append(f'BR S -1 0 {pattern} {repeat} 512 0 0')
    for i, name in enumerate(NAMES):
        calls = 64 if i < 8 else 8
        for profile in range(3):
            for repeat in range(7):
                lines.append(f'BR T {i} {profile} {repeat} 10000 {calls} {EXPECTED[i, profile]} {calls}')
            for pattern in range(2):
                for repeat in range(3):
                    lines.append(f'BR S {i} {profile} {pattern} {repeat} {4096 if "own_native" in name else 64} {EXPECTED[i, profile]} {calls}')
    lines.append('BR DONE')
    return dict(optimization=opt, uart_lines=lines)

def self_test(images=None):
    bases = images or [control_image('O2'), control_image('Os')]
    for image in bases:
        verify_image(image)
    base = bases[0]
    mutations = []
    def change(label, tag, column, value):
        image = copy.deepcopy(base)
        i = next(i for i, row in enumerate(image['uart_lines']) if row.startswith('BR ' + tag + ' '))
        words = image['uart_lines'][i].split(); words[column] = str(value)
        image['uart_lines'][i] = ' '.join(words)
        mutations.append((label, image))
    for label, tag, column, value in (
        ('clock', 'READY', 4, 1), ('cache', 'READY', 5, 1), ('matrix', 'READY', 11, 13),
        ('PSP bank', 'MEMORY', 2, 0x24000000), ('output bounds', 'MEMORY', 9, 0xffffffff),
        ('borrowed layout', 'MEMORY', 13, 65536), ('correctness count', 'CORRECT', 2, 126),
        ('correctness failure', 'CORRECT', 3, 1), ('payload coverage', 'CORRECT', 4, 626687),
        ('operation name', 'OP', 3, 'other'), ('operation iterations', 'OP', 6, 63),
        ('announced checksum', 'OP', 7, 0), ('timing zero', 'T', 5, 0),
        ('timing wrap bound', 'T', 5, 1000000001), ('timing calls', 'T', 6, 1),
        ('timing checksum', 'T', 7, 0), ('callback count', 'T', 8, 1),
        ('stack positive control', 'S', 6, 511), ('stack guard', 'S', 6, 16385),
        ('stack pattern', 'S', 4, 1), ('stack checksum', 'S', 7, 1)):
        change(label, tag, column, value)
    for label, transform in (
        ('missing timing', lambda rows: rows.pop(next(i for i, row in enumerate(rows) if row.startswith('BR T ')))),
        ('duplicate stack', lambda rows: rows.insert(17, rows[17])),
        ('missing DONE', lambda rows: rows.pop()),
        ('trailing row', lambda rows: rows.append('BR DONE'))):
        image = copy.deepcopy(base); transform(image['uart_lines']); mutations.append((label, image))
    for label, image in mutations:
        try:
            verify_image(image)
        except (ValueError, KeyError, IndexError, TypeError):
            continue
        raise AssertionError('Invalid mutation accepted: ' + label)
    return dict(positive_images=len(bases), rejected_mutations=len(mutations), scope='parser controls; no measurements generated')

def artifact_self_test(receipt, artifacts):
    """Mutate metadata against real retained artifacts; never alter their bytes."""
    verify_build(receipt, current=False, artifacts=artifacts)
    cases = []
    def alter(label, mutation):
        value = copy.deepcopy(receipt); mutation(value); cases.append((label, value))
    def remove_first(mapping):
        mapping.pop(next(iter(mapping)))
    alter('removed common object', lambda r: remove_first(r['images'][0]['common_objects_sha256']))
    alter('extra common object', lambda r: r['images'][0]['common_objects_sha256'].__setitem__('other.o', '0' * 64))
    alter('removed scaffold source', lambda r: remove_first(r['scaffold_sha256']))
    alter('removed compiled scaffold source', lambda r: r['scaffold_sha256'].pop('Boot/Core/Src/main.c'))
    alter('missing stack_usage', lambda r: r['images'][0].pop('stack_usage'))
    alter('removed one su', lambda r: remove_first(r['images'][0]['stack_usage']))
    alter('changed su digest', lambda r: r['images'][0]['stack_usage']['Probe.su'].__setitem__('sha256', '0' * 64))
    alter('changed su length', lambda r: r['images'][0]['stack_usage']['Probe.su'].__setitem__('bytes',
          r['images'][0]['stack_usage']['Probe.su']['bytes'] + 1))
    alter('forged one-byte Flash span', lambda r: r['images'][0].update(flash_bytes=1, flash_load_span=[0x08000000, 0x08000001]))
    alter('wrong binary digest', lambda r: r['images'][0].__setitem__('binary_sha256', '0' * 64))
    alter('wrong ELF digest', lambda r: r['images'][0].__setitem__('elf_sha256', '0' * 64))
    alter('wrong authenticated sections digest', lambda r: r['images'][0].__setitem__('sections_sha256', '0' * 64))
    alter('wrong load span', lambda r: r['images'][0]['flash_load_span'].__setitem__(1, r['images'][0]['flash_load_span'][1] + 1))
    alter('wrong object digest', lambda r: r['images'][0]['objects_sha256'].__setitem__('Probe.o', '0' * 64))
    alter('wrong compiler digest', lambda r: r.__setitem__('compiler_sha256', '0' * 64))
    alter('required source omitted', lambda r: r['input_lf_sha256'].pop('lib/telemetry/abi/StructuredAbi.cpp'))
    for label, value in cases:
        try:
            verify_build(value, current=False, artifacts=artifacts)
        except (ValueError, RuntimeError, KeyError, IndexError, TypeError, OSError):
            continue
        raise AssertionError('Invalid retained artifact mutation accepted: ' + label)
    return dict(positive_artifact_sets=1, rejected_artifact_mutations=len(cases),
                mutation_labels=[label for label, _ in cases], retained_artifact_bytes_unchanged=True)

def json_object(pairs):
    value = dict(pairs); require(len(value) == len(pairs), 'Duplicate JSON keys'); return value

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--receipt', type=Path)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--allow-stale-inputs', action='store_true')
    parser.add_argument('--artifacts', type=Path)
    args = parser.parse_args()
    if not args.receipt:
        if not args.self_test or args.build_only or args.artifacts:
            parser.error('Supply --receipt, or use --self-test alone for parser controls')
        print(json.dumps(self_test(), sort_keys=True)); return
    receipt = json.loads(args.receipt.read_text(encoding='utf-8'), object_pairs_hook=json_object)
    result = (verify_build if args.build_only else verify)(receipt, not args.allow_stale_inputs, args.artifacts)
    if args.self_test:
        result['controls'] = self_test(None if args.build_only else receipt['images'])
        if args.artifacts:
            result['artifact_controls'] = artifact_self_test(receipt, args.artifacts)
    print(json.dumps(result, sort_keys=True))

if __name__ == '__main__':
    main()
