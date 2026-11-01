#!/usr/bin/env python3
"""Independently verify the explicit NUCLEO ReadMany CLI report and runner receipt.

Author: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
This verifier reads files only; its self-test uses synthetic in-memory reports.
"""
import argparse
import copy
import hashlib
import json
import math
from pathlib import Path, PurePosixPath
import re
import struct
import sys


PROFILE = 'nucleo-h7s3l8-read-many'
COUNTS = (8, 12, 16, 32)
LIFECYCLE = ('preHandoffTimeout', 'preparedTimeout', 'callbacksOnce',
             'lateReplyRejected', 'reconnect', 'staleBindingRejected', 'noReplay',
             'sessionIncreased', 'txReleased', 'bootstrapComplete', 'samePeer')


class EvidenceError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise EvidenceError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'duplicate JSON key: ' + key)
        result[key] = value
    return result


def decode(data):
    try:
        result = json.loads(data, object_pairs_hook=unique_object,
                            parse_constant=lambda value: (_ for _ in ()).throw(
                                EvidenceError('nonfinite JSON number: ' + value)))
    except (UnicodeError, json.JSONDecodeError) as error:
        raise EvidenceError('invalid JSON report: ' + str(error)) from error
    require(isinstance(result, dict), 'JSON root must be an object')
    return result


def hash_value(value):
    require(isinstance(value, str) and re.fullmatch(r'[0-9a-fA-F]{64}', value),
            'invalid SHA256 value')
    return value.lower()


def hex_bytes(value, name):
    require(isinstance(value, str) and len(value) % 2 == 0
            and re.fullmatch(r'[0-9a-fA-F]*', value), 'invalid hex: ' + name)
    return bytes.fromhex(value)


def integer(value, name, minimum=0, maximum=0xffffffff):
    require(type(value) is int and minimum <= value <= maximum,
            'invalid integer: ' + name)
    return value


def reply(record, expected_count, expected_statuses):
    require(isinstance(record, dict) and record.get('passed') is True,
            'case did not pass')
    raw = hex_bytes(record.get('rawHex'), 'rawHex')
    require(digest(raw) == hash_value(record.get('sha256')), 'reply SHA256 mismatch')
    require(len(raw) >= 2 and struct.unpack_from('<H', raw)[0] == expected_count,
            'reply count mismatch')
    cursor, ids, statuses = 2, [], []
    for _ in range(expected_count):
        require(cursor + 8 <= len(raw), 'truncated reply item header')
        item_id, status, reserved, length = struct.unpack_from('<IBBH', raw, cursor)
        require(item_id not in ids, 'duplicate reply ID')
        require(status <= 4 and reserved == 0, 'invalid reply item status/reserved')
        require(length in (1, 2, 4, 8) if status == 0 else length == 0,
                'noncanonical reply item length')
        cursor += 8
        require(cursor + length <= len(raw), 'truncated reply item payload')
        cursor += length
        ids.append(item_id)
        statuses.append(status)
    require(cursor == len(raw), 'trailing reply bytes')
    require(sorted(statuses) == sorted(expected_statuses), 'reply statuses mismatch')
    return raw


