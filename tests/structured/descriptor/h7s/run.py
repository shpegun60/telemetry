#!/usr/bin/env python3
"""Build both descriptor controls, measure H7S DWT, restore verified Flash.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import runpy
import re
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
H7 = runpy.run_path(str(ROOT / 'tests/h7s_support/build.py'))


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cube', type=Path, required=True)
    parser.add_argument('--arm-cxx', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--programmer')
    parser.add_argument('--serial', help='explicit adapter serial, required with --run')
    parser.add_argument('--port', help='explicit serial port, required with --run')
    parser.add_argument('--run', action='store_true')
    args = parser.parse_args()
    if args.run and not all((args.programmer, args.serial, args.port)):
        parser.error('--run requires --programmer, --serial and --port')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    images = H7['build'](args, output, fixture_sources=[HERE / 'Probe.cpp', HERE / 'Benchmark.cpp'],
        fixture_inputs=[Path(__file__), HERE / 'verify.py', HERE.parent / 'Fixture.hpp', HERE.parent.parent / 'endpoints/MixedFixture.hpp'])
    if not args.run:
        return
    # Validate the complete matrix before any device access.
    for image in images:
        if sha(image['elf']) != image['elf_sha256'] or sha(image['binary']) != image['binary_sha256'] or not 0 < Path(image['binary']).stat().st_size <= 65536:
            raise RuntimeError('Compiled image changed after building')
    provenance = json.loads((output / 'build-provenance.json').read_text(encoding='utf-8'))
    import serial
    from verify import verify
    with serial.Serial(args.port, 115200, timeout=0.2):
        pass
    connection = ['-c', 'port=SWD', 'sn=' + args.serial, 'mode=UR', 'reset=HWrst', 'freq=1000']
    receipt = dict(started=datetime.now(timezone.utc).isoformat(), serial=args.serial, port=args.port,
        source_head=provenance['source_head'], source_dirty=provenance['source_dirty'],
        build_provenance=provenance,
        images=images, build_inputs=json.loads((output / 'build-inputs.json').read_text()),
        compiler=(output / 'common/compiler.log').read_text().splitlines()[0],
        runs={}, completed=False, restored_and_verified=False)

    def save():
        (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')

    def program(label, image, raw=False):
        text = H7['run']([args.programmer, *connection, '-w', image,
             *(['0x08000000'] if raw else []), '-v', '-rst'], output / (label + '.log'), 60)
        if 'Download verified successfully' not in text:
            raise RuntimeError('Programming verification missing: ' + label)

    backup = output / 'before.bin'
    info = H7['run']([args.programmer, *connection, '-u', '0x08000000', '0x10000', backup, '-rst'], output / 'backup.log', 60)
    boards = [line.split(':', 1)[1].strip() for line in info.splitlines() if line.startswith('Board ')]
    serials = re.findall(r'^ST-LINK SN\s*:\s*(.+)$', info, re.M)
    if boards != ['NUCLEO-H7S3L8'] or serials != [args.serial] or not re.search(r'Device ID\s*:\s*0x485\b', info) or not backup.is_file() or backup.stat().st_size != 65536:
        raise RuntimeError('Unexpected board or incomplete backup; no image programmed')
    receipt['board'] = boards[0]
    receipt['backup_sha256'] = sha(backup)
    receipt['backup_bytes'] = backup.stat().st_size
    save()
    try:
        for image in images:
            opt = image['optimization']
            if sha(image['elf']) != image['elf_sha256'] or sha(image['binary']) != image['binary_sha256'] or Path(image['binary']).stat().st_size != image['flash_bytes']:
                raise RuntimeError('Image changed after building')
            program('flash-' + opt, image['elf'])
            report = dict(ready=None, check=None, timing=[], uart_lines=[], done=False)
            receipt['runs'][opt] = report
            with serial.Serial(args.port, 115200, timeout=0.5, write_timeout=2) as port:
                port.reset_input_buffer()
                port.write(b'P'); port.flush()
                if port.readline().strip() != b'DESC IDLE 1':
                    raise RuntimeError('No idle acknowledgement')
                port.write(b'R'); port.flush()
                start = time.monotonic()
                with (output / (opt + '-uart.log')).open('w') as log:
                    while time.monotonic() - start < 60:
                        raw = port.readline()
                        if not raw:
                            continue
                        line = raw.decode('ascii').strip()
                        report['uart_lines'].append(line)
                        log.write(line + '\n'); log.flush()
                        words = line.split()
                        if words[:2] == ['DESC', 'READY'] and report['ready'] is None:
                            report['ready'] = list(map(int, words[2:]))
                        elif words[:2] == ['DESC', 'CHECK'] and report['check'] is None:
                            report['check'] = int(words[2])
                            if report['check'] != 0:
                                raise RuntimeError('MCU byte-equivalence check failed')
                        elif words[:2] == ['DESC', 'T']:
                            report['timing'].append(list(map(int, words[2:])))
                        elif words == ['DESC', 'DONE']:
                            report['done'] = True
                            break
                        else:
                            raise RuntimeError('Unexpected target report: ' + line)
                if not report['done']:
                    raise RuntimeError('MCU did not complete the measurements')
            save()
        receipt['completed'] = True
    except BaseException as error:
        receipt['error'] = str(error)
        raise
    finally:
        try:
            program('restore', backup, True)
            restored = output / 'restored.bin'
            H7['run']([args.programmer, *connection, '-u', '0x08000000', '0x10000', restored, '-rst'], output / 'restore-verify.log', 60)
            receipt['restored_bytes'] = restored.stat().st_size
            receipt['restored_sha256'] = sha(restored)
            receipt['restored_and_verified'] = receipt['restored_bytes'] == 65536 and receipt['restored_sha256'] == receipt['backup_sha256']
            if not receipt['restored_and_verified']:
                raise RuntimeError('Restored Flash differs from its complete backup')
        except BaseException as error:
            receipt['restore_error'] = str(error)
            raise
        finally:
            receipt['finished'] = datetime.now(timezone.utc).isoformat()
            save()
    verify(receipt)
    print('H7S descriptor measurements complete; original Flash restored and verified', flush=True)


if __name__ == '__main__':
    main()
