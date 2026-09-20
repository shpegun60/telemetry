#!/usr/bin/env python3
"""Build, measure and restore the typed-command NUCLEO-H7S3L8 fixture."""
# Author: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
import argparse
import csv
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import runpy
import statistics
import subprocess
import time
from types import SimpleNamespace

import serial


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
H7 = runpy.run_path(str(ROOT / 'tests/field_layout/h7s/run.py'))
DEFAULT_CUBE = ROOT / 'build/field_layout_experiment/h7s/scaffold'
DEFAULT_COMPILER = Path(
    'C:/ST/STM32CubeIDE_2.0.0/STM32CubeIDE/plugins/'
    'com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740/'
    'tools/bin/arm-none-eabi-g++.exe')
DEFAULT_PROGRAMMER = Path(
    'C:/ST/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe')
OPERATIONS = ('known', 'runtime_native', 'erased_scalar')
ITERATIONS = 65536
REPETITIONS = 9


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(command, log, timeout=180):
    result = subprocess.run([str(x) for x in command], stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, encoding='utf-8', errors='replace', timeout=timeout)
    Path(log).write_text(result.stdout, encoding='utf-8')
    if result.returncode:
        raise RuntimeError(f'{log}: exit {result.returncode}\n{result.stdout[-6000:]}')
    return result.stdout


def require_clean_head():
    for command in (['git', 'diff', '--quiet'], ['git', 'diff', '--cached', '--quiet']):
        if subprocess.run(command, cwd=ROOT).returncode:
            raise RuntimeError('Commit the tracked tree before recording hardware evidence')
    return subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT,
                                   encoding='ascii').strip()