def verify_report(report, port='COM6'):
    require(type(report.get('schema')) is int and report['schema'] == 1, 'report schema mismatch')
    require(report.get('profile') == PROFILE and report.get('passed') is True,
            'ReadMany profile/result mismatch')
    require(report.get('port') == port and type(report.get('baud')) is int
            and report['baud'] == 115200, 'serial port/baud mismatch')
    capability = hex_bytes(report.get('capabilityHex'), 'capabilityHex')
    require(len(capability) == 24 and capability[:4] == b'TRM1',
            '24-byte TRM1 capability is required')
    require(capability[4:8] == bytes((1, 32, 1, 1)), 'capability version/profile/limit mismatch')
    request_limit, reply_limit, session, _fingerprint = struct.unpack_from('<HHIQ', capability, 8)
    require(request_limit == 130 and reply_limit == 514 and session != 0,
            'capability payload limits/session mismatch')
    hash_value(report.get('descriptorSha256'))
    sessions = report.get('sessions')
    require(isinstance(sessions, list) and len(sessions) >= 2, 'recovery sessions are missing')
    for value in sessions:
        integer(value, 'session', 1)
    require(all(before < after for before, after in zip(sessions, sessions[1:])),
            'sessions did not strictly increase')
    require(session == sessions[-1], 'capability does not bind the final admitted session')
    cases = report.get('cases')
    require(isinstance(cases, list) and len(cases) == len(COUNTS), 'four ReadMany cases are required')
    require(all(isinstance(case, dict) and type(case.get('count')) is int for case in cases),
            'invalid ReadMany case count')
    require(sorted(case['count'] for case in cases) == list(COUNTS),
            'ReadMany cases must be exactly 8/12/16/32')
    for case in cases:
        count = case['count']
        raw = reply(case, count, [0] * count)
        elapsed = case.get('elapsedMs')
        require(type(elapsed) in (int, float) and math.isfinite(elapsed) and elapsed >= 0,
                'invalid case elapsedMs')
        require(integer(case.get('requestBytes'), 'requestBytes') == 2 + 4 * count,
                'request byte count mismatch')
        require(integer(case.get('replyBytes'), 'replyBytes') == len(raw),
                'reply byte count mismatch')
        for key, expected in (('requestFrameBytes', 34 + 4 * count),
                              ('replyFrameBytes', 32 + len(raw))):
            if key in case:
                require(integer(case[key], key) == expected, key + ' mismatch')
    mixed = report.get('mixed')
    require(isinstance(mixed, dict) and mixed.get('passed') is True
            and type(mixed.get('ok')) is int and mixed['ok'] == 2
            and type(mixed.get('unavailable')) is int and mixed['unavailable'] == 1,
            'mixed result must contain two OK and one Unavailable')
    reply(mixed, 3, [0, 0, 3])
    lifecycle = report.get('lifecycle')
    require(isinstance(lifecycle, dict), 'lifecycle proof is missing')
    for flag in LIFECYCLE:
        require(lifecycle.get(flag) is True, 'lifecycle proof is not true: ' + flag)
    return {'schema': 1, 'verified': True, 'profile': PROFILE,
            'counts': list(COUNTS), 'session_count': len(sessions),
            'final_session': session, 'mixed_ok': 2, 'mixed_unavailable': 1,
            'lifecycle_flags': len(LIFECYCLE), 'hardware_access': False}


def local_file(root, name):
    require(isinstance(name, str) and '\\' not in name, 'unsafe evidence path')
    relative = PurePosixPath(name)
    require(not relative.is_absolute() and bool(relative.parts)
            and all(part not in ('.', '..') for part in relative.parts), 'unsafe evidence path')
    path = (root / name).resolve()
    require(path.is_relative_to(root.resolve()), 'evidence path escapes directory')
    require(path.is_file(), 'evidence file is missing: ' + name)
    return path


