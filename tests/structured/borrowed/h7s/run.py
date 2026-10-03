#!/usr/bin/env python3
"""Build bounded borrowed H7S images; selected hardware access requires --run.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
Build mode imports no serial package and invokes no programmer.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import runpy
import statistics
import subprocess
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
SUPPORT = runpy.run_path(str(ROOT / 'tests/h7s_support/build.py'))
CHECK = runpy.run_path(str(HERE / 'verify.py'))
run, sha, write_json = (SUPPORT[key] for key in ('run', 'sha', 'write_json'))

def build(args, output):
    source_status = subprocess.check_output(['git', 'status', '--porcelain', '-uall'], cwd=ROOT, text=True)
    images = SUPPORT['build'](args, output, fixture_sources=[HERE / 'Benchmark.cpp', HERE / 'Probe.cpp',
        ROOT / 'lib/telemetry/abi/StructuredAbi.cpp', ROOT / 'tests/structured/mcu/h7s/StackCall.S'],
        fixture_inputs=[HERE / 'Fixture.hpp', HERE / 'run.py', HERE / 'verify.py'])
    compiler = Path(args.arm_cxx).resolve()
    nm = compiler.with_name('arm-none-eabi-nm' + compiler.suffix)
    for image in images:
        directory = Path(image['elf']).parent
        image['sections_sha256'] = sha(directory / 'sections.log')
        image['ram_spans'] = CHECK['ram_sections']((directory / 'sections.log').read_text())
        symbols = run([nm, '-S', '-C', image['elf']], directory / 'symbols.log')
        if re.search(r'(?:nativeOwnField|nativeOwnService)<65536[^>]*>', symbols):
            raise RuntimeError('An owning native 64 KiB call was instantiated')
        image['no_owning_native_64k'] = True
        for key in ('elf', 'binary'):
            image[key] = Path(image[key]).relative_to(output).as_posix()
    provenance = json.loads((output / 'build-provenance.json').read_text())
    receipt = dict(format_version=1, scope=CHECK['SCOPE'], **provenance,
        source_status=source_status,
        captured_at_utc=datetime.now(timezone.utc).isoformat(), completed=False,
        restored_and_verified=False, execution='compile/link only', images=images)
    if receipt['source_dirty'] != bool(source_status.strip()):
        raise RuntimeError('Dirty-state classification changed during capture')
    CHECK['verify_build'](receipt, current=True, artifacts=output)
    write_json(output / 'images.json', images)
    write_json(output / 'receipt.json', receipt)
    write_json(output / 'parser-controls.json', dict(uart=CHECK['self_test'](),
        artifacts=CHECK['artifact_self_test'](receipt, output)))
    print('BUILD PREFLIGHT PASSED: internal RAM/64 KiB Flash; no device accessed', flush=True)
    return receipt

def summaries(receipt):
    result = []
    for image in receipt['images']:
        measurement = image['measurement']
        for operation in measurement['operations']:
            for profile, label in enumerate(('same', 'alternating', 'shuffled')):
                rows = [row for row in measurement['timing'] if row[:2] == [operation['index'], profile]]
                stack = [row[4] for row in measurement['stack'] if row[:2] == [operation['index'], profile]]
                cycles = [row[3] / row[4] for row in rows]
                result.append(dict(optimization=image['optimization'], operation=operation['name'],
                    payload_bytes=operation['bytes'], profile=label, calls_per_window=operation['iterations'],
                    timing_windows=len(rows), median_dwt_cycles_per_call=statistics.median(cycles),
                    minimum_dwt_cycles_per_call=min(cycles), maximum_dwt_cycles_per_call=max(cycles),
                    maximum_observed_psp_bytes=max(stack), scope='full probe plus full-payload FNV consumer; no baseline subtraction'))
    return result

def measure(args, output, receipt):
    """The sole device path; all build/source/RAM/Flash gates run first."""
    CHECK['verify_build'](receipt, current=True, artifacts=output)
    import serial
    receipt.update(execution='device', serial=args.serial, port=args.port,
                   started_at_utc=datetime.now(timezone.utc).isoformat())
    def save():
        write_json(output / 'receipt.json', receipt)
    connection = ['-c', 'port=SWD', 'sn=' + args.serial, 'mode=UR', 'reset=HWrst', 'freq=4000']
    def program(label, file, raw=False):
        info = run([args.programmer, *connection, '-w', file, *(['0x08000000'] if raw else []), '-v', '-rst'],
                   output / (label + '.log'), 60)
        if 'Download verified successfully' not in info:
            raise RuntimeError('Programming verification missing: ' + label)
    backup = output / 'before.bin'
    info = run([args.programmer, *connection, '-u', '0x08000000', '0x10000', backup, '-rst'], output / 'backup.log', 60)
    boards = re.findall(r'^Board\s*:\s*(.+)$', info, re.M)
    serials = re.findall(r'^ST-LINK SN\s*:\s*(.+)$', info, re.M)
    if boards != [CHECK['BOARD']] or serials != [args.serial] or not re.search(r'Device ID\s*:\s*0x485\b', info):
        raise RuntimeError('Selected board/serial/device ID differs; no image programming attempted')
    if not backup.is_file() or backup.stat().st_size != 65536:
        raise RuntimeError('A complete 64 KiB backup is required')
    receipt.update(board=boards[0], backup_bytes=65536, backup_sha256=sha(backup)); save()
    try:
        for image in receipt['images']:
            opt = image['optimization']
            CHECK['verify_build'](receipt, current=True, artifacts=output)
            program('flash-' + opt, output / image['elf'])
            image['uart_lines'] = []
            with serial.Serial(args.port, 115200, timeout=0.5, write_timeout=2) as port:
                time.sleep(0.3); port.reset_input_buffer()
                if port.write(b'R') != 1:
                    raise RuntimeError('Short UART command write')
                port.flush()
                start = time.monotonic()
                with (output / (opt + '-uart.log')).open('w', encoding='utf-8') as log:
                    while time.monotonic() - start < 180:
                        raw = port.readline()
                        if not raw:
                            continue
                        text = raw.decode('ascii').strip()
                        log.write(text + '\n'); log.flush()
                        image['uart_lines'].append(text)
                        if text == 'BR DONE':
                            break
                        if text.startswith('BR FAIL'):
                            raise RuntimeError('Fixture refused measurement: ' + text)
                    else:
                        raise TimeoutError('Measurement did not finish: ' + opt)
            image['measurement'] = CHECK['verify_image'](image)
            save(); print('MEASURED ' + opt, flush=True)
        receipt['completed'] = True
    except BaseException as error:
        receipt['error'] = str(error); raise
    finally:
        try:
            program('restore', backup, raw=True)
            after = output / 'after.bin'
            run([args.programmer, *connection, '-u', '0x08000000', '0x10000', after, '-rst'], output / 'readback.log', 60)
            receipt.update(restored_bytes=after.stat().st_size, restored_sha256=sha(after))
            receipt['restored_and_verified'] = receipt['restored_bytes'] == 65536 and receipt['restored_sha256'] == receipt['backup_sha256']
            if not receipt['restored_and_verified']:
                raise RuntimeError('Restored image differs from the complete internal Flash backup')
            print('Restored 64 KiB image read-back verified and left running.', flush=True)
        except BaseException as error:
            receipt['restore_error'] = str(error); raise
        finally:
            receipt['finished_at_utc'] = datetime.now(timezone.utc).isoformat(); save()
    CHECK['verify'](receipt, current=True, artifacts=output)
    write_json(output / 'parser-controls.json', dict(uart=CHECK['self_test'](receipt['images']),
        artifacts=CHECK['artifact_self_test'](receipt, output)))
    write_json(output / 'summary.json', summaries(receipt))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cube', required=True, type=Path, help='copied 600 MHz Cube scaffold with bench hooks')
    parser.add_argument('--arm-cxx', required=True, help='CubeIDE ARM14.3.1 compiler executable')
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--run', action='store_true')
    parser.add_argument('--serial'); parser.add_argument('--port'); parser.add_argument('--programmer')
    args = parser.parse_args()
    if args.run and (args.serial != CHECK['SERIAL'] or args.port != CHECK['PORT'] or not args.programmer):
        parser.error('--run requires --serial ' + CHECK['SERIAL'] + ' --port ' + CHECK['PORT'] + ' --programmer PATH')
    output = args.output.resolve()
    if output.exists():
        parser.error('Use a fresh output directory to preserve existing evidence')
    output.mkdir(parents=True)
    receipt = build(args, output)
    if args.run:
        measure(args, output, receipt)

if __name__ == '__main__':
    main()
