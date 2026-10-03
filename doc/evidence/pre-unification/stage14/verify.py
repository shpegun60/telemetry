#!/usr/bin/env python3
"""Verify retained H7S measurements and restoration; never access a device.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
UART coverage, expected values and descriptor bytes are checked independently
of the runner. Artifact checks optionally reconnect recorded hashes with the
retained build bytes. None of these checks performs a new hardware run.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import statistics
import struct

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
SCOPE = 'H7S execution and DWT/PSP measurements'
SERIAL = '002A001F3033510135393935'
BOARD = 'NUCLEO-H7S3L8'
CONFIGURATIONS = {(family, opt) for family in ('Mixed', 'Scale') for opt in ('O2', 'Os')}
ARM_FLAGS = {'-std=c++20', '-mcpu=cortex-m7', '-mthumb', '-mfpu=fpv5-d16',
             '-mfloat-abi=hard', '-fno-exceptions', '-fno-rtti',
             '-ffunction-sections', '-fdata-sections'}
SOURCE_SUFFIXES = {'.h', '.hpp', '.cpp', '.c', '.pri', '.py', '.S'}
REQUIRED_INPUTS = {
    'tests/structured/mcu/Fixture.hpp', 'tests/structured/mcu/Mixed.cpp',
    'tests/structured/mcu/Scale.cpp', 'tests/structured/mcu/h7s/Benchmark.cpp',
    'tests/structured/mcu/h7s/Probe.cpp', 'tests/structured/mcu/h7s/run.py',
    'tests/structured/mcu/h7s/verify.py', 'tests/structured/qualification/Fixture.hpp',
    'tests/structured/qualification/Provider.cpp', 'tests/structured/qualification/Typed.cpp',
    'tests/structured/qualification/Encoded.cpp', 'tests/structured/traversal/Fixture.hpp',
    'tests/json_stack/StackCall.S', 'tests/field_layout/h7s/run.py',
    'tests/field_layout/run.py', 'tests/run_checks.py',
    'lib/telemetry_structured/model/Adapter.cpp', 'lib/telemetry_structured/abi/StructuredAbi.cpp',
    'lib/resource/structured/detail/Values.cpp', 'lib/telemetry/abi/TelemetryAbi.cpp',
}
MIXED = (
    ('direct_config', 1, 4096), ('local_config', 1, 4096),
    ('global_config', 1, 4096), ('slot_config', 1, 4096),
    ('visit_config', 7, 4096), ('readAs_config', 7, 4096),
    ('encoded_read', 7, 4096), ('encoded_write', 7, 4096),
    ('encoded_command', 7, 4096), ('direct_service', 1, 4096),
    ('local_service', 1, 4096), ('global_service', 1, 4096),
    ('encoded_service', 7, 4096), ('big_read', 1, 128),
    ('big_command', 1, 128), ('big_service', 1, 128),
    ('descriptor_chunk', 7, 512), ('packed_chunk', 7, 512),
    ('values', 1, 128), ('native_big', 7, 128),
)
SCALE = (
    ('direct_index', 7, 4096), ('local_u32', 1, 4096),
    ('global_u32', 1, 4096), ('old_local_u32', 1, 4096),
    ('named_visitor', 7, 4096), ('lambda_visitor', 7, 4096),
    ('readAs_u32', 7, 4096), ('encoded_u32', 7, 4096),
    ('old_scalar_u32', 7, 4096), ('direct_known_u32', 1, 4096),
)


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def valid_hash(value, length=64):
    return isinstance(value, str) and re.fullmatch(r'[0-9a-f]{' + str(length) + '}', value) is not None


def integer(value, reason, lower=0, upper=0xffffffff):
    require(type(value) is int and lower <= value <= upper, reason)
    return value


def safe_path(value):
    require(isinstance(value, str) and value and '\\' not in value, 'Invalid manifest path')
    path = PurePosixPath(value)
    require(path.parts and not path.is_absolute() and ':' not in value and
            all(part not in ('.', '..') for part in path.parts),
            'Manifest path is not relative and contained')
    require(path.as_posix() == value, 'Manifest path is not canonical')
    return path


def hash_manifest(value, reason):
    require(isinstance(value, dict) and value, reason)
    for path, hashed in value.items():
        safe_path(path)
        require(valid_hash(hashed), reason + ': malformed digest')


def inputs():
    """The source manifest excludes docs/data, matching the captured build set."""
    paths = {path for directory in (ROOT / 'lib', HERE.parent, HERE.parents[1] / 'qualification')
             for path in directory.rglob('*') if path.is_file() and path.suffix in SOURCE_SUFFIXES}
    paths.update(ROOT / relative for relative in (
        'tests/structured/traversal/Fixture.hpp', 'tests/json_stack/StackCall.S',
        'tests/field_layout/h7s/run.py', 'tests/field_layout/run.py', 'tests/run_checks.py'))
    return {path.relative_to(ROOT).as_posix(): digest(path.read_bytes().replace(b'\r\n', b'\n'))
            for path in sorted(paths)}


def fnv64(data):
    value = 0xcbf29ce484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001b3) & 0xffffffffffffffff
    return value


def descriptor_shape(data, values_bytes, announced_hash):
    """Decode just the published fixture's wire shape, without C++/JS helpers."""
    require(len(data) == 1775 and data[:4] == b'TDS3', 'Wrong fixture descriptor size/magic')
    major, minor, header_bytes, flags, total, fingerprint = struct.unpack_from('<HHHHIQ', data, 4)
    require((major, minor, header_bytes, flags, total) == (3, 0, 64, 0, len(data)),
            'Wrong descriptor header contract')
    require(valid_hash(announced_hash, 16) and int(announced_hash, 16) == fingerprint,
            'Descriptor announced fingerprint differs from its bytes')
    normalized = data[:16] + bytes(8) + data[24:]
    require(fnv64(normalized) == fingerprint, 'Descriptor fingerprint does not match its bytes')
    counts = struct.unpack_from('<7I', data, 24)
    types_offset, catalogs_offset, endpoints_offset = struct.unpack_from('<3I', data, 52)
    require(counts[1:] == (3, 20, 3, 6, 3, 8), 'Fixture catalog/endpoint counts changed')
    require(64 == types_offset < catalogs_offset < endpoints_offset < total,
            'Invalid descriptor section offsets')
    types, catalogs, endpoints = {}, {}, {3: {}, 4: {}, 5: {}}
    cursor = 64

    while cursor < len(data):
        start = cursor
        require(cursor + 8 <= len(data), 'Truncated descriptor record')
        kind, version, record_flags, length = struct.unpack_from('<BBHI', data, cursor)
        cursor += 8
        end = cursor + length
        require(kind in (1, 2, 3, 4, 5) and version == 1 and record_flags == 0 and end <= len(data),
                'Unsupported or truncated descriptor record')

        def take(count):
            nonlocal cursor
            require(cursor + count <= end, 'Truncated descriptor payload')
            result = data[cursor:cursor + count]
            cursor += count
            return result

        def u32():
            return int.from_bytes(take(4), 'little')

        def name():
            text = take(u32())
            require(text and b'\0' not in text, 'Empty/NUL descriptor name')
            try:
                return text.decode('utf-8', errors='strict')
            except UnicodeDecodeError as error:
                raise ValueError('Invalid UTF-8 descriptor name') from error

        if kind == 1:
            require(start < catalogs_offset, 'Type record outside type section')
            identifier = u32()
            type_kind = take(1)[0]
            require(take(3) == bytes(3), 'Nonzero type reserved bytes')
            wire = u32()
            require(identifier not in types and type_kind in range(5), 'Invalid/duplicate fixture type')
            record = dict(kind=type_kind, wire=wire)
            if type_kind == 1:
                code = take(1)[0]
                require(take(3) == bytes(3) and code in range(1, 12), 'Invalid scalar record')
                require(wire == (1, 1, 1, 2, 2, 4, 4, 8, 8, 4, 8)[code - 1],
                        'Scalar width differs from its code')
            elif type_kind == 2:
                record['related'] = u32()
                enum_count = u32()
                enum_entries = []
                for _ in range(enum_count):
                    enum_entries.append((int.from_bytes(take(wire), 'little'), name()))
                require(enum_entries == [(65535, 'Off'), (2, 'On')], 'Fixture enum dictionary changed')
            elif type_kind == 3:
                record['members'] = [(u32(), name()) for _ in range(u32())]
            elif type_kind == 4:
                record['related'], record['elements'] = u32(), u32()
            else:
                require(wire == 0, 'Void type has bytes')
            types[identifier] = record
        elif kind == 2:
            require(catalogs_offset <= start < endpoints_offset, 'Catalog outside catalog section')
            category = take(1)[0]
            require(category in (1, 2, 3) and take(3) == bytes(3), 'Invalid catalog category/reserved bytes')
            group, ordinal, count = u32(), u32(), u32()
            key = (category, group)
            require(key not in catalogs, 'Duplicate catalog')
            catalogs[key] = (ordinal, count, name())
        else:
            require(start >= endpoints_offset, 'Endpoint outside endpoint section')
            identifier = u32()
            related = [u32()]
            if kind == 3:
                capability = take(1)[0]
                require(capability in (1, 3) and take(3) == bytes(3), 'Invalid field capabilities')
                related.append(capability)
            elif kind == 5:
                related.append(u32())
            require(identifier not in endpoints[kind], 'Duplicate endpoint identity')
            endpoints[kind][identifier] = (related, name())
        require(cursor == end, 'Descriptor payload length mismatch')

    require(set(types) == set(range(counts[0])), 'Missing/nonpositional type IDs')
    for record in types.values():
        if record['kind'] in (2, 4):
            require(record['related'] in types, 'Missing related type')
            related = types[record['related']]['wire']
            expected = related * record['elements'] if record['kind'] == 4 else related
            require(record['wire'] == expected, 'Related type wire size mismatch')
        elif record['kind'] == 3:
            require(all(member in types for member, _ in record['members']), 'Missing member type')
            require(record['wire'] == sum(types[member]['wire'] for member, _ in record['members']),
                    'Struct wire size includes padding or misses a member')
    fields = [('Bool', 1), ('U16', 2), ('Float', 8), ('S64', 8), ('U64', 8),
              ('Mode', 2), ('Array', 4), ('Config', 5), ('Big', 4096), ('Slot', 5)]
    commands = [('Configure', 5), ('Reset', 0), ('Slot', 5)]
    services = [('Echo', (5, 5)), ('Ping', (0, 0)), ('Big', (0, 4096)), ('Slot', (5, 5))]
    for category, shape in ((1, fields), (2, commands), (3, services)):
        size = len(shape)
        for group, count, label in ((0, size, 'first'), (1, 0, 'empty'), (2, size, 'repeat')):
            require(catalogs.get((category, group)) == ((0 if group == 0 else size), count, label),
                    'Fixture catalog routing/name changed')
        family = endpoints[category + 2]
        require(set(family) == {(group << 16) | entry for group in (0, 2) for entry in range(size)},
                'Fixture endpoint positions changed')
        for identifier, (related, label) in family.items():
            entry = identifier & 0xffff
            require(label == shape[entry][0] and all(part in types for part in related[:1 if category < 3 else 2]),
                    'Fixture endpoint name/type changed')
            actual = tuple(types[part]['wire'] for part in related[:1 if category < 3 else 2])
            expected = shape[entry][1] if category == 3 else (shape[entry][1],)
            require(actual == expected, 'Fixture endpoint wire size changed')
            if category == 1:
                require(related[1] == (1 if entry == 6 else 3), 'Fixture write capability changed')
    expected_values = 24 + 2 * sum(1 + width for _, width in fields)
    require(values_bytes == expected_values == 8322, 'Values size differs from the fixture shape')
    return dict(bytes=len(data), values_bytes=values_bytes, fingerprint=announced_hash, sha256=digest(data))