def verify_receipt(root):
    receipt = decode((root / 'summary.json').read_bytes())
    require(receipt.get('profile') == PROFILE and receipt.get('validate_only') is False,
            'ReadMany hardware receipt/profile mismatch')
    for flag in ('test_passed', 'flash_attempted', 'restoration_required',
                 'restored_and_verified', 'qualification_passed', 'source_inputs_unchanged'):
        require(receipt.get(flag) is True, 'hardware receipt is not true: ' + flag)
    require(not receipt.get('error') and receipt.get('baud') == 115200,
            'hardware receipt failed')
    require(receipt.get('target') == {'serial': '002A001F3033510135393935',
            'board': 'NUCLEO-H7S3L8', 'device_id': '0x485', 'flash_bytes': 65536, 'port': 'COM6'},
            'hardware target mismatch')
    backup = receipt.get('backup', {})
    require(backup.get('bytes') == 65536 and hash_value(backup.get('sha256'))
            == hash_value(receipt.get('restored_readback_sha256')), 'flash restoration SHA256 mismatch')
    require(receipt.get('source_hashes') and receipt['source_hashes']
            == receipt.get('source_hashes_after'), 'frozen source inventory mismatch')
    require(isinstance(receipt.get('cobs_head'), str)
            and re.fullmatch(r'[0-9a-f]{40}', receipt['cobs_head'])
            and receipt['cobs_head'] == receipt.get('cobs_head_after'), 'COBS HEAD identity mismatch')
    for prefix in ('@cobs/src/', '@cobs/libs/spsc/'):
        require(any(name.startswith(prefix) for name in receipt['source_hashes']),
                'COBS source inventory is missing: ' + prefix)
    records = receipt.get('evidence_hashes')
    require(isinstance(records, dict) and records, 'evidence hash inventory is missing')
    for name, expected in records.items():
        require(digest(local_file(root, name).read_bytes()) == hash_value(expected),
                'evidence hash mismatch: ' + name)
    original = local_file(root, backup.get('path')).read_bytes()
    require(len(original) == 65536 and digest(original) == hash_value(backup.get('sha256')),
            'original backup bytes/SHA256 mismatch')
    inputs = receipt.get('inputs', {})
    require(hash_value(inputs.get('frozen_cli_sha256'))
            == hash_value(inputs.get('read_many', {}).get('sha256'))
            == digest(local_file(root, inputs.get('frozen_cli_path')).read_bytes()),
            'frozen CLI executable SHA256 mismatch')
    check = receipt.get('read_many_check', {})
    report_bytes = local_file(root, check.get('path')).read_bytes()
    require(digest(report_bytes) == hash_value(check.get('sha256')), 'ReadMany report SHA256 mismatch')
    result = verify_report(decode(report_bytes))
    commands = receipt.get('commands')
    require(isinstance(commands, list) and commands, 'recorded commands are missing')
    tags = [command.get('tag') for command in commands]
    require(tags.count('serial-read-many-check') == 1
            and not any(tag in tags for tag in ('serial-gui-smoke', 'serial-device-check')),
            'ReadMany receipt command scope mismatch')
    successful_readbacks = []
    for command in commands:
        tag = command.get('tag', '')
        restoration = re.fullmatch(r'(restore-original|verify-original-readback)-[1-3]', tag)
        successful = type(command.get('exit_code')) is int and command['exit_code'] == 0 \
            and command.get('timed_out') is False
        require(successful or restoration, 'recorded command failed')
        if successful and tag.startswith('verify-original-readback-'):
            attempt = tag.rsplit('-', 1)[1]
            data = local_file(root, 'restored-readback-' + attempt + '.bin').read_bytes()
            restore = next((item for item in commands if item.get('tag') == 'restore-original-' + attempt), {})
            if len(data) == 65536 and digest(data) == backup['sha256'] \
                    and restore.get('exit_code') == 0 and restore.get('timed_out') is False:
                successful_readbacks.append(tag)
        for role in ('stdout', 'stderr'):
            data = local_file(root, command.get(role)).read_bytes()
            require(digest(data) == hash_value(command.get(role + '_sha256')),
                    'command log SHA256 mismatch')
    require(successful_readbacks, 'independent successful restoration readback is missing')
    result.update({'receipt_verified': True, 'flash_restoration_verified': True,
                   'limitation': 'Recorded consistency and integrity; not a new hardware run.'})
    return result


