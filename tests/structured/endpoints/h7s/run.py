#!/usr/bin/env python3
"""Build isolated entry layouts, measure on H7S, restore the original Flash.

Authors: Ruslan Kovtun (shpegun60), codexAi. MIT license.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import re
from pathlib import Path
import runpy
import shutil
import statistics
import subprocess
import time
from types import SimpleNamespace

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
H7 = runpy.run_path(str(ROOT/'tests/field_layout/h7s/run.py'))
ALIGNMENTS = {'Natural': None, 'Aligned8': 8, 'Aligned32': 32}
LOCAL_BUDGETS = {'Local'+str(size): size for size in (0, 16, 32, 64)}
ALIGNMENTS.update({name: None for name in LOCAL_BUDGETS})
DISPATCH_SNAPSHOTS = ('Before', 'Local', 'Pointer', 'Boundary')
DISPATCH_VARIANTS = (*DISPATCH_SNAPSHOTS, 'Context')
ALIGNMENTS.update({name: None for name in DISPATCH_VARIANTS})
LAYOUTS = {'Natural': [24, 4, 20, 4, 24, 4], 'Aligned8': [24, 8, 24, 8, 24, 8],
           'Aligned32': [32, 32, 32, 32, 32, 32]}
LAYOUTS.update({name: LAYOUTS['Natural'] for name in LOCAL_BUDGETS})
LAYOUTS.update({name: LAYOUTS['Natural'] for name in DISPATCH_VARIANTS})
DIRECT_LAYOUTS = {name: [28, 4, 20, 4, 24, 4] for name in ALIGNMENTS}
DIRECT_LAYOUTS['Aligned8'] = [32, 8, 24, 8, 24, 8]
DIRECT_LAYOUTS['Aligned32'] = [32, 32, 32, 32, 32, 32]
CHECKSUMS = [21, 1, 1, 38, 21, 1, 1, 17, 17, 1, 1, 1, 1,
             17, 4, 1, 17, 17, 17, 17, 17, 17, 1,
             21, 1, 1, 36, 1, 1, 56, 52, 1, 1, 72, 84, 1, 1, 104]


def operation_count(receipt):
    return 38 if receipt.get('storage_probes', False) else 23 if receipt.get('components', False) else 13 if receipt.get('legacy', False) else 4


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def verify(receipt):
    if not receipt['completed'] or not receipt['restored_and_verified']:
        raise ValueError('Run did not complete with a verified restore')
    if not receipt['images']:
        raise ValueError('No images were measured')
    keys = [image['variant']+'-'+image['optimization'] for image in receipt['images']]
    variants = {image['variant'] for image in receipt['images']}
    if not variants <= set(ALIGNMENTS) or len(set(keys)) != len(keys) or set(keys) != {
            variant+'-'+optimization for variant in variants for optimization in ('O2', 'Os')}:
        raise ValueError('Incomplete or duplicate image matrix')
    if set(receipt['runs']) != set(keys):
        raise ValueError('Image/run mismatch')
    if receipt['board'] != 'NUCLEO-H7S3L8' or receipt['backup_sha256'] != receipt['restored_sha256']:
        raise ValueError('Board identity or restored Flash differs')
    if receipt.get('components', False) and not receipt.get('legacy', False):
        raise ValueError('Component measurements require the legacy controls')
    if receipt.get('storage_probes', False) and not receipt.get('components', False):
        raise ValueError('Storage measurements require component controls')
    for image in receipt['images']:
        key = image['variant']+'-'+image['optimization']
        result = receipt['runs'][key]
        if receipt.get('storage_policy', False) and result.get('policy') != [image['local_bytes']]:
            raise ValueError('Runtime storage policy differs from the built configuration')
        if image['variant'] in LOCAL_BUDGETS and image.get('local_bytes') != LOCAL_BUDGETS[image['variant']]:
            raise ValueError('Storage policy differs from the selected variant')
        layouts = DIRECT_LAYOUTS if image.get('direct_contexts', False) else LAYOUTS
        if result['check'] != [0, 0] or result['ready'] != [600000000, 32768, *layouts[image['variant']], 16384]:
            raise ValueError('Wrong device/layout/correctness report: '+key)
        legacy = receipt.get('legacy', False)
        if legacy and result['legacy_layout'] != [96, 32, 20, 4]:
            raise ValueError('Legacy layout differs from the RW32 baseline')
        expected = {(m, p, o, r) for m in range(3) for p in range(3)
                    for o in range(operation_count(receipt)) for r in range(5)
                    if o < 7 or (m == 0 and p == 0)}
        for memory, profile, op, rep, cycles, checksum in result['timing']:
            item = (memory, profile, op, rep)
            if item not in expected or not 0 < cycles < 100000000 or checksum != CHECKSUMS[op]*16384:
                raise ValueError('Invalid/duplicate measurement: '+str(item))
            expected.remove(item)
        if expected or not result['done']:
            raise ValueError('Missing measurements: '+key)


def summary(receipt):
    values = []
    for key, result in receipt['runs'].items():
        variant, opt = key.split('-')
        for memory in range(3):
            for profile in range(3):
                for operation in range(operation_count(receipt)):
                    cycles = [row[4]/16384 for row in result['timing'] if row[:3] == [memory, profile, operation]]
                    if not cycles:
                        continue
                    values.append(dict(variant=variant, optimization=opt, memory=memory, profile=profile,
                                       operation=operation, median=statistics.median(cycles),
                                       minimum=min(cycles), maximum=max(cycles)))
    return values


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cube', type=Path, required=True)
    p.add_argument('--arm-cxx', required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--programmer')
    p.add_argument('--serial', default='002A001F3033510135393935')
    p.add_argument('--port', default='COM6')
    p.add_argument('--run', action='store_true')
    p.add_argument('--variants', nargs='+', choices=list(ALIGNMENTS), default=['Natural', 'Aligned8', 'Aligned32'])
    p.add_argument('--legacy', action='store_true', help='Add same-image scalar RW32 comparisons')
    p.add_argument('--components', action='store_true', help='Profile individual steps; requires --legacy')
    p.add_argument('--storage-probes', action='store_true', help='Add 16/32/64-byte structures and resolved entries; requires --components')
    p.add_argument('--dispatch-snapshots', type=Path,
                   help='Isolated Before/Local/Pointer/Boundary library roots; Context uses the current library')
    args = p.parse_args()
    if args.run and not args.programmer:
        p.error('--run requires --programmer')
    if args.components and not args.legacy:
        p.error('--components requires --legacy')
    if args.storage_probes and not args.components:
        p.error('--storage-probes requires --components')
    if any(v in DISPATCH_SNAPSHOTS for v in args.variants) and not args.dispatch_snapshots:
        p.error('Historical dispatch variants require --dispatch-snapshots')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    scaffold = output/'scaffold'
    for relative in ('Boot/Core', 'Drivers'):
        shutil.copytree(args.cube/relative, scaffold/relative)
    shutil.copy2(args.cube/'Boot/STM32H7S3L8HX_FLASH.ld', scaffold/'Boot')
    for license_file in args.cube.glob('LICENSE*'):
        shutil.copy2(license_file, scaffold)

    # Reuse only the existing Cube build/backup machinery. Candidate headers
    # are isolated copies; the production tree is never patched by this test.
    def prepare(destination, variants):
        for variant in variants:
            lib = destination/variant/'lib'
            source = (args.dispatch_snapshots/variant/'lib'
                      if variant in DISPATCH_SNAPSHOTS else ROOT/'lib')
            shutil.copytree(source, lib)
            alignment = ALIGNMENTS[variant]
            if alignment is not None:
                for family in ('Field', 'Command', 'Service'):
                    path = lib/'telemetry_structured'/family.lower()/(family+'Table.hpp')
                    text = path.read_text(encoding='utf-8')
                    old = 'struct '+family+'Entry {'
                    if text.count(old) != 1:
                        raise ValueError('Expected one natural entry declaration: '+str(path))
                    path.write_text(text.replace(old, f'struct alignas({alignment}) {family}Entry {{'), encoding='utf-8')
    H7['BASE']['prepare'] = prepare
    def binding_flags(directory):
        text = (directory/'lib/telemetry_structured/field/FieldTable.hpp').read_text()
        return [*(['-DENDPOINT_POINTER_THUNKS=1'] if 'readEncoded(std::span' in text else []),
                *(['-DENDPOINT_DIRECT_CONTEXTS=1'] if 'const void* readContext;' in text else [])]
    H7['BASE']['includes'] = lambda directory: [
        '-I'+str(directory/'lib'), '-I'+str(directory/'lib/boost_pfr/include'),
        '-I'+str(directory/'lib/magic_enum'), *(['-DENDPOINT_LEGACY=1'] if args.legacy else []),
        *(['-DENDPOINT_COMPONENTS=1'] if args.components else []),
        *(['-DENDPOINT_STORAGE=1'] if args.storage_probes else []),
        *binding_flags(directory),
        *(['-DTELEMETRY_STRUCTURED_LOCAL_BYTES='+str(LOCAL_BUDGETS[directory.name])]
          if directory.name in LOCAL_BUDGETS else [])]
    H7['BASE']['VARIANTS'].update({v: i for i, v in enumerate(ALIGNMENTS)})
    build_args = SimpleNamespace(cube=scaffold, arm_cxx=args.arm_cxx,
        variants=args.variants, optimizations=['O2', 'Os'])
    images = H7['build'](build_args, output, cxx_standard='c++20', fixture_sources=[
        HERE/'Probe.cpp', HERE/'Benchmark.cpp', HERE/'MixedCheck.cpp', HERE/'LargeCheck.cpp',
        *([HERE/'Components.cpp'] if args.components else []),
        *([HERE/'StorageProbes.cpp'] if args.storage_probes else []),
        Path('lib/telemetry_structured/model/Adapter.cpp'),
        Path('lib/telemetry_structured/abi/StructuredAbi.cpp'),
        *([Path('lib/telemetry/abi/TelemetryAbi.cpp')] if args.legacy else [])], fixture_inputs=[
        Path(__file__), HERE/'Fixture.hpp', HERE.parent/'MixedProbe.cpp',
        HERE.parent/'MixedFixture.hpp', HERE.parent/'LargeEndpoints.cpp'])
    for image in images:
        library = output/image['variant']/'lib/telemetry_structured'
        default_budget = int(re.search(r'#define TELEMETRY_STRUCTURED_LOCAL_BYTES (\d+)',
            (library/'codec/StoragePolicy.hpp').read_text()).group(1))
        image['local_bytes'] = LOCAL_BUDGETS.get(image['variant'], default_budget)
        image['direct_contexts'] = 'const void* readContext;' in (library/'field/FieldTable.hpp').read_text()
    if not args.run:
        return
    import serial
    with serial.Serial(args.port, 115200, timeout=0.2):
        pass
    connection = ['-c', 'port=SWD', 'sn='+args.serial, 'mode=UR', 'reset=HWrst', 'freq=1000']
    receipt = dict(started=datetime.now(timezone.utc).isoformat(), serial=args.serial, port=args.port,
        head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        images=images, legacy=args.legacy, components=args.components, storage_policy=True,
        storage_probes=args.storage_probes,
        runs={}, completed=False, restored_and_verified=False,
        compiler=(output/'common/compiler.log').read_text().splitlines()[0])
    def save():
        (output/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
    def program(label, image, raw=False):
        result = H7['run']([args.programmer, *connection, '-w', image,
            *(['0x08000000'] if raw else []), '-v', '-rst'], output/(label+'.log'), 60)
        if 'Download verified successfully' not in result:
            raise RuntimeError('No programming verification: '+label)
    backup = output/'before.bin'
    info = H7['run']([args.programmer, *connection, '-u', '0x08000000', '0x10000', backup, '-rst'],
                     output/'backup.log', 60)
    boards = [line.split(':', 1)[1].strip() for line in info.splitlines() if line.startswith('Board ')]
    if boards != ['NUCLEO-H7S3L8'] or backup.stat().st_size != 65536:
        raise RuntimeError('Unexpected board or incomplete backup; no programming attempted')
    receipt['board'] = boards[0]
    receipt['backup_sha256'] = sha(backup)
    save()
    try:
        for image in images:
            key = image['variant']+'-'+image['optimization']
            if sha(image['elf']) != image['elf_sha256']:
                raise RuntimeError('Image changed since build')
            program('flash-'+key, image['elf'])
            result = dict(check=None, ready=None, policy=None, legacy_layout=None, timing=[], done=False)
            receipt['runs'][key] = result
            with serial.Serial(args.port, 115200, timeout=0.5, write_timeout=2) as port:
                port.reset_input_buffer()
                port.write(b'P'); port.flush()
                if port.readline().strip() != b'STRUCT IDLE 1':
                    raise RuntimeError('Fixture did not acknowledge idle: '+key)
                port.write(b'R'); port.flush()
                start = time.monotonic()
                with (output/(key+'-uart.log')).open('w') as log:
                    while time.monotonic()-start < 90:
                        raw = port.readline()
                        if not raw:
                            continue
                        line = raw.decode('ascii').strip()
                        log.write(line+'\n'); log.flush()
                        words = line.split()
                        if words[:2] == ['STRUCT', 'CHECK'] and result['check'] is None:
                            result['check'] = list(map(int, words[2:]))
                            if result['check'] != [0, 0]:
                                raise RuntimeError('MCU correctness check failed: '+line)
                        elif words[:2] == ['STRUCT', 'READY'] and result['ready'] is None:
                            result['ready'] = list(map(int, words[2:]))
                        elif words[:2] == ['STRUCT', 'POLICY'] and result['policy'] is None:
                            result['policy'] = list(map(int, words[2:]))
                        elif words[:2] == ['STRUCT', 'T']:
                            result['timing'].append(list(map(int, words[2:])))
                        elif words[:2] == ['STRUCT', 'LEGACY'] and result['legacy_layout'] is None:
                            result['legacy_layout'] = list(map(int, words[2:]))
                        elif words == ['STRUCT', 'DONE']:
                            result['done'] = True
                            break
                        else:
                            raise RuntimeError('Unexpected target report: '+line)
                if not result['done']:
                    raise RuntimeError('Measurement timeout: '+key)
            save()
            print('MEASURED', key, flush=True)
        receipt['completed'] = True
    except BaseException as error:
        receipt['error'] = str(error)
        raise
    finally:
        try:
            program('restore', backup, raw=True)
            after = output/'after.bin'
            H7['run']([args.programmer, *connection, '-u', '0x08000000', '0x10000', after, '-rst'],
                      output/'verify-restore.log', 60)
            receipt['restored_sha256'] = sha(after)
            receipt['restored_and_verified'] = sha(after) == receipt['backup_sha256']
            if not receipt['restored_and_verified']:
                raise RuntimeError('Restored Flash differs')
        except BaseException as error:
            receipt['restore_error'] = str(error)
            raise
        finally:
            save()
    verify(receipt)
    (output/'summary.json').write_text(json.dumps(summary(receipt), indent=2)+'\n')
    print('Completed measurements, correctness checks and verified restoration', flush=True)


if __name__ == '__main__':
    main()
