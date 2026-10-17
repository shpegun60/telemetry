#!/usr/bin/env python3
"""Check recorded H7S RPC coverage and provenance without accessing a device.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""
import argparse
import copy
import json
from pathlib import Path
import re
import struct


SCENARIO_MINIMUMS = dict(uart_split=4, fragment_retry=4, pending=3, ready=7,
                         generation=6, timeout=5, command=6, stats=3)
FLASH_ADDRESS = 0x08000000
FLASH_BYTES = 65536
CLOCK_HZ = 600000000


def require(condition, message):
    if not condition:
        raise ValueError(message)


def nonempty_text(value):
    return isinstance(value, str) and bool(value.strip())


def positive_integer(value):
    return type(value) is int and value > 0


def digest(value, label):
    require(isinstance(value, str) and re.fullmatch(r'[0-9a-f]{64}', value),
            'Malformed SHA-256: ' + label)


def digest_map(value, label):
    require(isinstance(value, dict) and bool(value), 'Missing digest map: ' + label)
    for name, sha256 in value.items():
        require(nonempty_text(name), 'Missing digest name: ' + label)
        digest(sha256, label + '/' + name)


def decode_envelope(value, direction):
    require(isinstance(value, str) and re.fullmatch(r'(?:[0-9a-fA-F]{2})+', value),
            'Malformed UART ' + direction + ' hex')
    packet = bytes.fromhex(value)
    require(len(packet) >= 2, 'Truncated UART envelope prefix')
    size = int.from_bytes(packet[:2], 'little')
    require(9 <= size <= 256 and len(packet) == size + 2, 'Invalid UART envelope length')
    kind, peer, generation = struct.unpack('<BII', packet[2:11])
    require(kind in (1, 2, 3, 127), 'Unknown UART envelope kind')
    return kind, peer, generation, packet[11:]


def resource_request(payload):
    require(bool(payload) and payload[0] in (2, 3, 4), 'Unknown Resource operation')
    operation = payload[0]
    if operation == 2:
        require(len(payload) == 5, 'Invalid Resource STAT request length')
    elif operation == 3:
        require(len(payload) == 13, 'Invalid Resource READ request length')
    else:
        require(len(payload) >= 16, 'Truncated Resource WRITE request')
        require(payload[13] in (0, 1), 'Invalid Resource WRITE final flag')
        require(len(payload) == 16 + int.from_bytes(payload[14:16], 'little'),
                'Invalid Resource WRITE payload length')
    return operation


def resource_reply(operation, payload):
    require(bool(payload) and payload[0] <= 8, 'Invalid Resource reply status')
    if operation == 2:
        require(len(payload) == 6, 'Invalid Resource STAT reply length')
    elif operation == 3:
        require(len(payload) >= 12, 'Truncated Resource READ reply')
        require(payload[9] in (0, 1), 'Invalid Resource READ EOF flag')
        require(len(payload) == 12 + int.from_bytes(payload[10:12], 'little'),
                'Invalid Resource READ payload length')
    else:
        require(len(payload) == 14, 'Invalid Resource WRITE reply length')
        require(payload[13] in (0, 1), 'Invalid Resource WRITE complete flag')


def verify_frame_pair(frame):
    require(isinstance(frame, dict) and set(frame) == {'request', 'reply'},
            'Malformed UART frame pair')
    kind, peer, generation, request = decode_envelope(frame['request'], 'request')
    reply_kind, reply_peer, reply_generation, reply = decode_envelope(frame['reply'], 'reply')
    require(kind in (1, 2, 3), 'Invalid UART request kind')
    require((peer, generation) == (reply_peer, reply_generation),
            'UART reply peer/generation metadata changed')
    require(reply_kind in (kind, 127), 'UART reply kind does not match request')
    if kind == 1:
        operation = resource_request(request)
    elif kind == 2:
        require(len(request) == 1 and request[0] in (0, 1), 'Invalid fixture hold/release request')
    else:
        require(not request, 'Invalid fixture session request')
    if reply_kind == 127:
        require(len(reply) == 1 and reply[0] in (1, 2, 3), 'Invalid fixture refusal reason')
    elif reply_kind == 1:
        resource_reply(operation, reply)
    else:
        require(len(reply) == 1, 'Invalid fixture control status length')


def verify(receipt):
    """Reject incomplete or internally inconsistent receipts; read no artifacts."""
    try:
        _verify(receipt)
    except (KeyError, TypeError, AttributeError) as error:
        raise ValueError('Malformed receipt: ' + str(error)) from error


def _verify(receipt):
    require(isinstance(receipt, dict), 'Receipt must be an object')
    require(receipt['completed'] is True and receipt['restored_and_verified'] is True,
            'Incomplete or unrestored run')
    require(receipt['board'] == 'NUCLEO-H7S3L8', 'Unexpected board')
    require(nonempty_text(receipt['serial']) and nonempty_text(receipt['port']),
            'Explicit adapter serial and port are required')
    require(type(receipt['backup_bytes']) is int and receipt['backup_bytes'] == FLASH_BYTES
            and type(receipt['restored_bytes']) is int and receipt['restored_bytes'] == FLASH_BYTES,
            'Incomplete Flash backup/restore')
    digest(receipt['backup_sha256'], 'backup')
    digest(receipt['restored_sha256'], 'restore')
    require(receipt['backup_sha256'] == receipt['restored_sha256'], 'Flash restore mismatch')

    inputs = receipt['build_inputs']
    digest_map(inputs, 'build_inputs')
    provenance = receipt['build_provenance']
    require(isinstance(provenance, dict), 'Missing build provenance')
    digest_map(provenance['input_lf_sha256'], 'input_lf_sha256')
    require(inputs == provenance['input_lf_sha256'], 'Build input manifests disagree')
    require(isinstance(provenance['source_head'], str)
            and re.fullmatch(r'(?:[0-9a-f]{40}|[0-9a-f]{64})', provenance['source_head']),
            'Malformed source revision')
    require(type(provenance['source_dirty']) is bool, 'Missing source worktree state')
    require(receipt['source_head'] == provenance['source_head']
            and type(receipt['source_dirty']) is bool
            and receipt['source_dirty'] == provenance['source_dirty'],
            'Source provenance disagrees')
    digest_map(provenance['scaffold_sha256'], 'scaffold_sha256')
    digest(provenance['compiler_sha256'], 'compiler_sha256')
    require(nonempty_text(provenance['compiler'])
            and re.search(r'14\.3\.1(?:\s|$)', provenance['compiler'])
            and nonempty_text(provenance['compiler_path'])
            and receipt['compiler'] == provenance['compiler'], 'Compiler provenance disagrees')
    require(type(provenance['required_clock_hz']) is int
            and provenance['required_clock_hz'] == CLOCK_HZ
            and provenance['internal_flash_address'] == '0x08000000'
            and type(provenance['internal_flash_bytes']) is int
            and provenance['internal_flash_bytes'] == FLASH_BYTES,
            'Unexpected target build contract')

    images = receipt['images']
    require(isinstance(images, list) and len(images) == 2, 'Incomplete image matrix')
    require(all(isinstance(image, dict) for image in images), 'Malformed image entry')
    require({image['optimization'] for image in images} == {'O2', 'Os'}, 'Incomplete image matrix')
    require(isinstance(receipt['runs'], dict) and set(receipt['runs']) == {'O2', 'Os'},
            'Incomplete run matrix')
    library_inputs = {name: sha256 for name, sha256 in inputs.items() if name.startswith('lib/')}
    require(bool(library_inputs), 'Missing library source inputs')
    for image in images:
        opt = image['optimization']
        require(image['variant'] == 'Current' and nonempty_text(image['elf'])
                and nonempty_text(image['binary']), 'Missing image identity')
        require(positive_integer(image['flash_bytes']) and image['flash_bytes'] <= FLASH_BYTES,
                'Image exceeds backed-up Flash')
        digest(image['elf_sha256'], opt + '/elf')
        digest(image['binary_sha256'], opt + '/binary')
        digest_map(image['library_sources'], opt + '/library_sources')
        require(image['library_sources'] == library_inputs, 'Library source manifests disagree')
        for name in ('objects_sha256', 'common_objects_sha256'):
            digest_map(image[name], opt + '/' + name)
        digest(image['linker_sha256'], opt + '/linker')
        require(isinstance(image['flags'], list) and bool(image['flags'])
                and all(nonempty_text(flag) for flag in image['flags'])
                and '-' + opt in image['flags'], 'Missing image build flags')
        span = image['flash_load_span']
        require(isinstance(span, list) and len(span) == 2
                and all(type(address) is int for address in span)
                and span == [FLASH_ADDRESS, FLASH_ADDRESS + image['flash_bytes']],
                'Image Flash span disagrees')
        require(isinstance(image['stack_usage'], dict) and bool(image['stack_usage']),
                'Missing stack provenance')
        for name, report in image['stack_usage'].items():
            require(nonempty_text(name) and isinstance(report, dict), 'Malformed stack report')
            digest(report['sha256'], opt + '/stack/' + name)
            require(type(report['bytes']) is int and report['bytes'] >= 0, 'Invalid stack report size')

        report = receipt['runs'][opt]
        require(isinstance(report, dict), 'Malformed device report')
        scenarios = report['scenarios']
        require(isinstance(scenarios, dict) and set(scenarios) == set(SCENARIO_MINIMUMS),
                'Missing or unexpected scenario group')
        for name, minimum in SCENARIO_MINIMUMS.items():
            require(positive_integer(scenarios[name]) and scenarios[name] >= minimum,
                    'Insufficient scenario checks: ' + name)
        require(positive_integer(report['checks'])
                and report['checks'] >= sum(SCENARIO_MINIMUMS.values())
                and report['checks'] == sum(scenarios.values()),
                'Aggregate check count disagrees')
        require(type(report['failures']) is int and report['failures'] == 0,
                'Device correctness failure')
        require(type(report['clock_hz']) is int and report['clock_hz'] == CLOCK_HZ,
                'Unexpected target clock')
        require(isinstance(report['uart_frames'], list) and bool(report['uart_frames']),
                'Missing UART frame corpus')
        for frame in report['uart_frames']:
            verify_frame_pair(frame)


def synthetic_envelope(kind, payload=b'', peer=1, generation=1):
    body = struct.pack('<BII', kind, peer, generation) + payload
    return (struct.pack('<H', len(body)) + body).hex()


def synthetic_receipt():
    """A deliberately synthetic valid receipt for device-free verifier controls."""
    inputs = {'lib/synthetic.hpp': 'a' * 64, 'tests/synthetic.cpp': 'b' * 64}
    provenance = dict(source_head='c' * 40, source_dirty=True, input_lf_sha256=inputs,
        scaffold_sha256={'synthetic.c': 'd' * 64}, compiler='synthetic ARM 14.3.1 test',
        compiler_path='synthetic/compiler', compiler_sha256='e' * 64,
        required_clock_hz=CLOCK_HZ, internal_flash_address='0x08000000', internal_flash_bytes=FLASH_BYTES)
    images = [dict(variant='Current', optimization=opt, elf='synthetic.elf', binary='synthetic.bin',
        elf_sha256='1' * 64, binary_sha256='2' * 64, flash_bytes=1024,
        library_sources={'lib/synthetic.hpp': 'a' * 64}, objects_sha256={'fixture.o': '3' * 64},
        common_objects_sha256={'startup.o': '4' * 64}, linker_sha256='5' * 64,
        flags=['-std=c++20', '-' + opt], flash_load_span=[FLASH_ADDRESS, FLASH_ADDRESS + 1024],
        stack_usage={'fixture.su': dict(sha256='6' * 64, bytes=12)}) for opt in ('O2', 'Os')]
    scenarios = dict(SCENARIO_MINIMUMS)
    frames = [
        dict(request=synthetic_envelope(1, struct.pack('<BIQ', 3, 0, 0)),
             reply=synthetic_envelope(1, struct.pack('<BQBH', 0, 0, 1, 0))),
        dict(request=synthetic_envelope(1, struct.pack('<BIQBH', 4, 0, 0, 1, 1) + b'X'),
             reply=synthetic_envelope(1, struct.pack('<BQIB', 0, 1, 1, 1))),
        dict(request=synthetic_envelope(1, struct.pack('<BI', 2, 1)),
             reply=synthetic_envelope(1, struct.pack('<BIB', 0, 44, 1))),
        dict(request=synthetic_envelope(2, b'\1'), reply=synthetic_envelope(2, b'\0')),
        dict(request=synthetic_envelope(3), reply=synthetic_envelope(3, b'\0')),
        dict(request=synthetic_envelope(1, struct.pack('<BIQ', 3, 0, 0)),
             reply=synthetic_envelope(127, b'\2')),
    ]
    return dict(completed=True, restored_and_verified=True, board='NUCLEO-H7S3L8',
        serial='synthetic-adapter', port='synthetic-port', backup_bytes=FLASH_BYTES,
        restored_bytes=FLASH_BYTES, backup_sha256='7' * 64, restored_sha256='7' * 64,
        source_head=provenance['source_head'], source_dirty=provenance['source_dirty'],
        compiler=provenance['compiler'], build_provenance=provenance, build_inputs=inputs,
        images=images, runs={opt: dict(checks=sum(scenarios.values()), failures=0,
            scenarios=dict(scenarios), clock_hz=CLOCK_HZ,
            uart_frames=copy.deepcopy(frames)) for opt in ('O2', 'Os')})


def self_test(receipt):
    verify(receipt)
    verify(synthetic_receipt())

    def set_scenario_count(altered, name, count):
        report = altered['runs']['O2']
        report['scenarios'][name] = count
        report['checks'] = sum(report['scenarios'].values())

    mutations = [
        ('incomplete', lambda r: r.update(completed=False)),
        ('unrestored', lambda r: r.update(restored_and_verified=False)),
        ('wrong board', lambda r: r.update(board='wrong')),
        ('missing serial', lambda r: r.pop('serial')),
        ('empty serial', lambda r: r.update(serial=' ')),
        ('missing port', lambda r: r.pop('port')),
        ('short backup', lambda r: r.update(backup_bytes=FLASH_BYTES - 1)),
        ('short restore', lambda r: r.update(restored_bytes=FLASH_BYTES - 1)),
        ('restore mismatch', lambda r: r.update(restored_sha256='8' * 64)),
        ('malformed backup hash', lambda r: r.update(backup_sha256='bad')),
        ('missing image', lambda r: r['images'].pop()),
        ('repeated optimization', lambda r: r['images'][1].update(optimization=r['images'][0]['optimization'])),
        ('malformed image hash', lambda r: r['images'][0].update(binary_sha256='bad')),
        ('missing object hashes', lambda r: r['images'][0].update(objects_sha256={})),
        ('missing library hashes', lambda r: r['images'][0].update(library_sources={})),
        ('invalid flash span', lambda r: r['images'][0].update(flash_load_span=[0, 1024])),
        ('malformed compiler hash', lambda r: r['build_provenance'].update(compiler_sha256='bad')),
        ('missing scaffold hashes', lambda r: r['build_provenance'].update(scaffold_sha256={})),
        ('malformed source hash', lambda r: r['build_inputs'].update({'lib/synthetic.hpp': 'bad'})),
        ('manifest disagreement', lambda r: r.update(build_inputs={'lib/other.hpp': '9' * 64})),
        ('missing run', lambda r: r['runs'].pop('Os')),
        ('missing group', lambda r: r['runs']['O2']['scenarios'].pop('timeout')),
        ('count loss', lambda r: set_scenario_count(r, 'timeout', 0)),
        ('boolean count', lambda r: r['runs']['O2']['scenarios'].update(timeout=True)),
        ('aggregate disagreement', lambda r: r['runs']['O2'].update(checks=r['runs']['O2']['checks'] + 1)),
        ('nonzero failures', lambda r: r['runs']['O2'].update(failures=1)),
        ('wrong clock', lambda r: r['runs']['O2'].update(clock_hz=1)),
        ('empty UART corpus', lambda r: r['runs']['O2'].update(uart_frames=[])),
        ('malformed request hex', lambda r: r['runs']['O2']['uart_frames'][0].update(request='0g')),
        ('odd reply hex', lambda r: r['runs']['O2']['uart_frames'][0].update(reply='0')),
    ]
    mutations += [(name + ' below minimum',
                   lambda r, name=name: set_scenario_count(r, name, SCENARIO_MINIMUMS[name] - 1))
                  for name in SCENARIO_MINIMUMS]

    read_request = struct.pack('<BIQ', 3, 0, 0)
    read_reply = struct.pack('<BQBH', 0, 0, 1, 0)
    write_request = struct.pack('<BIQBH', 4, 0, 0, 1, 1) + b'X'
    write_reply = struct.pack('<BQIB', 0, 1, 1, 1)
    stat_request = struct.pack('<BI', 2, 1)
    stat_reply = struct.pack('<BIB', 0, 44, 1)

    def frame_mutation(request=None, reply=None):
        pair = dict(request=request if request is not None else synthetic_envelope(1, read_request),
                    reply=reply if reply is not None else synthetic_envelope(1, read_reply))
        return lambda r: r['runs']['O2'].update(uart_frames=[pair])

    def resource_mutation(request=read_request, reply=read_reply):
        return frame_mutation(synthetic_envelope(1, request), synthetic_envelope(1, reply))

    mutations += [
        ('truncated envelope prefix', frame_mutation(request='09')),
        ('envelope below minimum', frame_mutation(request=(b'\x08\0' + bytes(8)).hex())),
        ('envelope above maximum', frame_mutation(request=(b'\x01\x01' + bytes(257)).hex())),
        ('envelope declared length mismatch', frame_mutation(request='0900' + synthetic_envelope(1, read_request)[4:])),
        ('envelope trailing bytes', frame_mutation(reply=synthetic_envelope(1, read_reply) + '00')),
        ('unknown request envelope kind', frame_mutation(request=synthetic_envelope(4, read_request))),
        ('refusal as request kind', frame_mutation(request=synthetic_envelope(127, b'\1'))),
        ('unknown reply envelope kind', frame_mutation(reply=synthetic_envelope(4, read_reply))),
        ('reply kind mismatch', frame_mutation(reply=synthetic_envelope(3, b'\0'))),
        ('reply peer mismatch', frame_mutation(reply=synthetic_envelope(1, read_reply, peer=2))),
        ('reply generation mismatch', frame_mutation(reply=synthetic_envelope(1, read_reply, generation=2))),
        ('empty hold/release request', frame_mutation(synthetic_envelope(2), synthetic_envelope(2, b'\0'))),
        ('invalid hold/release flag', frame_mutation(synthetic_envelope(2, b'\2'), synthetic_envelope(2, b'\0'))),
        ('nonempty session request', frame_mutation(synthetic_envelope(3, b'\0'), synthetic_envelope(3, b'\0'))),
        ('empty hold/release status', frame_mutation(synthetic_envelope(2, b'\1'), synthetic_envelope(2))),
        ('long session status', frame_mutation(synthetic_envelope(3), synthetic_envelope(3, b'\0\0'))),
        ('empty refusal', frame_mutation(reply=synthetic_envelope(127))),
        ('long refusal', frame_mutation(reply=synthetic_envelope(127, b'\1\2'))),
        ('zero refusal reason', frame_mutation(reply=synthetic_envelope(127, b'\0'))),
        ('unknown refusal reason', frame_mutation(reply=synthetic_envelope(127, b'\4'))),
        ('empty Resource request', resource_mutation(request=b'')),
        ('unknown Resource operation', resource_mutation(request=b'\1' + read_request[1:])),
        ('short READ request', resource_mutation(request=read_request[:-1])),
        ('long READ request', resource_mutation(request=read_request + b'\0')),
        ('short READ reply', resource_mutation(reply=read_reply[:-1])),
        ('READ payload count mismatch', resource_mutation(reply=read_reply[:10] + b'\1\0')),
        ('READ trailing payload', resource_mutation(reply=read_reply + b'X')),
        ('READ EOF flag', resource_mutation(reply=read_reply[:9] + b'\2' + read_reply[10:])),
        ('READ status', resource_mutation(reply=b'\11' + read_reply[1:])),
        ('short WRITE request', resource_mutation(request=write_request[:15], reply=write_reply)),
        ('WRITE payload count mismatch', resource_mutation(request=write_request[:-1], reply=write_reply)),
        ('WRITE final flag', resource_mutation(request=write_request[:13] + b'\2' + write_request[14:], reply=write_reply)),
        ('short WRITE reply', resource_mutation(request=write_request, reply=write_reply[:-1])),
        ('long WRITE reply', resource_mutation(request=write_request, reply=write_reply + b'\0')),
        ('WRITE complete flag', resource_mutation(request=write_request, reply=write_reply[:13] + b'\2')),
        ('WRITE status', resource_mutation(request=write_request, reply=b'\11' + write_reply[1:])),
        ('short STAT request', resource_mutation(request=stat_request[:-1], reply=stat_reply)),
        ('long STAT request', resource_mutation(request=stat_request + b'\0', reply=stat_reply)),
        ('short STAT reply', resource_mutation(request=stat_request, reply=stat_reply[:-1])),
        ('long STAT reply', resource_mutation(request=stat_request, reply=stat_reply + b'\0')),
        ('STAT status', resource_mutation(request=stat_request, reply=b'\11' + stat_reply[1:])),
    ]
    for label, mutate in mutations:
        altered = copy.deepcopy(receipt)
        mutate(altered)
        try:
            verify(altered)
        except ValueError:
            continue
        raise AssertionError('Receipt mutation accepted: ' + label)
    print(f'H7S RPC: {len(mutations)} receipt mutation controls passed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--receipt', type=Path, help='recorded receipt to check; no device access')
    parser.add_argument('--self-test', action='store_true', help='test rejection controls, using synthetic data by default')
    args = parser.parse_args()
    if args.receipt is None and not args.self_test:
        parser.error('--receipt is required unless --self-test is used')
    receipt = (json.loads(args.receipt.read_text(encoding='utf-8'))
               if args.receipt is not None else synthetic_receipt())
    verify(receipt)
    if args.self_test:
        self_test(receipt)
    print('Recorded H7S RPC evidence is internally consistent' if args.receipt is not None
          else 'Synthetic H7S RPC verifier checks complete; no hardware evidence produced')


if __name__ == '__main__':
    main()