def self_test():
    def body(count, statuses=None):
        return struct.pack('<H', count) + b''.join(
            struct.pack('<IBBH', index + 1, status, 0, 4 if status == 0 else 0)
            + (struct.pack('<I', index) if status == 0 else b'')
            for index, status in enumerate(statuses or [0] * count))

    def record(count, statuses=None):
        raw = body(count, statuses)
        return {'count': count, 'passed': True, 'rawHex': raw.hex(), 'sha256': digest(raw),
                'elapsedMs': 1, 'requestBytes': 2 + 4 * count, 'replyBytes': len(raw)}

    report = {'schema': 1, 'profile': PROFILE, 'passed': True, 'port': 'COM6', 'baud': 115200,
              'capabilityHex': (b'TRM1' + bytes((1, 32, 1, 1))
                                + struct.pack('<HHIQ', 130, 514, 2, 123)).hex(),
              'descriptorSha256': 'a' * 64, 'sessions': [1, 2],
              'cases': [record(count) for count in COUNTS],
              'mixed': dict(record(3, [0, 3, 0]), ok=2, unavailable=1),
              'lifecycle': dict.fromkeys(LIFECYCLE, True)}
    verify_report(report)
    mutations = {
        'missing-case': lambda r: r['cases'].pop(),
        'wrong-count': lambda r: r['cases'][0].update(count=9),
        'wrong-hash': lambda r: r['cases'][0].update(sha256='0' * 64),
        'wrong-bytes': lambda r: r['cases'][0].update(replyBytes=0),
        'wrong-request-bytes': lambda r: r['cases'][0].update(requestBytes=0),
        'wrong-capability': lambda r: r.update(capabilityHex='00' * 24),
        'duplicate-session': lambda r: r.update(sessions=[1, 1]),
        'wrong-port': lambda r: r.update(port='COM7'),
        'false-pass': lambda r: r.update(passed=False),
        'mixed-wrong-counts': lambda r: r['mixed'].update(ok=3),
        'nonfinite-time': lambda r: r['cases'][0].update(elapsedMs=float('nan')),
    }
    for flag in LIFECYCLE:
        mutations['lifecycle-' + flag] = lambda r, key=flag: r['lifecycle'].update({key: False})
    for name, mutation in mutations.items():
        changed = copy.deepcopy(report)
        mutation(changed)
        try:
            verify_report(changed)
        except EvidenceError:
            continue
        raise EvidenceError('negative self-test was accepted: ' + name)
    for name, transform in (
            ('duplicate-ID', lambda raw: raw[:14] + raw[2:6] + raw[18:]),
            ('wrong-status', lambda raw: raw[:6] + b'\x03' + raw[7:]),
            ('reserved', lambda raw: raw[:7] + b'\x01' + raw[8:]),
            ('trailing', lambda raw: raw + b'\0'),
            ('truncated', lambda raw: raw[:-1])):
        changed = copy.deepcopy(report)
        raw = transform(bytes.fromhex(changed['cases'][0]['rawHex']))
        changed['cases'][0].update(rawHex=raw.hex(), sha256=digest(raw), replyBytes=len(raw))
        try:
            verify_report(changed)
        except EvidenceError:
            continue
        raise EvidenceError('negative wire self-test was accepted: ' + name)
    try:
        decode(b'{"schema":1,"schema":1}')
    except EvidenceError:
        pass
    else:
        raise EvidenceError('duplicate JSON key was accepted')
    return {'self_test': True, 'positive_controls': 1,
            'negative_controls': len(mutations) + 6, 'hardware_access': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--report', type=Path)
    group.add_argument('--receipt', type=Path)
    parser.add_argument('--port', default='COM6')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    try:
        require(args.report or args.receipt or args.self_test, 'select --report, --receipt or --self-test')
        result = verify_receipt(args.receipt) if args.receipt else (
            verify_report(decode(args.report.read_bytes()), args.port) if args.report else {})
        if args.self_test:
            result.update(self_test())
        encoded = json.dumps(result, indent=2) + '\n'
        if args.output:
            require(not args.output.exists(), 'verification output already exists')
            args.output.write_text(encoded, encoding='utf-8')
        print(encoded, end='')
    except (EvidenceError, OSError, TypeError, KeyError, struct.error) as error:
        print('ReadMany evidence verification failed: ' + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
