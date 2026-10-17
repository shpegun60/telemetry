#!/usr/bin/env python3
"""Build bounded H7S RPC UART images; explicitly selected hardware is optional.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import runpy
import struct
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
H7 = runpy.run_path(str(ROOT / 'tests/h7s_support/build.py'))
FLASH_BYTES = 65536


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def envelope(kind, peer, generation, payload=b''):
    body = struct.pack('<BII', kind, peer, generation) + payload
    if not 9 <= len(body) <= 256:
        raise ValueError('Fixture envelope length is outside its fixed storage')
    return struct.pack('<H', len(body)) + body


def request(transaction, delta=0, operation=3):
    payload = struct.pack('<I', delta) + bytes(range(64)) if operation == 3 else b''
    return struct.pack('<4sHHIIIBBH', b'TRPC', 1, 0, transaction, 0,
                       len(payload), operation, 0, 0) + payload


def cursor(transaction, offset):
    return (transaction << 32) | offset


def write_packet(position, payload, final):
    return struct.pack('<BIQBH', 4, 0, position, int(final), len(payload)) + payload


def read_packet(index=0, position=0):
    return struct.pack('<BIQ', 3, index, position)


def read_result(payload):
    if len(payload) < 12:
        raise RuntimeError('Truncated Resource READ reply')
    status, position, eof, size = struct.unpack('<BQBH', payload[:12])
    if status != 0 or eof not in (0, 1) or len(payload) != 12 + size:
        raise RuntimeError('Malformed or failed Resource READ reply')
    return position, bool(eof), payload[12:]


def rpc_header(payload):
    if len(payload) < 32:
        raise RuntimeError('Truncated RPC snapshot')
    words = struct.unpack('<4sHHIIIBBBBII', payload[:32])
    if words[:3] != (b'TRPS', 1, 0):
        raise RuntimeError('Unexpected RPC reply version')
    return dict(zip(('transaction', 'endpoint', 'result', 'operation', 'state',
                     'dispatch', 'endpoint_status', 'received', 'request'), words[3:]))


class UartProfile:
    def __init__(self, port, report, log):
        self.port, self.report, self.log = port, report, log
        self.generation = 1

    def check(self, scenario, condition, message):
        self.report['checks'] += 1
        counts = self.report['scenarios']
        counts[scenario] = counts.get(scenario, 0) + 1
        if not condition:
            self.report['failures'] += 1
            raise RuntimeError(scenario + ': ' + message)

    def exact(self, count):
        result = bytearray()
        deadline = time.monotonic() + 3
        while len(result) < count and time.monotonic() < deadline:
            chunk = self.port.read(count - len(result))
            result.extend(chunk)
        if len(result) != count:
            raise RuntimeError(f'UART reply timeout: {len(result)}/{count} bytes')
        return bytes(result)

    def exchange(self, payload=b'', *, kind=1, peer=1, generation=None, split=False):
        epoch = self.generation if generation is None else generation
        outgoing = envelope(kind, peer, epoch, payload)
        if split:
            # Split both the length prefix and the unchanged Resource packet.
            for start, end in ((0, 1), (1, 8), (8, 17), (17, len(outgoing))):
                self.port.write(outgoing[start:end])
                self.port.flush()
                time.sleep(0.002)
        else:
            self.port.write(outgoing)
            self.port.flush()
        prefix = self.exact(2)
        length, = struct.unpack('<H', prefix)
        if not 9 <= length <= 256:
            raise RuntimeError('Target envelope length exceeds fixed bounds')
        incoming = prefix + self.exact(length)
        entry = dict(request=outgoing.hex(), reply=incoming.hex())
        self.report['uart_frames'].append(entry)
        self.log.write(json.dumps(entry) + '\n')
        self.log.flush()
        reply_kind, reply_peer, reply_epoch = struct.unpack('<BII', incoming[2:11])
        if (reply_peer, reply_epoch) != (peer, epoch):
            raise RuntimeError('Target changed fixture peer/generation metadata')
        return reply_kind, incoming[11:]

    def resource(self, payload, **kwargs):
        kind, result = self.exchange(payload, **kwargs)
        if kind != 1:
            raise RuntimeError('Resource packet was refused by fixture admission')
        return result

    def control(self, kind, payload=b''):
        reply_kind, reply = self.exchange(payload, kind=kind)
        if reply_kind != kind or reply != b'\0':
            raise RuntimeError('Fixture control was refused')

    def stats(self):
        position, eof, payload = read_result(self.resource(read_packet(1)))
        if position != 44 or not eof or len(payload) != 44:
            raise RuntimeError('Malformed fixture stats record')
        words = struct.unpack('<11I', payload)
        if words[0] != 0x53504352:
            raise RuntimeError('Unexpected fixture stats version')
        return dict(zip(('magic', 'clock', 'calls', 'value', 'before_ack', 'generation',
                         'rejected', 'packets', 'acks', 'queue_drops', 'commands'), words))

    def ack(self, payload, transaction, offset, count, final):
        return payload == struct.pack('<BQIB', 0, cursor(transaction, offset), count, int(final))

    def run(self):
        self.control(3)
        self.control(2, b'\1')
        packet = request(1, 7)
        first = self.resource(write_packet(0, packet[:24], False), split=True)
        self.check('uart_split', self.ack(first, 1, 24, 24, False), 'split prefix/header ACK')
        self.check('uart_split', self.stats()['calls'] == 0, 'partial request invoked callback')
        # Leave one incomplete UART envelope; bounded target receive drains it.
        self.port.write(struct.pack('<H', 30) + b'\1\1\0')
        self.port.flush()
        time.sleep(0.45)
        self.check('uart_split', self.stats()['calls'] == 0, 'incomplete envelope recovery')
        _, _, snapshot = read_result(self.resource(read_packet()))
        self.check('uart_split', rpc_header(snapshot)['received'] == 24,
                   'UART fragment changed accepted RPC bytes')

        middle = self.resource(write_packet(cursor(1, 24), packet[24:55], False))
        self.check('fragment_retry', self.ack(middle, 1, 55, 31, False), 'middle fragment ACK')
        final_packet = write_packet(cursor(1, 55), packet[55:], True)
        discarded_ack = self.resource(final_packet)
        self.check('fragment_retry', self.ack(discarded_ack, 1, 92, 37, True), 'final ACK')
        # This ACK is deliberately discarded at the RPC client; repeat its exact request.
        retried_ack = self.resource(final_packet)
        self.check('fragment_retry', retried_ack == discarded_ack, 'Pending retry changed ACK')
        self.check('fragment_retry', self.stats()['calls'] == 0, 'held Pending invoked callback')

        position, eof, snapshot = read_result(self.resource(read_packet()))
        header = rpc_header(snapshot)
        self.check('pending', position == 0 and eof and len(snapshot) == 32, 'snapshot shape')
        self.check('pending', header['transaction'] == 1 and header['state'] == 2
                   and header['result'] == 0 and header['received'] == header['request'] == 92,
                   'Pending header')
        self.check('pending', self.stats()['calls'] == 0, 'Pending poll dispatched')

        self.control(2, b'\0')
        position, eof, first_result = read_result(self.resource(read_packet()))
        header = rpc_header(first_result)
        self.check('ready', header['state'] == 4 and header['result'] == 200
                   and header['dispatch'] == header['endpoint_status'] == 0, 'Ready status')
        self.check('ready', position == cursor(1, 116) and not eof
                   and len(first_result) == 116, 'first bounded result read')
        last_position, last_eof, last_result = read_result(self.resource(read_packet(0, position)))
        record = first_result + last_result
        self.check('ready', last_position == cursor(1, 232) and last_eof and len(record) == 232,
                   'continuation result read')
        self.check('ready', record[32:] == struct.pack('<II', 7, 1) + bytes(range(192)),
                   'canonical Service result')
        repeated = read_result(self.resource(read_packet()))
        self.check('ready', repeated == (position, eof, first_result), 'immutable repeated result')
        self.check('ready', self.resource(final_packet) == discarded_ack, 'Ready WRITE retry ACK')
        after = self.stats()
        self.check('ready', after['calls'] == 1 and after['value'] == 7
                   and after['before_ack'] == 0, 'one callback after complete UART ACK')

        self.control(2, b'\1')
        held = request(2, 100)
        held_ack = self.resource(write_packet(0, held, True))
        self.check('generation', self.ack(held_ack, 2, 92, 92, True), 'held request admission')
        self.generation = 2
        self.control(3)
        after = self.stats()
        self.check('generation', after['generation'] == 2 and after['calls'] == 1
                   and after['value'] == 7, 'application session event did not cancel Pending')
        stale = self.exchange(read_packet(), generation=1)
        self.check('generation', stale == (127, b'\2'), 'stale generation accepted')
        foreign = self.exchange(read_packet(), peer=2)
        self.check('generation', foreign == (127, b'\1'), 'foreign peer accepted')
        resumed = request(1, 5)
        resumed_ack = self.resource(write_packet(0, resumed, True))
        self.check('generation', self.ack(resumed_ack, 1, 92, 92, True), 'fresh session ID restart')
        after = self.stats()
        self.check('generation', after['calls'] == 2 and after['value'] == 12,
                   'new admitted session did not execute once')

        partial = request(2, 100)
        partial_ack = self.resource(write_packet(0, partial[:29], False))
        self.check('timeout', self.ack(partial_ack, 2, 29, 29, False), 'Receiving admission')
        _, _, snapshot = read_result(self.resource(read_packet()))
        self.check('timeout', rpc_header(snapshot)['state'] == 1, 'Receiving state')
        # Real HAL ticks, rather than a fixture clock override, drive the 10 s deadline.
        time.sleep(10.2)
        _, _, snapshot = read_result(self.resource(read_packet()))
        header = rpc_header(snapshot)
        self.check('timeout', header['state'] == 0 and header['transaction'] == 2
                   and header['received'] == header['request'] == 0, '10 second timeout')
        expired = self.resource(write_packet(0, partial[:29], False))
        self.check('timeout', expired == struct.pack('<BQIB', 5, 0, 0, 0), 'expired ID reused')
        next_request = request(3, 1)
        self.resource(write_packet(0, next_request, True))
        after = self.stats()
        self.check('timeout', after['calls'] == 3 and after['value'] == 13,
                   'newer transaction after timeout')

        self.control(2, b'\1')
        command_packet = write_packet(0, request(4, operation=2), True)
        command_ack = self.resource(command_packet)  # deliberately discarded by RPC client
        self.check('command', self.ack(command_ack, 4, 24, 24, True), 'Command final ACK')
        self.check('command', self.resource(command_packet) == command_ack,
                   'Command Pending retry ACK')
        self.check('command', self.stats()['commands'] == 0, 'held Command executed')
        self.control(2, b'\0')
        position, eof, snapshot = read_result(self.resource(read_packet()))
        header = rpc_header(snapshot)
        self.check('command', position == cursor(4, 32) and eof and len(snapshot) == 32
                   and header['operation'] == 2 and header['state'] == 4
                   and header['result'] == 0 and header['dispatch'] == header['endpoint_status'] == 0,
                   'Command Ready status')
        self.check('command', self.resource(command_packet) == command_ack, 'Command Ready retry ACK')
        after = self.stats()
        self.check('command', after['commands'] == 1 and after['calls'] == 3
                   and after['value'] == 14 and after['before_ack'] == 0,
                   'Command retry executed more than once or before ACK')

        stat = self.resource(struct.pack('<BI', 2, 1))
        self.check('stats', stat == struct.pack('<BIB', 0, 44, 1), 'read-only stats capabilities')
        self.report['clock_hz'] = after['clock']
        self.check('stats', after['clock'] == 600000000, 'unexpected target clock')
        self.check('stats', after['before_ack'] == 0 and after['rejected'] == 2
                   and after['queue_drops'] == 0 and after['packets'] > 0 and after['acks'] > 0,
                   'owner ordering, admission or bounded queue counters')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cube', type=Path, required=True)
    parser.add_argument('--arm-cxx', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--programmer')
    parser.add_argument('--serial', help='explicit adapter serial, required with --run')
    parser.add_argument('--port', help='explicit UART port, required with --run')
    parser.add_argument('--run', action='store_true')
    args = parser.parse_args()
    if args.run and not all((args.programmer, args.serial, args.port)):
        parser.error('--run requires --programmer, --serial and --port')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    images = H7['build'](args, output, fixture_sources=[HERE / 'Probe.cpp', HERE / 'Benchmark.cpp',
        ROOT / 'lib/resource/protocol/Protocol.cpp', ROOT / 'lib/telemetry/model/Adapter.cpp',
        ROOT / 'lib/telemetry/abi/StructuredAbi.cpp'], fixture_inputs=[Path(__file__),
        HERE / 'verify.py', HERE / 'README.md', HERE.parent / 'run.py',
        ROOT / 'doc/RpcMailboxV1.md'])
    # Every image must be bounded and unchanged before importing device transport.
    for image in images:
        binary = Path(image['binary'])
        if (sha(image['elf']) != image['elf_sha256'] or sha(binary) != image['binary_sha256']
                or binary.stat().st_size != image['flash_bytes']
                or not 0 < image['flash_bytes'] <= FLASH_BYTES
                or image['flash_load_span'] != [0x08000000, 0x08000000 + image['flash_bytes']]):
            raise RuntimeError('Compiled image changed or exceeds the complete Flash backup')
    if not args.run:
        return
    # No serial package import or adapter access occurs in offline build mode.
    import serial
    from verify import verify
    provenance = json.loads((output / 'build-provenance.json').read_text(encoding='utf-8'))
    receipt = dict(started=datetime.now(timezone.utc).isoformat(), serial=args.serial, port=args.port,
        source_head=provenance['source_head'], source_dirty=provenance['source_dirty'],
        build_provenance=provenance, build_inputs=provenance['input_lf_sha256'],
        compiler=provenance['compiler'], images=images, runs={}, completed=False,
        restored_and_verified=False)
    connection = ['-c', 'port=SWD', 'sn=' + args.serial, 'mode=UR', 'reset=HWrst', 'freq=1000']

    def save():
        (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')

    def program(label, image, raw=False):
        result = H7['run']([args.programmer, *connection, '-w', image,
            *(['0x08000000'] if raw else []), '-v', '-rst'], output / (label + '.log'), 60)
        if 'Download verified successfully' not in result:
            raise RuntimeError('Programming verification missing: ' + label)

    backup = output / 'before.bin'
    info = H7['run']([args.programmer, *connection, '-u', '0x08000000', '0x10000', backup,
                      '-rst'], output / 'backup.log', 60)
    boards = [line.split(':', 1)[1].strip() for line in info.splitlines() if line.startswith('Board ')]
    serials = re.findall(r'^ST-LINK SN\s*:\s*(.+)$', info, re.M)
    if (boards != ['NUCLEO-H7S3L8'] or serials != [args.serial]
            or not re.search(r'Device ID\s*:\s*0x485\b', info)
            or not backup.is_file() or backup.stat().st_size != FLASH_BYTES):
        raise RuntimeError('Unexpected board or incomplete backup; no image programmed')
    receipt.update(board=boards[0], backup_bytes=backup.stat().st_size, backup_sha256=sha(backup))
    save()
    try:
        for image in images:
            opt = image['optimization']
            if sha(image['elf']) != image['elf_sha256'] or sha(image['binary']) != image['binary_sha256']:
                raise RuntimeError('Image changed after preflight')
            program('flash-' + opt, image['elf'])
            report = dict(checks=0, failures=0, scenarios={}, clock_hz=0, uart_frames=[])
            receipt['runs'][opt] = report
            with serial.Serial(args.port, 115200, timeout=0.1, write_timeout=2) as port:
                port.reset_input_buffer()
                with (output / (opt + '-uart.jsonl')).open('w', encoding='utf-8') as log:
                    UartProfile(port, report, log).run()
            save()
        receipt['completed'] = True
    except BaseException as error:
        receipt['error'] = str(error)
        raise
    finally:
        try:
            program('restore', backup, True)
            restored = output / 'restored.bin'
            H7['run']([args.programmer, *connection, '-u', '0x08000000', '0x10000', restored,
                        '-rst'], output / 'restore-verify.log', 60)
            receipt['restored_bytes'] = restored.stat().st_size
            receipt['restored_sha256'] = sha(restored)
            receipt['restored_and_verified'] = (receipt['restored_bytes'] == FLASH_BYTES
                and receipt['restored_sha256'] == receipt['backup_sha256'])
            if not receipt['restored_and_verified']:
                raise RuntimeError('Restored Flash differs from its complete backup')
        except BaseException as error:
            receipt['restore_error'] = str(error)
            raise
        finally:
            receipt['finished'] = datetime.now(timezone.utc).isoformat()
            save()
    verify(receipt)
    print('H7S RPC UART checks complete; original Flash restored and verified', flush=True)


if __name__ == '__main__':
    main()
