#!/usr/bin/env python3
"""Build both resource revisions, measure DWT/PSP stack, restore verified Flash."""
# Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import runpy
import subprocess
import time
from types import SimpleNamespace
from verify import measurements, verify

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
H7 = runpy.run_path(str(ROOT / 'tests/field_layout/h7s/run.py'))


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def input_label(path):
    path = Path(path)
    return (path.relative_to(ROOT) if path.is_relative_to(ROOT) else path).as_posix()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--cube', type=Path, default=ROOT / 'build/field_layout_experiment/h7s/scaffold')
    p.add_argument('--arm-cxx', required=True)
    p.add_argument('--programmer', default='C:/ST/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe')
    p.add_argument('--serial', default='002A001F3033510135393935')
    p.add_argument('--port', default='COM6')
    p.add_argument('--baseline', default='f1cfbd891ef2f0c23f0fb2dae70be8ed4bd6fcaf')
    p.add_argument('--run', action='store_true')
    args = p.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    build_args = SimpleNamespace(cube=args.cube, arm_cxx=args.arm_cxx,
        variants=['Baseline', 'Current'], baseline_ref=args.baseline, optimizations=['O2', 'Os'])
    sources = [HERE/'ResourceBenchmark.cpp', ROOT/'tests/json_stack/StackCall.S',
        Path('lib/telemetry/abi/TelemetryAbi.cpp'),
        *[Path('lib/resource/telemetry')/(name+'.cpp') for name in ('SchemaFile', 'CommandsFile', 'ValuesFile')]]
    images = H7['build'](build_args, output, fixture_sources=sources, cxx_standard='c++20',
        fixture_inputs=[Path(__file__), HERE / "verify.py"], linker_sections=
        '  .resource_probe_stack (NOLOAD) : { . = ALIGN(32); *(.resource_probe_stack) . = ALIGN(32); } >DTCM\n')
    if not args.run:
        return
    import serial
    with serial.Serial(args.port, 115200, timeout=0.2):
        pass
    connection = ['-c', 'port=SWD', 'sn='+args.serial, 'mode=UR', 'reset=HWrst', 'freq=1000']
    receipt = {'started': datetime.now(timezone.utc).isoformat(), 'serial': args.serial,
        'port': args.port, 'head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        'dirty': bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True).strip()),
        'images': images, 'runs': {}, 'completed': False, 'restored_and_verified': False,
        'compiler': (output/'common/compiler.log').read_text().splitlines()[0],
        'build_inputs': {input_label(k): v
            for k, v in json.loads((output/'build-inputs.json').read_text()).items()}}
    def save():
        (output/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
    def program(label, path, raw=False):
        result = H7['run']([args.programmer, *connection, '-w', path,
            *(['0x08000000'] if raw else []), '-v', '-rst'], output/(label+'.log'), 60)
        if 'Download verified successfully' not in result:
            raise RuntimeError('Missing programming verification: '+label)
    backup = output/'before.bin'
    info = H7['run']([args.programmer, *connection, '-u', '0x08000000', '0x10000', backup, '-rst'], output/'backup.log', 60)
    boards = [line.split(':', 1)[1].strip() for line in info.splitlines() if line.startswith('Board ')]
    if boards != ['NUCLEO-H7S3L8']:
        raise RuntimeError('Unexpected target board; no programming attempted')
    receipt['board'] = boards[0]
    if backup.stat().st_size != 65536:
        raise RuntimeError('Incomplete backup; no programming attempted')
    receipt['backup_sha256'] = sha(backup)
    save()
    try:
        for image in images:
            key = image['variant']+'-'+image['optimization']
            if sha(image['elf']) != image['elf_sha256']:
                raise RuntimeError('Image changed after build')
            program('flash-'+key, image['elf'])
            data = {'ready': None, 'timing': [], 'stacks': [], 'files': [], 'idle': False, 'done': False}
            receipt['runs'][key] = data
            with serial.Serial(args.port, 115200, timeout=0.5, write_timeout=2) as port:
                time.sleep(0.3)
                port.reset_input_buffer()
                if port.write(b'P') != 1:
                    raise RuntimeError('Short idle command')
                port.flush()
                if port.readline().strip() != b'RESOURCE IDLE 2':
                    raise RuntimeError('Target did not reach idle: '+key)
                data['idle'] = True
                if port.write(b'R') != 1:
                    raise RuntimeError('Short UART command')
                port.flush()
                start, done = time.monotonic(), False
                with (output/(key+'-uart.log')).open('w') as log:
                    while time.monotonic() - start < 90:
                        raw = port.readline()
                        if not raw:
                            continue
                        line = raw.decode('ascii').strip()
                        log.write(line+'\n')
                        log.flush()
                        words = line.split()
                        if words[:2] == ['RESOURCE', 'READY']:
                            if data['ready'] is not None:
                                raise RuntimeError('Unexpected target restart')
                            data['ready'] = list(map(int, words[2:]))
                        elif words[:2] == ['RESOURCE', 'F']:
                            data['files'].append(list(map(int, words[2:])))
                        elif words[:2] == ['RESOURCE', 'T']:
                            data['timing'].append(list(map(int, words[2:])))
                        elif words[:2] == ['RESOURCE', 'S']:
                            data['stacks'].append(list(map(int, words[2:])))
                        elif words == ['RESOURCE', 'DONE']:
                            done = data['done'] = True
                            break
                        else:
                            raise RuntimeError('Unexpected target report: '+line)
                if not done:
                    raise RuntimeError('Target report timeout: '+key)
            save()
            print('MEASURED', key, flush=True)
        receipt['summary'] = measurements(receipt)
        receipt['completed'] = True
    except BaseException as error:
        receipt['run_error'] = str(error)
        raise
    finally:
        try:
            program('restore', backup, raw=True)
            after = output/'after.bin'
            H7['run']([args.programmer, *connection, '-u', '0x08000000', '0x10000', after, '-rst'], output/'verify-restore.log', 60)
            receipt['restored_sha256'] = sha(after)
            receipt['restored_and_verified'] = sha(after) == receipt['backup_sha256']
            if not receipt['restored_and_verified']:
                raise RuntimeError('Restored Flash differs from backup')
        except BaseException as error:
            receipt['restore_error'] = str(error)
            raise
        finally:
            save()
    verify(receipt)
    print(json.dumps(receipt['summary'], indent=2))


if __name__ == '__main__':
    main()
