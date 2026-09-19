#!/usr/bin/env python3
"""Measure JSON stack writes on the authorized H7S board, restoring its image.

Author: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
Build-only unless --run is passed. Reuses the layout harness's Cube build.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import runpy
import time

HERE = Path(__file__).resolve().parent
COMMON = runpy.run_path(str(HERE.parent/'field_layout/h7s/run.py'))
CHECK = runpy.run_path(str(HERE/'verify.py'))
run, sha = COMMON['run'], COMMON['sha']


def measure(args, output, images):
    import serial
    with serial.Serial(args.port, 115200, timeout=0.2):
        pass
    connection = ['-c', 'port=SWD', 'sn='+args.serial, 'mode=UR', 'reset=HWrst', 'freq=4000']
    receipt = dict(started=datetime.now(timezone.utc).isoformat(), serial=args.serial, port=args.port,
                   completed=False, restored_and_verified=False, images=images, samples=[])
    receipt['compiler'] = (output/'common/compiler.log').read_text(encoding='utf-8').splitlines()[0]
    receipt['build_inputs'] = json.loads((output/'build-inputs.json').read_text(encoding='utf-8'))

    def save():
        (output/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n', encoding='utf-8')

    def program(tag, path, raw=False):
        text = run([args.programmer, *connection, '-w', path, *(['0x08000000'] if raw else []), '-v', '-rst'], output/(tag+'.log'), 60)
        if 'Download verified successfully' not in text:
            raise RuntimeError('Missing programming verification: '+tag)

    backup = output/'before.bin'
    run([args.programmer, *connection, '-u', '0x08000000', '0x10000', backup, '-rst'], output/'backup.log', 60)
    if backup.stat().st_size != 65536:
        raise RuntimeError('A complete 64 KiB backup is required')
    receipt['backup_sha256'] = sha(backup)
    save()
    try:
        for image in images:
            opt = image['optimization']
            if sha(image['elf']) != image['elf_sha256']:
                raise RuntimeError('Compiled image changed before programming')
            program('flash-'+opt, image['elf'])
            print('MEASURING JSON '+opt, flush=True)
            rows, ready, pending = [], False, None
            with serial.Serial(args.port, 115200, timeout=0.5, write_timeout=2) as port:
                time.sleep(0.3)
                port.reset_input_buffer()
                if port.write(b'R') != 1:
                    raise RuntimeError('Short command write')
                port.flush()
                start = time.monotonic()
                with (output/(opt+'-uart.log')).open('w', encoding='utf-8') as log:
                    while time.monotonic() - start < 120:
                        raw = port.readline()
                        if not raw:
                            continue
                        text = raw.decode('ascii').strip()
                        log.write(text+'\n'); log.flush()
                        words = text.split()
                        if words[:2] == ['STACK', 'READY']:
                            if ready or list(map(int, words[2:])) != [1, 2 if opt == 'O2' else 0, 600000000, 16384, 256, 21, 3]:
                                raise RuntimeError('Unexpected build/stack geometry: '+text)
                            ready = True
                            image['device'] = dict(clock_hz=600000000, stack_bytes=16384, guard_bytes=256)
                        elif words[:2] == ['STACK', 'T']:
                            if not ready or pending is not None or len(words) != 9:
                                raise RuntimeError('Unexpected measurement: '+text)
                            row = dict(zip(('case', 'sample', 'pattern', 'repetition', 'used', 'length', 'checksum'), map(int, words[2:])))
                            row.update(optimization=opt, elf_sha256=image['elf_sha256'], json='')
                            rows.append(row)
                            pending = row if row['length'] else None
                        elif text.startswith('STACK JSON ') and pending is not None:
                            pending['json'] = text[len('STACK JSON '):]
                            pending = None
                        elif text == 'STACK DONE' and ready and pending is None:
                            CHECK['verify_rows'](image, rows)
                            break
                        else:
                            raise RuntimeError('Unexpected board report: '+text)
                    else:
                        raise TimeoutError('Board did not finish JSON '+opt)
            image['windows'] = len(rows)
            receipt['samples'].extend(rows)
            save()
            print(f'PASS {opt}: {len(rows)} windows; largest observed stack write {max(r["used"] for r in rows)} bytes', flush=True)
        receipt['completed'] = True
    finally:
        try:
            program('restore', backup, True)
            restored = output/'after.bin'
            run([args.programmer, *connection, '-u', '0x08000000', '0x10000', restored, '-rst'], output/'readback.log', 60)
            receipt['restored_sha256'] = sha(restored)
            receipt['restored_and_verified'] = receipt['restored_sha256'] == receipt['backup_sha256']
            if not receipt['restored_and_verified']:
                raise RuntimeError('Restored firmware differs from backup')
            print('Original 64 KiB firmware restored and read-back verified.', flush=True)
        finally:
            save()
    CHECK['verify'](receipt)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cube', type=Path, required=True)
    parser.add_argument('--arm-cxx', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--run', action='store_true')
    parser.add_argument('--serial', default='002A001F3033510135393935')
    parser.add_argument('--port', default='COM6')
    parser.add_argument('--programmer', default='C:/ST/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe')
    args = parser.parse_args()
    args.variants, args.optimizations = ['Current'], ['O2', 'Os']
    output = args.output.resolve()
    if output.exists():
        parser.error('Use a fresh output directory')
    output.mkdir(parents=True)
    images = COMMON['build'](args, output,
        fixture_sources=[HERE/'JsonStack.cpp', HERE/'StackCall.S',
                         Path('lib/telemetry/abi/TelemetryAbi.cpp'),
                         Path('lib/telemetry/serialization/TelemetryJson.cpp')],
        linker_sections='  .json_probe_stack (NOLOAD) : { . = ALIGN(32); *(.json_probe_stack) . = ALIGN(32); } >DTCM\n',
        link_flags=['-Wl,-u,_printf_float'], fixture_inputs=[Path(__file__), HERE/'verify.py'])
    for image in images:
        directory = Path(image['elf']).parent
        image['stack_usage'] = {p.name: p.read_text() for p in directory.glob('*.su')}
    if args.run:
        measure(args, output, images)


if __name__ == '__main__':
    main()