def summarize(rows):
    result = {}
    for optimization in ('O2', 'Os'):
        result[optimization] = {}
        for operation, name in enumerate(OPERATIONS):
            samples = [row['cycles'] / row['calls'] for row in rows
                       if row['optimization'] == optimization
                       and row['operation'] == operation]
            result[optimization][name] = {
                'median_cycles': statistics.median(samples),
                'min_cycles': min(samples),
                'max_cycles': max(samples),
            }
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cube', type=Path, default=DEFAULT_CUBE)
    parser.add_argument('--arm-cxx', type=Path,
                        default=Path(os.environ.get('ARM_CXX', DEFAULT_COMPILER)))
    parser.add_argument('--programmer', type=Path, default=DEFAULT_PROGRAMMER)
    parser.add_argument('--serial', default='002A001F3033510135393935')
    parser.add_argument('--port', default='COM6')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists():
        raise RuntimeError(f'Output already exists: {output}')
    output.mkdir(parents=True)
    head = require_clean_head()

    build_args = SimpleNamespace(cube=args.cube, arm_cxx=str(args.arm_cxx),
        output=output, variants=['Current'], baseline_ref=None,
        optimizations=['O2', 'Os'])
    source = HERE / 'CommandDispatchBenchmark.cpp'
    images = H7['build'](build_args, output, fixture_sources=[source],
                         fixture_inputs=[Path(__file__)])
    image_paths = {image['optimization']: Path(image['elf']) for image in images}
    for image in images:
        image['elf'] = str(Path(image['elf']).relative_to(output)).replace('\\', '/')

    connection = ['-c', 'port=SWD', 'sn=' + args.serial, 'mode=UR',
                  'reset=HWrst', 'freq=4000']
    receipt = dict(
        started=datetime.now(timezone.utc).isoformat(), source_commit=head,
        compiler=(output / 'common/compiler.log').read_text(encoding='utf-8').splitlines()[0],
        board='NUCLEO-H7S3L8', serial=args.serial, port=args.port,
        fixture_sha256=sha(source), runner_sha256=sha(Path(__file__)),
        completed=False, restored_and_verified=False, images=images)
    rows = []

    def save():
        (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n',
                                             encoding='utf-8')

    def program(tag, path, raw=False):
        command = [args.programmer, *connection, '-w', path]
        if raw:
            command.append('0x08000000')
        command += ['-v', '-rst']
        text = run(command, output / (tag + '.log'), 60)
        if 'Download verified successfully' not in text:
            raise RuntimeError('Missing programming verification: ' + tag)

    backup = output / 'before.bin'
    run([args.programmer, *connection, '-u', '0x08000000', '0x10000', backup, '-rst'],
        output / 'backup.log', 60)
    if backup.stat().st_size != 65536:
        raise RuntimeError('Incomplete 64 KiB backup')
    receipt['backup_sha256'] = sha(backup)
    save()
    try:
        for image in images:
            tag = image['optimization']
            elf = image_paths[tag]
            if sha(elf) != image['elf_sha256']:
                raise RuntimeError('Image changed before programming')
            program('flash-' + tag, elf)
            print('MEASURING', tag, flush=True)
            with serial.Serial(args.port, 115200, timeout=0.5, write_timeout=2) as port:
                time.sleep(0.3)
                port.reset_input_buffer()
                if port.write(b'R') != 1:
                    raise RuntimeError('Short UART write')
                port.flush()
                expected = {(operation, repetition)
                    for operation in range(len(OPERATIONS))
                    for repetition in range(REPETITIONS)}
                observed = set()
                ready = None
                started = time.monotonic()
                with (output / (tag + '-uart.log')).open('w', encoding='utf-8') as log:
                    while time.monotonic() - started < 60:
                        raw = port.readline()
                        if not raw:
                            continue
                        line = raw.decode('ascii').strip()
                        log.write(line + '\n')
                        log.flush()
                        words = line.split()
                        if words[:2] == ['DISPATCH', 'READY']:
                            if ready is not None or len(words) != 9:
                                raise RuntimeError('Unexpected READY: ' + line)
                            ready = list(map(int, words[2:]))
                            version, optimization, clock, iterations, reps, scalar_size, command_size = ready
                            if (version, optimization, clock, iterations, reps,
                                scalar_size, command_size) != (
                                    1, 2 if tag == 'O2' else 0, 600000000,
                                    ITERATIONS, REPETITIONS, 16, 20):
                                raise RuntimeError('Device identity mismatch: ' + line)
                        elif words[:2] == ['DISPATCH', 'T']:
                            if ready is None or len(words) != 6:
                                raise RuntimeError('Timing before READY: ' + line)
                            operation, repetition, cycles, checksum = map(int, words[2:])
                            key = (operation, repetition)
                            if (key not in expected or key in observed
                                    or not 0 < cycles < ITERATIONS * 1000
                                    or checksum != ITERATIONS):
                                raise RuntimeError('Invalid timing row: ' + line)
                            observed.add(key)
                            rows.append(dict(optimization=tag, operation=operation,
                                repetition=repetition, cycles=cycles, calls=ITERATIONS,
                                checksum=checksum, elf_sha256=image['elf_sha256']))
                        elif line == 'DISPATCH DONE':
                            if observed != expected:
                                raise RuntimeError('Missing timing windows')
                            break
                        else:
                            raise RuntimeError('Unexpected board output: ' + line)
                    else:
                        raise TimeoutError('Board timeout: ' + tag)
            image['windows'] = len(observed)
            save()
            print('PASS', tag, len(observed), 'windows', flush=True)
        receipt['summary'] = summarize(rows)
        receipt['completed'] = True
    finally:
        try:
            program('restore', backup, True)
            restored = output / 'after.bin'
            run([args.programmer, *connection, '-u', '0x08000000', '0x10000', restored, '-rst'],
                output / 'readback.log', 60)
            receipt['restored_sha256'] = sha(restored)
            receipt['restored_and_verified'] = (
                receipt['restored_sha256'] == receipt['backup_sha256'])
            if not receipt['restored_and_verified']:
                raise RuntimeError('Restored firmware differs from backup')
            print('RESTORED', receipt['restored_sha256'], flush=True)
        finally:
            receipt['finished'] = datetime.now(timezone.utc).isoformat()
            save()

    with (output / 'samples.csv').open('w', newline='', encoding='utf-8') as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)
    print(json.dumps(receipt['summary'], indent=2))


if __name__ == '__main__':
    main()
