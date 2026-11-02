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
MAX_LOAD_SECONDS = 1800
MAX_EXACT_INTEGER = (1 << 53) - 1
LOAD_FINISH_GRACE_MS = 25000  # One finite 20-second exchange plus scheduler slack.
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


def reply_items(raw):
    """Navigate a body only after reply() has independently validated its bounds."""
    result, cursor = [], 2
    for _ in range(struct.unpack_from('<H', raw)[0]):
        item_id, status, _reserved, length = struct.unpack_from('<IBBH', raw, cursor)
        cursor += 8
        result.append((item_id, status, raw[cursor:cursor + length]))
        cursor += length
    return result


def rotating_reply(baseline, unavailable_id, count, round_index):
    """Reconstruct the versioned fixed-fixture order without trusting sample IDs."""
    start, stride = (round_index * 7) % 32, 31 if round_index % 2 else 5
    selected = [baseline[(start + index * stride) % 32] for index in range(count)]
    if round_index % 8 == 7:
        selected[count // 2] = (unavailable_id, 3, b'')
    return struct.pack('<H', count) + b''.join(
        struct.pack('<IBBH', item_id, status, 0, len(payload)) + payload
        for item_id, status, payload in selected)


def latency(value, count, elapsed_ms, name):
    require(isinstance(value, dict), 'missing load latency: ' + name)
    lower = 1 if count else 0
    maximum = elapsed_ms * 1000 + 999
    minimum = integer(value.get('min'), name + '.min', lower, maximum)
    largest = integer(value.get('max'), name + '.max', lower, maximum)
    total = integer(value.get('sum'), name + '.sum', lower, maximum)
    if not count:
        require(minimum == largest == total == 0, 'nonzero empty load latency: ' + name)
    else:
        require(minimum <= largest and largest + (count - 1) * minimum <= total
                <= minimum + (count - 1) * largest, 'inconsistent load latency: ' + name)
    return minimum, largest, total


def verify_load(report, minimum_seconds):
    load = report.get('load')
    require(isinstance(load, dict) and load.get('passed') is True,
            'successful sustained load report is required')
    require(type(load.get('schema')) is int and load['schema'] == 1
            and load.get('pattern') == 'rotating-v1', 'load schema/pattern mismatch')
    requested = integer(load.get('requestedSeconds'), 'load.requestedSeconds', 1, MAX_LOAD_SECONDS)
    require(requested >= minimum_seconds, 'load requested duration is too short')
    elapsed = integer(load.get('elapsedMs'), 'load.elapsedMs', requested * 1000,
                      requested * 1000 + LOAD_FINISH_GRACE_MS)
    completed = integer(load.get('completed'), 'load.completed', len(COUNTS), MAX_EXACT_INTEGER)
    for key in ('attempted', 'callbacks', 'physicalRequests'):
        require(integer(load.get(key), 'load.' + key, 1, MAX_EXACT_INTEGER) == completed,
                'load completion accounting mismatch: ' + key)
    baseline_record = next(case for case in report['cases'] if case['count'] == 32)
    baseline = reply_items(reply(baseline_record, 32, [0] * 32))
    missing = [item_id for item_id, status, _ in reply_items(reply(report['mixed'], 3, [0, 0, 3]))
               if status == 3]
    require(len(missing) == 1 and missing[0] not in {item[0] for item in baseline},
            'load Unavailable ID overlaps baseline fields')
    rows = load.get('byCount')
    require(isinstance(rows, list) and len(rows) == len(COUNTS)
            and all(isinstance(row, dict) and type(row.get('count')) is int for row in rows)
            and sorted(row['count'] for row in rows) == list(COUNTS),
            'load must contain exactly four count rows')
    totals = dict.fromkeys(('requestPayloadBytes', 'replyPayloadBytes', 'okItems', 'unavailableItems'), 0)
    row_latencies = {}
    row_counts = {}
    for position, count in enumerate(COUNTS):
        row = next(row for row in rows if row['count'] == count)
        iterations = (completed + len(COUNTS) - 1 - position) // len(COUNTS)
        row_counts[count] = iterations
        for key in ('attempted', 'completed', 'callbacks'):
            require(integer(row.get(key), 'load row ' + key, 1, MAX_EXACT_INTEGER) == iterations,
                    'load row rotation/completion mismatch: ' + key)
        # The selection, stride and failure injection repeat every 32 rounds.
        # Even malicious large counters cause only bounded reconstruction work.
        lengths = [len(rotating_reply(baseline, missing[0], count, index)) for index in range(32)]
        periods, remainder = divmod(iterations, 32)
        unavailable = iterations // 8
        expected = {'requestPayloadBytes': iterations * (2 + 4 * count),
                    'replyPayloadBytes': periods * sum(lengths) + sum(lengths[:remainder]),
                    'okItems': iterations * count - unavailable, 'unavailableItems': unavailable}
        for key, value in expected.items():
            require(integer(row.get(key), 'load row ' + key, 0, MAX_EXACT_INTEGER) == value,
                    'load row aggregate mismatch: ' + key)
            totals[key] += value
        row_latencies[count] = latency(row.get('latencyUs'), iterations, elapsed, 'load row latency')
    for key, value in totals.items():
        require(integer(load.get(key), 'load.' + key, 0, MAX_EXACT_INTEGER) == value,
                'load total aggregate mismatch: ' + key)
    global_latency = latency(load.get('latencyUs'), completed, elapsed, 'load latency')
    require(global_latency == (min(value[0] for value in row_latencies.values()),
                               max(value[1] for value in row_latencies.values()),
                               sum(value[2] for value in row_latencies.values())),
            'load total latency does not match rows')
    hash_value(load.get('transcriptSha256'))
    samples = load.get('samples')
    require(isinstance(samples, list) and len(samples) == len(COUNTS)
            and all(isinstance(sample, dict) and type(sample.get('count')) is int for sample in samples)
            and sorted(sample['count'] for sample in samples) == list(COUNTS),
            'load must retain four latest samples')
    for count in COUNTS:
        sample = next(sample for sample in samples if sample['count'] == count)
        last_round = row_counts[count] - 1
        unavailable = int(last_round % 8 == 7)
        raw = reply(sample, count, [0] * (count - unavailable) + [3] * unavailable)
        require(raw == rotating_reply(baseline, missing[0], count, last_round),
                'load sample order, status or canonical value mismatch')
        for key, expected in (('requestBytes', 2 + 4 * count), ('replyBytes', len(raw)),
                              ('ok', count - unavailable), ('unavailable', unavailable)):
            require(integer(sample.get(key), 'load sample ' + key) == expected,
                    'load sample aggregate mismatch: ' + key)
        milliseconds = integer(sample.get('elapsedMs'), 'load sample elapsedMs', 0, elapsed)
        minimum, largest, _ = row_latencies[count]
        require(minimum // 1000 <= milliseconds <= largest // 1000,
                'load sample latency is outside its row bounds')
    return {'verified': True, 'requested_seconds': requested, 'elapsed_ms': elapsed,
            'completed': completed, 'samples': len(samples), 'transcript_format_verified': True,
            'transcript_limitation': 'Digest format only; full per-exchange history is not retained.'}


def verify_report(report, port='COM6', minimum_load_seconds=0):
    integer(minimum_load_seconds, 'minimum-load-seconds', 0, MAX_LOAD_SECONDS)
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
    result = {'schema': 1, 'verified': True, 'profile': PROFILE,
            'counts': list(COUNTS), 'session_count': len(sessions),
            'final_session': session, 'mixed_ok': 2, 'mixed_unavailable': 1,
            'lifecycle_flags': len(LIFECYCLE), 'hardware_access': False}
    if minimum_load_seconds or 'load' in report:
        result['load'] = verify_load(report, minimum_load_seconds)
    return result


def local_file(root, name):
    require(isinstance(name, str) and '\\' not in name, 'unsafe evidence path')
    relative = PurePosixPath(name)
    require(not relative.is_absolute() and bool(relative.parts)
            and all(part not in ('.', '..') for part in relative.parts), 'unsafe evidence path')
    path = (root / name).resolve()
    require(path.is_relative_to(root.resolve()), 'evidence path escapes directory')
    require(path.is_file(), 'evidence file is missing: ' + name)
    return path


def verify_load_arguments(command, requested):
    arguments = command.get('arguments')
    require(isinstance(arguments, list) and all(isinstance(value, str) for value in arguments),
            'invalid ReadMany command arguments')
    flags = [index for index, value in enumerate(arguments) if value.startswith('--load-seconds')]
    if requested:
        require(len(flags) == 1 and arguments[flags[0]] == '--load-seconds'
                and flags[0] + 1 < len(arguments) and arguments[flags[0] + 1] == str(requested),
                'recorded ReadMany load duration mismatch')
        require(integer(command.get('timeout_seconds'), 'ReadMany process timeout', 1,
                        MAX_EXACT_INTEGER) >= requested + 20, 'ReadMany load process budget is too short')
    else:
        require(not flags, 'unexpected ReadMany load command')


def verify_receipt(root, minimum_load_seconds=0):
    integer(minimum_load_seconds, 'minimum-load-seconds', 0, MAX_LOAD_SECONDS)
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
    requested_load = integer(receipt.get('read_many_load_seconds', 0),
                             'receipt read_many_load_seconds', 0, MAX_LOAD_SECONDS)
    report = decode(report_bytes)
    result = verify_report(report, minimum_load_seconds=max(minimum_load_seconds, requested_load))
    require(('load' in report) == bool(requested_load), 'receipt/report load scope mismatch')
    if requested_load:
        require(report['load']['requestedSeconds'] == requested_load,
                'receipt/report requested load duration mismatch')
    commands = receipt.get('commands')
    require(isinstance(commands, list) and commands, 'recorded commands are missing')
    tags = [command.get('tag') for command in commands]
    require(tags.count('serial-read-many-check') == 1
            and not any(tag in tags for tag in ('serial-gui-smoke', 'serial-device-check')),
            'ReadMany receipt command scope mismatch')
    load_command = next(command for command in commands if command.get('tag') == 'serial-read-many-check')
    verify_load_arguments(load_command, requested_load)
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


def load_self_test(original):
    """Construct bounded independent synthetic transcripts, then corrupt their claims."""
    baseline = [(index + 1, 0, bytes((index + offset) % 256 for offset in range((1, 2, 4, 8)[index % 4])))
                for index in range(32)]

    def encoded(items):
        return struct.pack('<H', len(items)) + b''.join(
            struct.pack('<IBBH', item_id, status, 0, len(data)) + data for item_id, status, data in items)

    def capture(items, microseconds=1000):
        raw = encoded(items)
        unavailable = sum(item[1] == 3 for item in items)
        return {'count': len(items), 'passed': True, 'rawHex': raw.hex(), 'sha256': digest(raw),
                'elapsedMs': microseconds // 1000, 'requestBytes': 2 + 4 * len(items),
                'replyBytes': len(raw), 'ok': len(items) - unavailable, 'unavailable': unavailable}

    def fixture(iterations):
        report = copy.deepcopy(original)
        report['cases'] = [capture(baseline[:count]) for count in COUNTS]
        report['mixed'] = capture([baseline[0], (33, 3, b''), baseline[-1]])
        rows = [{'count': count, 'attempted': 0, 'completed': 0, 'callbacks': 0,
                 'requestPayloadBytes': 0, 'replyPayloadBytes': 0, 'okItems': 0,
                 'unavailableItems': 0} for count in COUNTS]
        times = [[] for _ in COUNTS]
        samples = [None] * len(COUNTS)
        transcript = hashlib.sha256()
        for sequence in range(iterations):
            position, round_index = sequence % 4, sequence // 4
            count = COUNTS[position]
            selected = [baseline[((round_index * 7) + index * (31 if round_index % 2 else 5)) % 32]
                        for index in range(count)]
            if round_index % 8 == 7:
                selected[count // 2] = (33, 3, b'')
            microseconds = 1000 + (sequence % 7) * 100
            sample = capture(selected, microseconds)
            raw = bytes.fromhex(sample['rawHex'])
            transcript.update(struct.pack('<QI', sequence, len(raw)) + raw)
            row = rows[position]
            for key in ('attempted', 'completed', 'callbacks'):
                row[key] += 1
            for key, value in (('requestPayloadBytes', sample['requestBytes']),
                               ('replyPayloadBytes', len(raw)), ('okItems', sample['ok']),
                               ('unavailableItems', sample['unavailable'])):
                row[key] += value
            samples[position] = sample
            times[position].append(microseconds)
        for row, values in zip(rows, times):
            row['latencyUs'] = {'min': min(values), 'max': max(values), 'sum': sum(values)}
        load = {'schema': 1, 'pattern': 'rotating-v1', 'requestedSeconds': 2, 'elapsedMs': 2000,
                'passed': True, 'physicalRequests': iterations, 'byCount': rows, 'samples': samples,
                'transcriptSha256': transcript.hexdigest()}
        for key in ('attempted', 'completed', 'callbacks', 'requestPayloadBytes',
                    'replyPayloadBytes', 'okItems', 'unavailableItems'):
            load[key] = sum(row[key] for row in rows)
        load['latencyUs'] = {'min': min(min(values) for values in times),
                             'max': max(max(values) for values in times),
                             'sum': sum(sum(values) for values in times)}
        report['load'] = load
        return report

    positives = 0
    for iterations in (4, 32, 128, 139):
        verify_report(fixture(iterations), minimum_load_seconds=2)
        positives += 1
    report = fixture(139)  # Beyond one full pattern period, ending mid rotation.
    reordered = copy.deepcopy(report)
    reordered['load']['byCount'].reverse()
    reordered['load']['samples'].reverse()
    verify_report(reordered, minimum_load_seconds=2)
    positives += 1

    def changed(path, value):
        def mutate(result):
            item = result
            for key in path[:-1]:
                item = item[key]
            item[path[-1]] = value
        return mutate

    mutations = {
        'missing-load': lambda result: result.pop('load'),
        'schema': changed(('load', 'schema'), True),
        'pattern': changed(('load', 'pattern'), 'rotating-v2'),
        'failed': changed(('load', 'passed'), False),
        'zero-duration': changed(('load', 'requestedSeconds'), 0),
        'excess-duration': changed(('load', 'requestedSeconds'), MAX_LOAD_SECONDS + 1),
        'short-elapsed': changed(('load', 'elapsedMs'), 1999),
        'excess-elapsed': changed(('load', 'elapsedMs'), 2000 + LOAD_FINISH_GRACE_MS + 1),
        'zero-count': changed(('load', 'completed'), 0),
        'inexact-count': changed(('load', 'completed'), MAX_EXACT_INTEGER + 1),
        'few-count': changed(('load', 'completed'), 3),
        'row-missing': lambda result: result['load']['byCount'].pop(),
        'row-duplicate': changed(('load', 'byCount', 0, 'count'), 12),
        'row-wrong-count': changed(('load', 'byCount', 0, 'completed'), 34),
        'empty-latency': changed(('load', 'latencyUs', 'min'), 0),
        'row-latency-min': changed(('load', 'byCount', 0, 'latencyUs', 'min'), 0),
        'row-latency-max': changed(('load', 'byCount', 0, 'latencyUs', 'max'), 1),
        'row-latency-sum-low': changed(('load', 'byCount', 0, 'latencyUs', 'sum'), 1),
        'row-latency-sum-high': changed(('load', 'byCount', 0, 'latencyUs', 'sum'), 100000),
        'total-latency-min': changed(('load', 'latencyUs', 'min'), 1001),
        'total-latency-max': changed(('load', 'latencyUs', 'max'), 1601),
        'total-latency-sum': changed(('load', 'latencyUs', 'sum'), report['load']['latencyUs']['sum'] + 1),
        'digest-length': changed(('load', 'transcriptSha256'), 'a' * 63),
        'digest-character': changed(('load', 'transcriptSha256'), 'z' * 64),
        'sample-missing': lambda result: result['load']['samples'].pop(),
        'sample-duplicate': changed(('load', 'samples', 0, 'count'), 12),
        'sample-failed': changed(('load', 'samples', 0, 'passed'), False),
        'sample-hash': changed(('load', 'samples', 0, 'sha256'), '0' * 64),
        'sample-elapsed': changed(('load', 'samples', 0, 'elapsedMs'), 2),
    }
    for key in ('attempted', 'callbacks', 'physicalRequests'):
        mutations['total-' + key] = changed(('load', key), report['load'][key] + 1)
    for key in ('attempted', 'callbacks'):
        mutations['row-' + key] = changed(('load', 'byCount', 0, key),
                                          report['load']['byCount'][0][key] + 1)
    for key in ('requestPayloadBytes', 'replyPayloadBytes', 'okItems', 'unavailableItems'):
        mutations['aggregate-' + key] = changed(('load', key), report['load'][key] + 1)
        mutations['row-' + key] = changed(('load', 'byCount', 0, key),
                                          report['load']['byCount'][0][key] + 1)
    for key in ('requestBytes', 'replyBytes', 'ok', 'unavailable'):
        mutations['sample-' + key] = changed(('load', 'samples', 0, key),
                                             report['load']['samples'][0][key] + 1)
    for value, label in ((True, 'bool'), (1.0, 'float'), (float('nan'), 'nan'),
                         (float('inf'), 'infinity'), (-1, 'negative')):
        for key in ('elapsedMs', 'completed', 'requestPayloadBytes'):
            mutations[label + '-' + key] = changed(('load', key), value)
        mutations[label + '-latency'] = changed(('load', 'byCount', 0, 'latencyUs', 'sum'), value)

    def corrupt_sample(result, position):
        sample = result['load']['samples'][position]
        raw = bytearray.fromhex(sample['rawHex'])
        raw[-1] ^= 0x80  # Rehash a canonical, still bounded but incorrect scalar value.
        sample.update(rawHex=raw.hex(), sha256=digest(raw))

    for position in range(4):
        mutations['sample-value-' + str(position)] = lambda result, at=position: corrupt_sample(result, at)
    negatives = 0
    for name, mutate in mutations.items():
        invalid = copy.deepcopy(report)
        mutate(invalid)
        try:
            verify_report(invalid, minimum_load_seconds=2)
        except EvidenceError:
            negatives += 1
            continue
        raise EvidenceError('negative load self-test was accepted: ' + name)
    for candidate, minimum in ((original, 1), (report, 3), (report, True), (report, MAX_LOAD_SECONDS + 1)):
        try:
            verify_report(candidate, minimum_load_seconds=minimum)
        except EvidenceError:
            negatives += 1
            continue
        raise EvidenceError('minimum load duration self-test was accepted')
    command = {'arguments': ['--load-seconds', '1800'], 'timeout_seconds': 1980}
    verify_load_arguments(command, 1800)
    verify_load_arguments({'arguments': ['--report', 'legacy.json']}, 0)
    positives += 2
    for arguments, timeout, requested in (([], 1980, 1800), (['--load-seconds'], 1980, 1800),
            (['--load-seconds', '2'], 1980, 1800), (['--load-seconds=1800'], 1980, 1800),
            (['--load-seconds', '1800', '--load-seconds', '1800'], 1980, 1800),
            (['--load-seconds', '1800'], 1819, 1800), (['--load-seconds', '1800'], True, 1800),
            (['--load-seconds', '1800'], 1980, 0), ([None], 1980, 0)):
        try:
            verify_load_arguments({'arguments': arguments, 'timeout_seconds': timeout}, requested)
        except EvidenceError:
            negatives += 1
            continue
        raise EvidenceError('recorded load command self-test was accepted')
    for value in (float('nan'), float('inf'), -float('inf')):
        invalid = copy.deepcopy(report)
        invalid['load']['elapsedMs'] = value
        try:
            decode(json.dumps(invalid).encode())
        except EvidenceError:
            negatives += 1
            continue
        raise EvidenceError('nonfinite load JSON was accepted')
    return positives, negatives


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
    load_positive, load_negative = load_self_test(report)
    return {'self_test': True, 'positive_controls': 1 + load_positive,
            'negative_controls': len(mutations) + 6 + load_negative,
            'load_positive_controls': load_positive, 'load_negative_controls': load_negative,
            'hardware_access': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--report', type=Path)
    group.add_argument('--receipt', type=Path)
    parser.add_argument('--port', default='COM6')
    parser.add_argument('--minimum-load-seconds', type=int, default=0,
                        help='Require a verified sustained load of at least this many seconds (0..1800)')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    try:
        require(args.report or args.receipt or args.self_test, 'select --report, --receipt or --self-test')
        integer(args.minimum_load_seconds, 'minimum-load-seconds', 0, MAX_LOAD_SECONDS)
        require(not args.minimum_load_seconds or args.report or args.receipt,
                '--minimum-load-seconds requires a report or receipt')
        result = verify_receipt(args.receipt, args.minimum_load_seconds) if args.receipt else (
            verify_report(decode(args.report.read_bytes()), args.port, args.minimum_load_seconds)
            if args.report else {})
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
