#!/usr/bin/env python3
"""Build Bind/Exchange, verify H7S results and restore the original Flash.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import runpy
import shutil
import subprocess
import time
from types import SimpleNamespace

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
H7 = runpy.run_path(str(ROOT / 'tests/field_layout/h7s/run.py'))


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cube', type=Path, required=True)
    parser.add_argument('--arm-cxx', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--programmer')
    parser.add_argument('--serial', default='002A001F3033510135393935')
    parser.add_argument('--port', default='COM6')
    parser.add_argument('--run', action='store_true')
    args = parser.parse_args()
    if args.run and not args.programmer:
        parser.error('--run requires --programmer')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    scaffold = output / 'scaffold'
    for relative in ('Boot/Core', 'Drivers'):
        shutil.copytree(args.cube / relative, scaffold / relative)
    shutil.copy2(args.cube / 'Boot/STM32H7S3L8HX_FLASH.ld', scaffold / 'Boot')

    def prepare(destination, variants):
        for variant in variants:
            shutil.copytree(ROOT / 'lib', destination / variant / 'lib')
            shutil.copytree(ROOT / 'examples/structured_protocol', destination / variant / 'examples/structured_protocol')

    H7['BASE']['prepare'] = prepare
    H7['BASE']['includes'] = lambda directory: ['-I' + str(directory / path) for path in
        ('lib', 'examples', 'lib/boost_pfr/include', 'lib/magic_enum')]
    build_args = SimpleNamespace(cube=scaffold, arm_cxx=args.arm_cxx, variants=['Current'], optimizations=['O2', 'Os'])
    images = H7['build'](build_args, output, cxx_standard='c++20', fixture_sources=[HERE / 'Probe.cpp', HERE / 'Benchmark.cpp'],
        fixture_inputs=[Path(__file__), HERE.parent / 'Fixture.hpp',
            *sorted(p for p in (ROOT / 'examples/structured_protocol').rglob('*')
                    if p.is_file() and p.suffix in ('.hpp', '.cpp', '.pri'))])
    if not args.run:
        return
    import serial
    from verify import verify
    with serial.Serial(args.port, 115200, timeout=0.2):
        pass
    connection = ['-c', 'port=SWD', 'sn=' + args.serial, 'mode=UR', 'reset=HWrst', 'freq=1000']
    receipt = dict(started=datetime.now(timezone.utc).isoformat(), serial=args.serial, port=args.port,
        source_head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        source_dirty=bool(subprocess.check_output(['git', 'status', '--porcelain', '--untracked-files=no'], cwd=ROOT, text=True).strip()),
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
    if boards != ['NUCLEO-H7S3L8'] or backup.stat().st_size != 65536:
        raise RuntimeError('Unexpected board or incomplete backup; no image programmed')
    receipt['board'] = boards[0]
    receipt['backup_sha256'] = sha(backup)
    save()
    try:
        for image in images:
            opt = image['optimization']
            if sha(image['elf']) != image['elf_sha256']:
                raise RuntimeError('Image changed after building')
            program('flash-' + opt, image['elf'])
            report = dict(ready=None, check=None, timing=[], done=False)
            receipt['runs'][opt] = report
            with serial.Serial(args.port, 115200, timeout=0.5, write_timeout=2) as port:
                port.reset_input_buffer()
                port.write(b'P'); port.flush()
                if port.readline().strip() != b'EXCHANGE IDLE 1':
                    raise RuntimeError('No idle acknowledgement')
                port.write(b'R'); port.flush()
                start = time.monotonic()
                with (output / (opt + '-uart.log')).open('w') as log:
                    while time.monotonic() - start < 60:
                        raw = port.readline()
                        if not raw:
                            continue
                        line = raw.decode('ascii').strip()
                        log.write(line + '\n'); log.flush()
                        words = line.split()
                        if words[:2] == ['EXCHANGE', 'READY'] and report['ready'] is None:
                            report['ready'] = list(map(int, words[2:]))
                        elif words[:2] == ['EXCHANGE', 'CHECK'] and report['check'] is None:
                            report['check'] = list(map(int, words[2:]))
                            if report['check'][0] != 0:
                                raise RuntimeError('MCU byte-equivalence check failed')
                        elif words[:2] == ['EXCHANGE', 'T']:
                            report['timing'].append(list(map(int, words[2:])))
                        elif words == ['EXCHANGE', 'DONE']:
                            report['done'] = True
                            break
                        else:
                            raise RuntimeError('Unexpected target report: ' + line)
                if not report['done']:
                    raise RuntimeError('MCU did not complete the measurements')
            save()
        receipt['completed'] = True
    finally:
        program('restore', backup, True)
        restored = output / 'restored.bin'
        H7['run']([args.programmer, *connection, '-u', '0x08000000', '0x10000', restored, '-rst'], output / 'restore-verify.log', 60)
        receipt['restored_sha256'] = sha(restored)
        receipt['restored_and_verified'] = receipt['restored_sha256'] == receipt['backup_sha256']
        receipt['finished'] = datetime.now(timezone.utc).isoformat()
        save()
        if not receipt['restored_and_verified']:
            raise RuntimeError('Restored Flash differs from backup')
    verify(receipt)
    print('H7S Bind/Exchange measurements complete; original Flash restored and verified', flush=True)


if __name__ == '__main__':
    main()