def expected_sum(family, operation, profile, iterations, descriptor):
    # Every measured count is a whole multiple of the 128-ID sequence. The
    # shuffled sequence is a permutation, so its sum must equal sequential.
    require(iterations % 128 == 0, 'Incomplete ID sequence window')
    ids = [0] * 128 if profile == 0 else list(range(128))
    if family == 'Scale':
        values = [17 + (0 if operation in (1, 2, 3, 9) else 3 * identifier) for identifier in ids]
    elif operation in (7, 8, 14):
        values = [1] * 128
    elif operation in (13, 19):
        values = [4097] * 128
    elif operation == 15:
        values = [4345] * 128
    elif operation in (16, 17):
        values = [128 + descriptor[identifier] + descriptor[identifier + 127] for identifier in ids]
    elif operation == 18:
        values = [8322 + ord('T') + 1] * 128
    else:
        values = [17] * 128
    return (sum(values) * (iterations // 128)) & 0xffffffff


def verify_image(image):
    """Return parsed rows only after checking every required raw UART row."""
    family, optimization = image['family'], image['optimization']
    require((family, optimization) in CONFIGURATIONS, 'Unexpected image configuration')
    definitions = MIXED if family == 'Mixed' else SCALE
    require(isinstance(image['uart_lines'], list) and image['uart_lines'], 'Missing raw UART evidence')
    lines = image['uart_lines']
    require(all(isinstance(line, str) and '\n' not in line.rstrip('\r\n') for line in lines),
            'Invalid UART line storage')
    position = 0

    def row(tag, count):
        nonlocal position
        require(position < len(lines), 'Missing MCU ' + tag + ' line')
        parts = lines[position].strip().split()
        position += 1
        require(parts[:2] == ['MCU', tag] and len(parts) == count + 2, 'Unexpected MCU line/order: ' + str(parts))
        return parts[2:]

    def numbers(tag, count):
        values = row(tag, count)
        require(all(re.fullmatch(r'-?[0-9]+', value) is not None for value in values), 'Noninteger MCU ' + tag)
        return [int(value) for value in values]

    ready = numbers('READY', 12)
    expected_ready = [1, 0 if family == 'Mixed' else 1, 2 if optimization == 'O2' else 0,
                      600000000, 32768, 16384, 256, 128, 128, 7, 3, len(definitions)]
    require(ready == expected_ready, 'Device clock/cache/stack/matrix setup changed')
    memory = numbers('MEMORY', 8)
    stack, ids, probe = memory[:3]
    require(0x20000000 <= stack and stack + 16384 + 256 <= 0x20010000 and stack % 32 == 0,
            'Probe stack is not aligned DTCM storage')
    require(0x20000000 <= ids and ids + 512 <= 0x20010000 and ids % 32 == 0 and
            (ids + 512 <= stack or stack + 16640 <= ids), 'ID sequence overlaps stack or leaves DTCM')
    require(0x08000000 <= probe < 0x08010000 and probe & 1, 'Probe code is not internal Flash Thumb code')
    require(memory[3:] == [60, 28, 20, 24, 4096], 'Compiled descriptor/native layout changed')
    correct = numbers('CORRECT', 5)
    expected_correct = [12230, 0, 97, 3, 0] if family == 'Mixed' else [2831, 0, 0, 0, 0]
    require(correct == expected_correct, 'Target correctness counts/failures changed')
    descriptor, description = b'', None
    if family == 'Mixed':
        raw = row('DESCRIPTOR', 3)
        require(raw[:2] == ['1775', '8322'], 'Fixture descriptor/values length changed')
        chunks = bytearray()
        while len(chunks) < 1775:
            offset, text = row('D', 2)
            count = min(64, 1775 - len(chunks))
            require(offset == str(len(chunks)) and re.fullmatch(r'[0-9a-f]{' + str(2 * count) + '}', text),
                    'Missing/out-of-order descriptor bytes')
            chunks += bytes.fromhex(text)
        descriptor = bytes(chunks)
        description = descriptor_shape(descriptor, 8322, raw[2])
        require(97 + 35 * 128 + 4103 + 2 * len(descriptor) == correct[0],
                'Correctness total no longer matches its component conditions')
    else:
        require(22 * 128 + 15 == correct[0], 'Scale correctness condition arithmetic changed')
    operations = []
    for index, (name, profiles, iterations) in enumerate(definitions):
        raw = row('OP', 7)
        sums = [expected_sum(family, index, profile, iterations, descriptor) for profile in range(3)]
        require(raw == list(map(str, [index, name, profiles, iterations, *sums])),
                'Operation declaration/checksum differs from independent fixture values')
        operations.append(dict(index=index, name=name, profiles=profiles, iterations=iterations, sums=sums))

    timing, stack_rows = [], []

    def stack_row(operation, profile, pattern, repetition, checksum):
        raw = numbers('S', 6)
        require(raw[:4] == [operation, profile, pattern, repetition] and raw[5] == checksum,
                'Missing/duplicate stack row or wrong checksum')
        integer(raw[4], 'Stack guard exhausted or empty stack observation', 1, 16384)
        if operation == -1:
            require(512 <= raw[4] <= 1024, '512-byte positive stack control was not observed')
        elif family == 'Mixed' and operation in (13, 14, 15):
            # This is an acceptance ceiling for the entire measured probe
            # chain, not a claim that its measured stack is exactly 1024 B.
            require(raw[4] <= 1024, 'Encoded 4 KiB path acquired a large local stack object')
        elif family == 'Mixed' and operation == 19:
            require(raw[4] >= 4096, 'Owning native Big return was optimized out of the stack comparison')
        stack_rows.append(dict(operation=operation, profile=profile, pattern=pattern,
                               repetition=repetition, used=raw[4], checksum=checksum))

    for pattern in range(2):
        for repetition in range(3):
            stack_row(-1, 0, pattern, repetition, 0)
    for operation in operations:
        index = operation['index']
        for profile in range(3):
            if not operation['profiles'] & (1 << profile):
                continue
            for repetition in range(7):
                raw = numbers('T', 6)
                require(raw[:3] == [index, profile, repetition] and raw[4:] ==
                        [operation['iterations'], operation['sums'][profile]],
                        'Missing/duplicate timing row, wrong calls/checksum')
                integer(raw[3], 'Invalid/wrapped DWT measurement', 1, 0xfffffffe)
                timing.append(dict(operation=index, profile=profile, repetition=repetition,
                                   cycles=raw[3], calls=raw[4], checksum=raw[5]))
            for pattern in range(2):
                for repetition in range(3):
                    stack_row(index, profile, pattern, repetition, operation['sums'][profile])
    row('DONE', 0)
    require(position == len(lines), 'Unexpected data after MCU DONE')
    return dict(ready=ready, memory=memory, correct=correct, operations=operations,
                timing=timing, stack=stack_rows, descriptor=description, done=True)


def verify_artifacts(receipt, directory):
    """Recompute raw object/image/compiler hashes and captured LF source hashes."""
    root = Path(directory).resolve()
    for manifest, subdirectory in ((receipt['input_lf_sha256'], 'inputs'),
                                    (receipt['scaffold_lf_sha256'], 'scaffold')):
        available = {path.relative_to(root / subdirectory).as_posix()
                     for path in (root / subdirectory).rglob('*') if path.is_file()}
        require(set(manifest) == available, 'Captured input manifest does not enumerate its complete snapshot')
        for path, hashed in manifest.items():
            file = root / subdirectory / str(safe_path(path))
            require(file.is_file() and digest(file.read_bytes().replace(b'\r\n', b'\n')) == hashed,
                    'Captured build input changed: ' + path)
    for image in receipt['images']:
        location = root / image['family'] / image['optimization']
        for path, hashed in {**image['objects_sha256'], 'benchmark.elf': image['elf_sha256'],
                             'benchmark.bin': image['binary_sha256']}.items():
            file = location / str(safe_path(path))
            require(file.is_file() and digest(file.read_bytes()) == hashed, 'Build artifact changed: ' + str(file))
        require((location / 'benchmark.bin').stat().st_size == image['flash_bytes'], 'Image length differs from receipt')
        for path, hashed in image['common_objects_sha256'].items():
            file = root / 'common' / str(safe_path(path))
            require(file.is_file() and digest(file.read_bytes()) == hashed, 'Common object changed: ' + str(file))
        for path, identity in image['stack_usage'].items():
            file = location / str(safe_path(path))
            require(file.is_file() and file.stat().st_size == identity['bytes'] and
                    digest(file.read_bytes()) == identity['sha256'], 'Stack-usage artifact changed: ' + str(file))
        linker = root / 'common/benchmark.ld'
        require(linker.is_file() and digest(linker.read_bytes()) == image['linker_sha256'],
                'Generated linker input changed')
    compiler = Path(receipt['compiler_path'])
    require(compiler.is_file() and digest(compiler.read_bytes()) == receipt['compiler_sha256'],
            'Retained compiler path no longer matches the measured build')
    for name, hashed in (('before.bin', receipt['backup_sha256']), ('after.bin', receipt['restored_sha256'])):
        file = root / name
        require(file.is_file() and file.stat().st_size == 65536 and digest(file.read_bytes()) == hashed,
                'Retained full Flash backup/readback changed: ' + name)


def verify(receipt, *, current=True, artifacts=None):
    require(type(receipt['format_version']) is int and receipt['format_version'] == 1 and receipt['scope'] == SCOPE,
            'Wrong hardware receipt format/scope')
    require(receipt['completed'] is True and receipt['restored_and_verified'] is True,
            'Hardware run did not finish and restore')
    require(receipt['execution'] == 'device', 'Receipt describes compilation without a hardware execution')
    require(receipt['board'] == BOARD and receipt['serial'] == SERIAL and receipt['port'] == 'COM6',
            'Wrong qualification device/port')
    require(receipt['backup_bytes'] == receipt['restored_bytes'] == 65536 and
            valid_hash(receipt['backup_sha256']) and receipt['backup_sha256'] == receipt['restored_sha256'],
            'Full internal Flash backup/restoration mismatch')
    require(valid_hash(receipt['source_head'], 40) and type(receipt['source_dirty']) is bool,
            'Missing captured source HEAD/dirty state')
    require(isinstance(receipt['compiler'], str) and re.search(r'14\.3\.1(?:\s|$)', receipt['compiler']) and
            valid_hash(receipt['compiler_sha256']), 'Wrong/missing CubeIDE compiler identity')
    hash_manifest(receipt['input_lf_sha256'], 'Missing source manifest')
    hash_manifest(receipt['scaffold_lf_sha256'], 'Missing Cube scaffold manifest')
    require(REQUIRED_INPUTS.issubset(receipt['input_lf_sha256']), 'Source manifest omits a required build input')
    if current:
        require(receipt['input_lf_sha256'] == inputs(), 'Receipt source inputs differ from the current tree')
    require(any(path.startswith('Boot/Core/Src/') for path in receipt['scaffold_lf_sha256']) and
            any(path.startswith('Drivers/') for path in receipt['scaffold_lf_sha256']) and
            any(path.endswith('_FLASH.ld') for path in receipt['scaffold_lf_sha256']),
            'Scaffold manifest omits startup, HAL or linker inputs')
    require({'Boot/Core/Src/main.c', 'Boot/Core/Inc/uart_bench.h',
             'Boot/Core/Startup/startup_stm32h7s3l8hx.s', 'Boot/STM32H7S3L8HX_FLASH.ld',
             'Drivers/STM32H7RSxx_HAL_Driver/Src/stm32h7rsxx_hal.c'}.issubset(receipt['scaffold_lf_sha256']),
            'Scaffold manifest omits an actual common build source/header')
    require(isinstance(receipt['images'], list) and len(receipt['images']) == 4, 'Missing hardware image matrix')
    observed, reports = set(), {}
    for image in receipt['images']:
        key = image['family'], image['optimization']
        require(key in CONFIGURATIONS and key not in observed, 'Wrong/duplicate hardware image')
        observed.add(key)
        integer(image['flash_bytes'], 'Oversized/empty internal Flash image', 1, 65536)
        require(valid_hash(image['elf_sha256']) and valid_hash(image['binary_sha256']), 'Missing ELF/binary identity')
        hash_manifest(image['objects_sha256'], 'Missing object identities')
        expected_objects = {'Benchmark.o', 'Probe.o', image['family'] + '.o', 'Adapter.o',
                            'StructuredAbi.o', 'Values.o', 'TelemetryAbi.o', 'StackCall.o'}
        if image['family'] == 'Mixed':
            expected_objects.update(('Provider.o', 'Typed.o', 'Encoded.o'))
        require(set(image['objects_sha256']) == expected_objects, 'Fixture object manifest is incomplete')
        require(isinstance(image['stack_usage'], dict) and
                set(image['stack_usage']) == {name[:-2] + '.su' for name in expected_objects if name != 'StackCall.o'},
                'Stack-usage manifest is incomplete')
        for path, identity in image['stack_usage'].items():
            safe_path(path)
            require(isinstance(identity, dict) and set(identity) == {'sha256', 'bytes'} and
                    valid_hash(identity['sha256']), 'Malformed stack-usage identity')
            integer(identity['bytes'], 'Malformed stack-usage artifact size')
        hash_manifest(image['common_objects_sha256'], 'Missing Cube object identities')
        expected_common = {PurePosixPath(path).stem + '.o' for path in receipt['scaffold_lf_sha256']
                           if path.endswith('.c') and path.startswith(
                               ('Boot/Core/Src/', 'Drivers/STM32H7RSxx_HAL_Driver/Src/'))} | {'startup.o'}
        require(set(image['common_objects_sha256']) == expected_common, 'Cube object manifest is incomplete')
        require(valid_hash(image['linker_sha256']), 'Missing generated linker input identity')
        hash_manifest(image['library_sources'], 'Missing compiled library provenance')
        require(image['library_sources'] == {path: hashed for path, hashed in receipt['input_lf_sha256'].items()
                                            if path.startswith('lib/')},
                'Library snapshot disagrees with captured inputs')
        require(isinstance(image['flags'], list) and all(isinstance(flag, str) for flag in image['flags']) and
                ARM_FLAGS.issubset(image['flags']) and '-' + image['optimization'] in image['flags'],
                'Missing Cortex-M7 ABI/optimization flags')
        require(('-DMCU_SCALE' in image['flags']) == (image['family'] == 'Scale'), 'Wrong fixture compile mode')
        require({flag for flag in image['flags'] if re.fullmatch(r'-O\w+', flag)} == {'-' + image['optimization']} and
                '-DQUALIFICATION_ARM' in image['flags'] and
                '-DLAYOUT_OPT=' + ('2' if image['optimization'] == 'O2' else '0') in image['flags'],
                'Wrong optimization/setup compile defines')
        require(isinstance(image['link_flags'], list) and all(isinstance(flag, str) for flag in image['link_flags']) and
                {'-mcpu=cortex-m7', '-mthumb', '-mfpu=fpv5-d16', '-mfloat-abi=hard',
                 '--specs=nano.specs', '--specs=nosys.specs', '-Wl,--gc-sections'}.issubset(image['link_flags']) and
                sum(flag.startswith('-T') for flag in image['link_flags']) == 1,
                'Missing linker/ABI flags')
        parsed = verify_image(image)
        if 'measurement' in image:
            require(image['measurement'] == parsed, 'Parsed summary disagrees with its raw UART evidence')
        reports['-'.join(key)] = dict(timing=len(parsed['timing']), stack=len(parsed['stack']),
                                    checks=parsed['correct'][0], extra_checks=parsed['correct'][3])
    require(observed == CONFIGURATIONS, 'Incomplete hardware configuration coverage')
    if artifacts is not None:
        verify_artifacts(receipt, artifacts)
    return dict(images=len(observed), input_files=len(receipt['input_lf_sha256']), configurations=reports)


def self_test(receipt):
    """Reject mutations independently of the runner and without device access."""
    mutations = []

    def alter(label, mutation):
        changed = copy.deepcopy(receipt)
        mutation(changed)
        mutations.append((label, changed))

    for key, value in (('format_version', 2), ('scope', 'offline'), ('completed', False),
                       ('restored_and_verified', False), ('board', 'wrong'), ('serial', 'wrong'),
                       ('port', 'COM5'), ('backup_bytes', 32768), ('restored_bytes', 32768),
                       ('restored_sha256', '0' * 64), ('source_head', 'invalid'),
                       ('source_dirty', 1), ('compiler', 'other compiler'), ('compiler_sha256', 'invalid')):
        alter(key, lambda value_receipt, key=key, value=value: value_receipt.__setitem__(key, value))
    alter('missing images', lambda r: r['images'].pop())
    alter('duplicate images', lambda r: r['images'].__setitem__(1, copy.deepcopy(r['images'][0])))
    for key, value in (('elf_sha256', 'invalid'), ('binary_sha256', 'invalid'), ('flash_bytes', 65537),
                       ('objects_sha256', {}), ('common_objects_sha256', {}), ('linker_sha256', 'invalid'),
                       ('library_sources', {}), ('flags', []), ('link_flags', [])):
        alter(key, lambda r, key=key, value=value: r['images'][0].__setitem__(key, value))
    for key in ('input_lf_sha256', 'scaffold_lf_sha256'):
        alter('empty ' + key, lambda r, key=key: r.__setitem__(key, {}))
        alter('path escape ' + key, lambda r, key=key: r[key].__setitem__('../outside.cpp', '0' * 64))
    alter('missing required input while others remain',
          lambda r: r['input_lf_sha256'].pop('tests/structured/mcu/h7s/verify.py'))
    alter('missing library input while others remain',
          lambda r: r['input_lf_sha256'].pop('lib/telemetry_structured/model/Adapter.cpp'))
    alter('missing library snapshot member', lambda r: r['images'][0]['library_sources'].pop(
          'lib/telemetry_structured/model/Adapter.cpp'))
    alter('missing startup source while others remain', lambda r: r['scaffold_lf_sha256'].pop(
          'Boot/Core/Startup/startup_stm32h7s3l8hx.s'))
    alter('missing common object while others remain', lambda r: r['images'][0]['common_objects_sha256'].pop('startup.o'))
    alter('missing one stack-usage artifact', lambda r: r['images'][0]['stack_usage'].pop('Probe.su'))
    alter('compile-only role', lambda r: r.update(execution='compile/link only'))

    def uart_mutation(label, tag, mutation):
        def change(r):
            image = next(image for image in r['images'] if image['family'] == 'Mixed')
            index = next(i for i, line in enumerate(image['uart_lines']) if line.startswith('MCU ' + tag + ' '))
            parts = image['uart_lines'][index].strip().split()
            mutation(image['uart_lines'], index, parts)
        alter(label, change)

    def replace_word(index, value):
        def mutate(lines, row_index, parts):
            parts[index] = str(value)
            lines[row_index] = ' '.join(parts)
        return mutate

    uart_mutation('clock', 'READY', replace_word(5, 1))
    uart_mutation('cache', 'READY', replace_word(6, 1))
    uart_mutation('operation count', 'READY', replace_word(13, 19))
    uart_mutation('memory bank', 'MEMORY', replace_word(2, 0x24000000))
    uart_mutation('native layout', 'MEMORY', replace_word(9, 1))
    uart_mutation('correctness count', 'CORRECT', replace_word(2, 12229))
    uart_mutation('correctness failure', 'CORRECT', replace_word(3, 1))
    uart_mutation('consumer count', 'CORRECT', replace_word(4, 96))
    uart_mutation('extra count', 'CORRECT', replace_word(5, 2))
    uart_mutation('extra failure', 'CORRECT', replace_word(6, 1))
    uart_mutation('descriptor length', 'DESCRIPTOR', replace_word(2, 1774))
    uart_mutation('values length', 'DESCRIPTOR', replace_word(3, 8321))
    uart_mutation('descriptor fingerprint', 'DESCRIPTOR', replace_word(4, '0' * 16))
    uart_mutation('descriptor cursor', 'D', replace_word(2, 1))
    uart_mutation('descriptor byte', 'D', replace_word(3, '00' * 64))
    uart_mutation('operation name', 'OP', replace_word(3, 'unknown'))
    uart_mutation('operation mask', 'OP', replace_word(4, 7))
    uart_mutation('operation iterations', 'OP', replace_word(5, 128))
    uart_mutation('announced checksum', 'OP', replace_word(6, 0))
    uart_mutation('timing cycles', 'T', replace_word(5, 0))
    uart_mutation('timing calls', 'T', replace_word(6, 1))
    uart_mutation('timing checksum', 'T', replace_word(7, 0))
    uart_mutation('timing repeat', 'T', replace_word(4, 1))
    uart_mutation('stack positive control', 'S', replace_word(6, 511))
    uart_mutation('stack guard', 'S', replace_word(6, 16385))
    uart_mutation('stack pattern', 'S', replace_word(4, 1))
    uart_mutation('stack checksum', 'S', replace_word(7, 1))
    uart_mutation('missing timing', 'T', lambda lines, index, _: lines.pop(index))
    uart_mutation('duplicate stack', 'S', lambda lines, index, _: lines.insert(index, lines[index]))
    def selected_stack(label, operation, used):
        def change(r):
            image = next(image for image in r['images'] if image['family'] == 'Mixed')
            index = next(i for i, line in enumerate(image['uart_lines']) if line.startswith(f'MCU S {operation} '))
            parts = image['uart_lines'][index].split()
            parts[6] = str(used)
            image['uart_lines'][index] = ' '.join(parts)
        alter(label, change)
    selected_stack('encoded Big acquired a large stack object', 13, 1025)
    selected_stack('owning Big stack was optimized away', 19, 4095)
    alter('missing DONE', lambda r: r['images'][0]['uart_lines'].pop())
    alter('extra DONE', lambda r: r['images'][0]['uart_lines'].append('MCU DONE'))
    if 'measurement' in receipt['images'][0]:
        alter('summary detached from raw', lambda r: r['images'][0]['measurement'].update(done=False))
    for label, changed in mutations:
        try:
            verify(changed, current=False)
        except (ValueError, KeyError, IndexError, TypeError, struct.error):
            continue
        raise AssertionError('Invalid receipt mutation accepted: ' + label)
    return dict(rejected_mutations=len(mutations))


def json_object(pairs):
    result = dict(pairs)
    require(len(result) == len(pairs), 'Duplicate JSON keys')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--receipt', type=Path, default=HERE / 'receipt.json')
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--allow-stale-inputs', action='store_true',
                        help='Check recorded coverage only, without claiming current-tree evidence')
    parser.add_argument('--artifacts', type=Path, help='Recompute retained build/backup/readback hashes')
    args = parser.parse_args()
    receipt = json.loads(args.receipt.read_text(encoding='utf-8'), object_pairs_hook=json_object)
    result = verify(receipt, current=not args.allow_stale_inputs, artifacts=args.artifacts)
    if args.self_test:
        result['controls'] = self_test(receipt)
    print(json.dumps(result, sort_keys=True))
    for image in receipt['images']:
        parsed = verify_image(image)
        for operation in parsed['operations']:
            rows = [row for row in parsed['timing'] if row['operation'] == operation['index']]
            stack_rows = [row for row in parsed['stack'] if row['operation'] == operation['index']]
            medians = {profile: statistics.median(row['cycles'] / row['calls'] for row in rows
                       if row['profile'] == profile) for profile in sorted({row['profile'] for row in rows})}
            print(f'{image["family"]}/{image["optimization"]} {operation["name"]}: '
                  f'cycles/call {medians}, full probe chain stack {max(row["used"] for row in stack_rows)} B')


if __name__ == '__main__':
    main()
