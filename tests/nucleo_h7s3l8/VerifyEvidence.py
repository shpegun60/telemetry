#!/usr/bin/env python3
"""Verify the published, historical H7S receipts without tools or hardware (MIT).

Author: Ruslan Kovtun (shpegun60), codexAi.
Hashes establish bundle integrity and cross-receipt consistency, not attestation.
Original ELF/GUI/flash bytes are deliberately absent; this never recreates them.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import struct
import sys
import zlib


PROVENANCE = 'd8137af6a81557a41c636439171957ef1d5c1ea8'
BASELINE = 'a537988bb53292dca0c5c221f9d9008d5464be82'
COBS = '2e0abf260848fcb74e4a57b37532188047759643'
GUI_DELTA = {'.github/workflows/ci.yml', 'examples/inspector_qml/Main.qml'}
RECEIPTS = ('hardware', 'firmware-build', 'gui-hardware-build',
            'serial-windows-final', 'serial-gcc-final', 'serial-clang-san-final')
SERIAL_RECEIPTS = RECEIPTS[2:]
ALLOWED_SUFFIXES = {'.json', '.log', '.txt', '.png', '.md'}
DEFAULT_BUNDLE = Path(__file__).resolve().parents[2] / 'doc/evidence/inspector-serial-h7s-2026-10-28'


class EvidenceError(ValueError):
    """A missing, changed, unsafe or contradictory evidence item."""


def require(condition, message):
    if not condition:
        raise EvidenceError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def hash_value(value):
    require(isinstance(value, str) and re.fullmatch(r'[0-9a-fA-F]{64}', value),
            'invalid SHA-256 value')
    return value.lower()


def pairs_unique(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'duplicate JSON key: ' + key)
        result[key] = value
    return result


def decode(data, name):
    try:
        value = json.loads(data.decode('utf-8-sig'), object_pairs_hook=pairs_unique,
                           parse_constant=lambda value: (_ for _ in ()).throw(
                               EvidenceError('non-finite JSON constant: ' + value)))
    except (UnicodeError, json.JSONDecodeError) as error:
        raise EvidenceError('invalid JSON: ' + name) from error
    require(isinstance(value, dict), 'JSON root is not an object: ' + name)
    return value


def safe_name(name):
    require(isinstance(name, str) and name and '\\' not in name and ':' not in name
            and '\x00' not in name, 'unsafe bundle path')
    path = PurePosixPath(name)
    require(not path.is_absolute() and name == path.as_posix()
            and all(part not in ('.', '..') for part in path.parts), 'unsafe bundle path')
    require(path.suffix.lower() in ALLOWED_SUFFIXES,
            'binary artifact exclusion: ' + name)
    return name


def inventory(bundle):
    """Read only this directory; reject links and all non-receipt/image formats."""
    require(bundle.is_dir() and not bundle.is_symlink(), 'bundle directory is missing or linked')
    root = bundle.resolve()
    result = {}
    for item in sorted(bundle.rglob('*')):
        require(not item.is_symlink(), 'linked bundle entry: ' + str(item))
        require(item.resolve().is_relative_to(root), 'bundle entry escapes directory')
        if item.is_file():
            name = safe_name(item.relative_to(bundle).as_posix())
            result[name] = item.read_bytes()
    return result


def normalize(path):
    return path.replace('\\', '/')


def artifact_hash(mapping, suffix):
    values = [value for name, value in mapping.items() if normalize(name).endswith(suffix)]
    require(len(values) == 1, 'missing or ambiguous artifact: ' + suffix)
    return hash_value(values[0])


def png(data):
    require(data.startswith(b'\x89PNG\r\n\x1a\n'), 'invalid PNG signature')
    offset, dimensions, ended = 8, None, False
    while offset < len(data):
        require(offset + 12 <= len(data), 'truncated PNG chunk')
        length, = struct.unpack_from('>I', data, offset)
        require(length <= len(data) - offset - 12, 'truncated PNG payload')
        kind = data[offset + 4:offset + 8]
        body = data[offset + 8:offset + 8 + length]
        checksum, = struct.unpack_from('>I', data, offset + 8 + length)
        require(zlib.crc32(kind + body) == checksum, 'PNG CRC mismatch')
        if dimensions is None:
            require(kind == b'IHDR' and length == 13, 'missing PNG IHDR')
            dimensions = struct.unpack_from('>II', body)
            require(all(dimensions), 'empty PNG dimensions')
        offset += 12 + length
        if kind == b'IEND':
            require(length == 0 and offset == len(data), 'invalid PNG end')
            ended = True
            break
    require(ended, 'missing PNG end')
    return dimensions


def device_report(report):
    for flag in ('passed', 'voltage', 'exactCounter', 'demonstration', 'probe'):
        require(report.get(flag) is True, 'device flag not true: ' + flag)
    require(report.get('profile') == 'nucleo-h7s3l8-demo', 'wrong device profile')
    fields = report.get('fields')
    require(isinstance(fields, list) and len(fields) == 4 and report.get('scalarCount') == 17,
            'device must contain four Fields and seventeen scalars')
    names = [field.get('field') for field in fields]
    require(set(names) == {'measure.Voltage', 'device.Counter', 'device.Status', 'device.Probe'}
            and len(set(names)) == len(names), 'device Field catalog mismatch')
    values = {}
    for field in fields:
        require(isinstance(field.get('values'), list), 'missing scalar values')
        for item in field['values']:
            path = item.get('path')
            require(isinstance(path, str) and path not in values and isinstance(item.get('value'), str)
                    and isinstance(item.get('type'), str), 'invalid/duplicate scalar representation')
            values[path] = item
    require(len(values) == 17, 'device scalar count mismatch')
    for member, expected in (('Ua', 230.1), ('Ub', 229.8), ('Uc', 231.2)):
        value = values.get('measure.Voltage.Voltage.' + member, {})
        require(value.get('type') == 'float32', 'voltage scalar type mismatch')
        try:
            same = struct.pack('<f', float(value['value'])) == struct.pack('<f', expected)
        except (KeyError, ValueError, OverflowError, struct.error):
            same = False
        require(same, 'voltage value mismatch: ' + member)
    counter = values.get('device.Counter.Counter', {})
    require(counter == {'path': 'device.Counter.Counter', 'type': 'uint64',
                        'value': '18446744073709551615'}, 'exact Counter representation mismatch')
    demonstration = values.get('device.Status.Status.demonstration', {})
    require(demonstration.get('type') == 'bool' and demonstration.get('value') == 'true',
            'demonstration scalar mismatch')
    for row, expected in enumerate(((1, -2, 3), (4, 5, -6))):
        for column, number in enumerate(expected):
            item = values.get(f'device.Probe.Probe.Matrix.[{row}].[{column}]', {})
            require(item.get('type') == 'int32' and item.get('value') == str(number),
                    'Probe matrix mismatch')
    require(values.get('device.Probe.Probe.Enabled', {}).get('value') == 'true'
            and values.get('device.Probe.Probe.mode', {}).get('type') == 'enum (int16)'
            and values.get('device.Probe.Probe.mode', {}).get('value') == '-2 (unknown)',
            'Probe open enum/bool mismatch')


def verify(files):
    """Pure, in-memory verification; negative controls use this same entry point."""
    require('manifest.json' in files, 'manifest is missing')
    for name in files:
        safe_name(name)
    manifest = decode(files['manifest.json'], 'manifest.json')
    require(manifest.get('schema') == 1 and manifest.get('provenance_commit') == PROVENANCE,
            'provenance commit mismatch')
    require(manifest.get('recorded_build_baseline') == BASELINE
            and manifest.get('hardware_ran_clean_provenance_commit') is False,
            'legacy dirty build must not become a clean provenance hardware claim')
    require(manifest.get('cobs_revision') == COBS, 'COBS revision mismatch')
    require(set(manifest.get('post_hardware_gui_input_changes', [])) == GUI_DELTA,
            'declared GUI input delta mismatch')
    require(manifest.get('hardware_source_input_changes_at_publication') ==
            ['tests/nucleo_h7s3l8/README.md'], 'hardware source publication delta mismatch')
    records = manifest.get('files')
    require(isinstance(records, dict) and records, 'manifest files are missing')
    require(set(files) - {'manifest.json', 'README.md'} == set(records), 'bundle inventory mismatch')
    for name, record in records.items():
        safe_name(name)
        require(isinstance(record, dict) and type(record.get('bytes')) is int,
                'invalid manifest file record: ' + name)
        require(len(files[name]) == record['bytes'], 'byte count mismatch: ' + name)
        require(digest(files[name]) == hash_value(record.get('sha256')), 'file hash mismatch: ' + name)
    receipts = {name: decode(files[name + '/summary.json'], name) for name in RECEIPTS}
    log_map = {}
    for item in manifest.get('receipt_log_files', []):
        key = (item.get('receipt'), item.get('command_index'), item.get('role'))
        require(type(key[1]) is int and key not in log_map and item.get('file') in records,
                'invalid/duplicate command log mapping')
        log_map[key] = item['file']
    expected_logs = set()
    for name, receipt in receipts.items():
        commands = receipt.get('commands')
        require(isinstance(commands, list) and commands, 'missing commands: ' + name)
        for index, command in enumerate(commands):
            exit_code = command.get('exit_code') if name == 'hardware' else command.get('exit')
            require(type(exit_code) is int and exit_code == 0 and command.get('timed_out', False) is False,
                    'failed recorded command: ' + name)
            for role in ('log', 'stdout', 'stderr'):
                if role not in command:
                    continue
                key = (name + '/summary.json', index, role)
                expected_logs.add(key)
                require(key in log_map, 'missing command log mapping: ' + str(key))
                mapped = log_map[key]
                require(PurePosixPath(normalize(command[role])).name == PurePosixPath(mapped).name,
                        'command log basename mismatch')
                expected = command.get(role + '_sha256') if role != 'log' else command.get('sha256')
                require(digest(files[mapped]) == hash_value(expected), 'command log hash mismatch')
    require(set(log_map) == expected_logs, 'extra command log mapping')
    hardware, firmware = receipts['hardware'], receipts['firmware-build']
    for flag in ('test_passed', 'flash_attempted', 'restoration_required',
                 'restored_and_verified', 'qualification_passed'):
        require(hardware.get(flag) is True, 'hardware result not true: ' + flag)
    require(hardware.get('validate_only') is False and not hardware.get('error'), 'hardware did not run')
    target = hardware.get('target', {})
    require(target == {'serial': '002A001F3033510135393935', 'board': 'NUCLEO-H7S3L8',
                       'device_id': '0x485', 'flash_bytes': 65536, 'port': 'COM6'},
            'hardware target mismatch')
    require(hardware.get('baud') == 115200 and hardware.get('flash_bytes') == 65536,
            'hardware link/flash profile mismatch')
    require(hardware.get('backup', {}).get('bytes') == 65536 and
            hash_value(hardware['backup']['sha256']) == hash_value(hardware.get('restored_readback_sha256')),
            'original/restored flash SHA mismatch')
    require(firmware.get('passed') is True and firmware.get('hardware') is False
            and firmware.get('flash') is False, 'firmware build result mismatch')
    require(firmware.get('inputHashesBefore') == firmware.get('inputHashesAfter'), 'firmware input drift')
    elf = artifact_hash(firmware['artifacts'], '/telemetry-nucleo-h7s3.elf')
    require(elf == hash_value(hardware['inputs']['elf']['sha256']) ==
            hash_value(hardware['inputs']['frozen_elf_sha256']), 'tested ELF/build hash mismatch')
    gui = receipts['gui-hardware-build']
    require(artifact_hash(gui['binary_sha256'], '/telemetry-inspector-qml.exe') ==
            hash_value(hardware['inputs']['gui']['sha256']), 'tested GUI/v3 build hash mismatch')
    for name in SERIAL_RECEIPTS:
        receipt = receipts[name]
        require(receipt.get('passed') is True and receipt.get('hardware') is False,
                'host qualification result mismatch: ' + name)
        require(receipt.get('cobs_head') == COBS, 'COBS revision mismatch: ' + name)
        require(receipt.get('source_sha256_before') == receipt.get('source_sha256_after'),
                'host input drift: ' + name)
        require(receipt.get('source_head') != PROVENANCE, 'historical build falsely relabelled as provenance commit')
    require(gui['source_head'] == receipts['serial-windows-final']['source_head'] == BASELINE,
            'Windows historical baseline mismatch')
    before, after = gui['source_sha256_before'], receipts['serial-windows-final']['source_sha256_before']
    delta = {name for name in set(before) | set(after) if before.get(name) != after.get(name)}
    require(delta == GUI_DELTA, 'GUI v3/v4 input delta mismatch')
    for name in ('serial-gcc-final', 'serial-clang-san-final'):
        require(receipts[name]['source_sha256_before'] == after,
                'final Windows/Linux source inventory mismatch: ' + name)
    device_path, image_path = 'hardware/device-report.json', 'hardware/serial-inspector.png'
    require(hardware['device_check'].get('path') == 'device-report.json' and
            digest(files[device_path]) == hash_value(hardware['device_check'].get('sha256')),
            'hardware device report hash mismatch')
    require(hardware['screenshot'].get('path') == 'serial-inspector.png' and
            digest(files[image_path]) == hash_value(hardware['screenshot'].get('sha256')),
            'hardware screenshot hash mismatch')
    device = decode(files[device_path], device_path)
    device_report(device)
    require(device.get('port') == 'COM6' and device.get('baud') == 115200 and
            hardware['device_check'].get('field_count') == 4 and
            hardware['device_check'].get('scalar_count') == 17, 'hardware device counts/link mismatch')
    dimensions = png(files[image_path])
    require(hardware['screenshot'].get('endpoint_rows', 0) > 0 and
            hardware['screenshot'].get('value_rows', 0) > 0, 'QML delegates missing')
    for name in ('serial-gcc-final', 'serial-clang-san-final'):
        receipt = receipts[name]
        require(receipt.get('executions', {}).get('pty') is True and
                receipt['executions'].get('checks', 0) >= 1204, 'PTY tests were not executed')
        checks = receipt.get('device_check', [])
        require([item.get('passed') for item in checks] == [True, False, False, False],
                'headless positive/negative cases mismatch')
        for index, item in enumerate(checks):
            name_in_bundle = name + '/' + safe_name(item['file'])
            require(digest(files[name_in_bundle]) == hash_value(item['sha256']), 'host device report hash mismatch')
            checked = decode(files[name_in_bundle], name_in_bundle)
            require(checked.get('passed') is item['passed'], 'host device report result mismatch')
            if index == 0:
                device_report(checked)
            else:
                require(bool(checked.get('error')), 'negative headless case has no failure')
    sanitized = receipts['serial-clang-san-final']
    require(sanitized.get('sanitize') is True and set(sanitized.get('sanitizers', [])) ==
            {'address', 'undefined'}, 'sanitizer qualification mismatch')
    return {'verified': True, 'files': len(records), 'bytes': sum(len(files[name]) for name in records),
            'command_logs': len(log_map), 'device_fields': 4, 'device_scalars': 17,
            'screenshot_dimensions': list(dimensions), 'provenance_commit': PROVENANCE,
            'hardware_clean_at_provenance_commit': False, 'recorded_build_baseline': BASELINE,
            'limitation': 'Integrity and recorded consistency only; not signed attestation or a new hardware run.'}


def self_test(files):
    """Mutate only in-memory copies, rehashing semantic cases past the hash layer."""
    verify(files)
    controls = []

    def alter(data, name, change):
        value = decode(data[name], name)
        change(value)
        data[name] = json.dumps(value, sort_keys=True).encode()
        manifest = decode(data['manifest.json'], 'manifest.json')
        if name in manifest['files']:
            manifest['files'][name] = {'sha256': digest(data[name]), 'bytes': len(data[name])}
            data['manifest.json'] = json.dumps(manifest).encode()

    def reject(name, mutate, message):
        changed = copy.deepcopy(files)
        mutate(changed)
        try:
            verify(changed)
        except EvidenceError as error:
            require(message in str(error), f'negative control {name} failed at wrong check: {error}')
        else:
            raise EvidenceError('negative control was accepted: ' + name)
        controls.append(name)

    def manifest_change(data, change):
        alter(data, 'manifest.json', change)

    reject('tampered-file', lambda data: data.__setitem__('hardware/device-report.json',
           data['hardware/device-report.json'] + b' '), 'byte count mismatch')
    reject('missing-file', lambda data: data.pop('hardware/device-report.json'), 'inventory mismatch')
    reject('false-clean-hardware', lambda data: manifest_change(data, lambda value:
           value.update(hardware_ran_clean_provenance_commit=True)), 'legacy dirty build')
    reject('wrong-provenance', lambda data: manifest_change(data, lambda value:
           value.update(provenance_commit='0' * 40)), 'provenance commit mismatch')
    reject('binary-exclusion', lambda data: data.update({'original-flash.bin': b'not published'}), 'binary artifact exclusion')
    reject('path-traversal', lambda data: data.update({'../secret.json': b'{}'}), 'unsafe bundle path')
    reject('duplicate-json-key', lambda data: data.__setitem__('manifest.json',
           b'{"schema":1,"schema":1}'), 'duplicate JSON key')
    reject('restore-hash', lambda data: alter(data, 'hardware/summary.json', lambda value:
           value.update(restored_readback_sha256='0' * 64)), 'original/restored flash SHA mismatch')
    reject('tested-elf-link', lambda data: alter(data, 'hardware/summary.json', lambda value:
           value['inputs'].update(frozen_elf_sha256='0' * 64)), 'tested ELF/build hash mismatch')
    reject('tested-gui-link', lambda data: alter(data, 'hardware/summary.json', lambda value:
           value['inputs']['gui'].update(sha256='0' * 64)), 'tested GUI/v3 build hash mismatch')
    reject('cobs-revision', lambda data: alter(data, 'serial-windows-final/summary.json', lambda value:
           value.update(cobs_head='0' * 40)), 'COBS revision mismatch')
    reject('missing-log-map', lambda data: manifest_change(data, lambda value:
           value['receipt_log_files'].pop()), 'missing command log mapping')
    reject('failed-command', lambda data: alter(data, 'hardware/summary.json', lambda value:
           value['commands'][0].update(exit_code=1)), 'failed recorded command')
    reject('log-hash-link', lambda data: alter(data, 'hardware/summary.json', lambda value:
           value['commands'][0].update(stdout_sha256='0' * 64)), 'command log hash mismatch')

    def extra_gui_delta(data):
        def change(value):
            value['source_sha256_before']['examples/inspector_qml/DeviceCheck.cpp'] = '0' * 64
            value['source_sha256_after']['examples/inspector_qml/DeviceCheck.cpp'] = '0' * 64
        alter(data, 'serial-windows-final/summary.json', change)
    reject('undeclared-gui-source-change', extra_gui_delta, 'GUI v3/v4 input delta mismatch')

    def linux_input_drift(data):
        def change(value):
            key = 'examples/inspector_qml/DeviceCheck.cpp'
            value['source_sha256_before'][key] = '0' * 64
            value['source_sha256_after'][key] = '0' * 64
        alter(data, 'serial-gcc-final/summary.json', change)
    reject('cross-platform-input-mismatch', linux_input_drift,
           'final Windows/Linux source inventory mismatch')

    def wrong_device(data, path, value):
        def change(report):
            for field in report['fields']:
                for item in field['values']:
                    if item['path'] == path:
                        item['value'] = value
        alter(data, 'hardware/device-report.json', change)
        alter(data, 'hardware/summary.json', lambda hardware:
              hardware['device_check'].update(sha256=digest(data['hardware/device-report.json'])))
    reject('wrong-u64', lambda data: wrong_device(data, 'device.Counter.Counter',
           '18446744073709551614'), 'exact Counter representation mismatch')
    reject('float-prefix-regression', lambda data: wrong_device(data, 'measure.Voltage.Voltage.Ua',
           '230.110001'), 'voltage value mismatch')
    reject('unknown-enum-proof', lambda data: wrong_device(data, 'device.Probe.Probe.mode',
           '-2'), 'Probe open enum/bool mismatch')
    return controls


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle', type=Path, default=DEFAULT_BUNDLE)
    parser.add_argument('--self-test', action='store_true', help='also reject in-memory corrupted bundles; writes no files')
    arguments = parser.parse_args()
    try:
        files = inventory(arguments.bundle)
        result = verify(files)
        if arguments.self_test:
            result['negative_controls'] = self_test(files)
        print(json.dumps(result, indent=2))
    except (EvidenceError, KeyError, TypeError, OSError) as error:
        print('Evidence verification failed: ' + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
