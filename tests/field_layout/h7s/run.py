#!/usr/bin/env python3
"""Build, measure and restore the selected NUCLEO-H7S3L8 telemetry fixture.

Author: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
Requires a copied Cube scaffold; never edits the original COBS project.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import runpy
import shutil
import statistics
import struct
import subprocess
import time

HERE = Path(__file__).resolve().parent
EXP = HERE.parent
BASE = runpy.run_path(str(EXP / 'run.py'))
VARIANTS = ['A', 'B', 'B32', 'C', 'Current']
LAYOUTS = {'A': (80, 8, 56, 68, 16), 'B': (80, 8, 0, 12, 24),
           'B32': (96, 32, 0, 12, 24), 'C': (96, 32, 0, 12, 40), 'Current': (96, 32, 0, 32, 40)}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(command, log, timeout=180):
    result = subprocess.run([str(x) for x in command], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            encoding='utf-8', errors='replace', timeout=timeout)
    log.write_text(result.stdout, encoding='utf-8')
    if result.returncode:
        raise RuntimeError(f'{log}: exit {result.returncode}\n{result.stdout[-6000:]}')
    return result.stdout


def build(args, output, *, fixture_sources=None, linker_sections='', link_flags=(), fixture_inputs=()):
    cube = args.cube.resolve()
    compiler = Path(args.arm_cxx).resolve()
    gcc = compiler.with_name('arm-none-eabi-gcc.exe')
    objcopy = compiler.with_name('arm-none-eabi-objcopy.exe')
    objdump = compiler.with_name('arm-none-eabi-objdump.exe')
    size = compiler.with_name('arm-none-eabi-size.exe')
    BASE['prepare'](output, args.variants)
    sources_for_fixture = fixture_sources or [EXP/'Probe.cpp', HERE/'Benchmark.cpp']
    inputs = [EXP/'Fixture.h', Path(__file__), EXP/'run.py', *fixture_inputs]
    inputs += [p for p in sources_for_fixture if p.is_absolute()]
    inputs += [p for p in cube.rglob('*') if p.is_file()]
    manifest = {str(p): sha(p) for p in inputs}
    flags = BASE['ARM'] + ['-ffunction-sections', '-fdata-sections', '-DUSE_HAL_DRIVER', '-DSTM32H7S3xx', '--specs=nano.specs']
    include = ['-I'+str(cube/'Boot/Core/Inc')]
    for path in ('Drivers/STM32H7RSxx_HAL_Driver/Inc', 'Drivers/STM32H7RSxx_HAL_Driver/Inc/Legacy',
                 'Drivers/CMSIS/Device/ST/STM32H7RSxx/Include', 'Drivers/CMSIS/Include'):
        include += ['-isystem', str(cube/path)]
    common = output/'common'
    common.mkdir()
    run([compiler, '--version'], common/'compiler.log')
    cobjects = []
    sources = list((cube/'Drivers/STM32H7RSxx_HAL_Driver/Src').glob('*.c')) + list((cube/'Boot/Core/Src').glob('*.c'))
    for source in sources:
        obj = common/(source.stem+'.o')
        run([gcc, *flags, *include, '-Os', '-std=gnu11', '-Wall', '-c', source, '-o', obj], common/(source.stem+'.log'))
        cobjects.append(obj)
    startup = common/'startup.o'
    run([gcc, *BASE['ARM'], '-x', 'assembler-with-cpp', '-c', cube/'Boot/Core/Startup/startup_stm32h7s3l8hx.s', '-o', startup], common/'startup.log')
    original_ld = (cube/'Boot/STM32H7S3L8HX_FLASH.ld').read_text()
    linker = common/'benchmark.ld'
    linker.write_text(BASE['replace_once'](original_ld, '  ._user_heap_stack :',
        '  .dtcm_ids (NOLOAD) : { . = ALIGN(32); *(.dtcm_ids) . = ALIGN(32); } >DTCM\n'
        + linker_sections + '\n  ._user_heap_stack :'))
    images = []
    for opt in args.optimizations:
        for variant in args.variants:
            directory = output/variant/opt
            directory.mkdir()
            cppflags = [*flags, *include, *BASE['includes'](output/variant), '-std=c++17', '-'+opt,
                        '-Wall', '-Wextra', '-Werror', '-pedantic-errors', '-fno-use-cxa-atexit', '-fstack-usage',
                        f'-DTELEMETRY_LAYOUT_VARIANT={BASE["VARIANTS"][variant]}',
                        f'-DLAYOUT_OPT={2 if opt == "O2" else 0}', '-DLAYOUT_FIELD_COUNT=128', '-DLAYOUT_TABLE_ALIGN=32']
            objects = []
            for source in sources_for_fixture:
                source = source if source.is_absolute() else output/variant/source
                obj = directory/(source.stem+'.o')
                source_flags = [*BASE['ARM'], '-x', 'assembler-with-cpp'] if source.suffix == '.S' else cppflags
                run([compiler, *source_flags, '-c', source, '-o', obj], directory/(source.stem+'.log'))
                objects.append(obj)
            elf = directory/'benchmark.elf'
            binary = directory/'benchmark.bin'
            run([compiler, *BASE['ARM'], *cobjects, startup, *objects, '-T'+str(linker),
                 '--specs=nano.specs', '--specs=nosys.specs', *link_flags, '-Wl,--gc-sections', '-Wl,-Map='+str(directory/'benchmark.map'),
                 '-Wl,--start-group', '-lc', '-lm', '-Wl,--end-group', '-o', elf], directory/'link.log')
            run([objcopy, '-O', 'binary', elf, binary], directory/'binary.log')
            if not 0 < binary.stat().st_size <= 65536:
                raise RuntimeError(f'Image exceeds the backed-up internal flash: {binary}')
            run([objdump, '-h', elf], directory/'sections.log')
            run([objdump, '-dr', '-C', elf], directory/'benchmark.asm')
            run([size, elf], directory/'size.log')
            lib_hashes = {str(p.relative_to(output/variant)): sha(p) for p in (output/variant/'lib').rglob('*') if p.is_file()}
            image = dict(variant=variant, optimization=opt, elf=str(elf), elf_sha256=sha(elf), binary_sha256=sha(binary),
                         flash_bytes=binary.stat().st_size, library_sources=lib_hashes,
                         objects_sha256={p.name: sha(p) for p in objects})
            images.append(image)
            print(f'BUILT {variant} {opt}: {image["flash_bytes"]} / 65536 flash bytes', flush=True)
    if manifest != {str(p): sha(p) for p in inputs}:
        raise RuntimeError('Build inputs changed during compilation')
    (output/'build-inputs.json').write_text(json.dumps(manifest, indent=2)+'\n')
    (output/'images.json').write_text(json.dumps(images, indent=2)+'\n')
    return images


def sequence(profile, count):
    ids = [[0, 1, 2, 4][profile]]*1024 if profile < 4 else [i % count for i in range(1024)]
    if profile == 5:
        state = 0x19a753
        for i in range(1023, 0, -1):
            state ^= (state << 13) & 0xffffffff
            state ^= state >> 17
            state ^= (state << 5) & 0xffffffff
            j = state % (i+1)
            ids[i], ids[j] = ids[j], ids[i]
    return ids


def expected_sum(op, ids, base, size):
    types = [1, 1, 8, 8, 8, 8, 8, 1]
    float_writes = [0, 0, 0, 3, 3, 3, 0, 2]
    u16_writes = [0, 0, 0, 0, 3, 3, 0, 2]
    result = 0
    for id_ in ids:
        kind = id_ % 8
        if op == 0: value = (base + id_*size) >> 5
        elif op == 1: value = types[kind]
        elif op in (2, 5):
            number = 230.0 if op == 5 or kind in (0, 1, 7) else 12.0 if kind == 6 else 17.0
            value = struct.unpack('<I', struct.pack('<f', number))[0] ^ id_
        elif op == 3: value = float_writes[kind]
        elif op == 4: value = u16_writes[kind]
        else: value = 0
        result += value
    return (result * 32) & 0xffffffff


def measure(args, output, images):
    import serial
    with serial.Serial(args.port, 115200, timeout=0.2):
        pass
    connection = ['-c', 'port=SWD', 'sn='+args.serial, 'mode=UR', 'reset=HWrst', 'freq=4000']
    receipt = dict(started=datetime.now(timezone.utc).isoformat(), serial=args.serial, port=args.port,
                   completed=False, restored_and_verified=False, images=images)
    receipt['compiler'] = (output/'common/compiler.log').read_text(encoding='utf-8').splitlines()[0]
    receipt['build_inputs'] = json.loads((output/'build-inputs.json').read_text(encoding='utf-8'))

    def save():
        (output/'session.json').write_text(json.dumps(receipt, indent=2)+'\n')

    def program(tag, path, raw=False):
        text = run([args.programmer, *connection, '-w', path, *(['0x08000000'] if raw else []), '-v', '-rst'], output/(tag+'.log'), 60)
        if 'Download verified successfully' not in text:
            raise RuntimeError('Missing programming verification: '+tag)

    backup = output/'before.bin'
    run([args.programmer, *connection, '-u', '0x08000000', '0x10000', backup, '-rst'], output/'backup.log', 60)
    if not backup.is_file() or backup.stat().st_size != 65536:
        raise RuntimeError('A complete 64 KiB backup is required')
    receipt['backup_sha256'] = sha(backup)
    save()
    rows = []
    try:
        for image in images:
            variant, opt = image['variant'], image['optimization']
            if sha(image['elf']) != image['elf_sha256']:
                raise RuntimeError('Compiled image changed before programming')
            tag = variant+'-'+opt
            program('flash-'+tag, image['elf'])
            print('MEASURING '+tag, flush=True)
            with serial.Serial(args.port, 115200, timeout=0.5, write_timeout=2) as port:
                time.sleep(0.3)
                port.reset_input_buffer()
                if port.write(b'R') != 1:
                    raise RuntimeError('Short command write')
                port.flush()
                expected_plan = {(m, 128 if m < 2 else 1024, p, op, rep)
                    for m in range(3) for p in range(6) for op in range(7) for rep in range(5)
                    if op < 5 or (m == 0 and p == 0)}
                observed, ready = set(), None
                start = time.monotonic()
                with (output/(tag+'-uart.log')).open('w', encoding='utf-8') as log:
                    while time.monotonic() - start < 90:
                        raw = port.readline()
                        if not raw:
                            continue
                        text = raw.decode('ascii').strip()
                        log.write(text+'\n'); log.flush()
                        words = text.split()
                        if words[:2] == ['LAYOUT', 'READY']:
                            if ready is not None or len(words) != 16:
                                raise RuntimeError('Unexpected READY: '+text)
                            ready = list(map(int, words[2:]))
                            version, number, optimization, clock, size, alignment, get, set_, declared, cache, flash_base, ram_base, iterations, reps = ready
                            if (version, number, optimization, clock, iterations, reps) != (1, BASE['VARIANTS'][variant], 2 if opt == 'O2' else 0, 600000000, 32768, 5):
                                raise RuntimeError('Device build/clock mismatch: '+text)
                            if (size, alignment, get, set_, declared) != LAYOUTS[variant] or cache == 0 or flash_base % 32 or ram_base % 32:
                                raise RuntimeError('Device layout/cache mismatch: '+text)
                            if not 0x08000000 <= flash_base < 0x08010000 or not 0x24000000 <= ram_base < 0x24071c00:
                                raise RuntimeError('Unexpected table memory placement: '+text)
                            image['device'] = dict(clock_hz=clock, data_cache_bytes=cache, flash_base=flash_base, ram_base=ram_base)
                        elif words[:2] == ['LAYOUT', 'T']:
                            if ready is None or len(words) != 9:
                                raise RuntimeError('Timing before validated READY: '+text)
                            memory, count, profile, op, rep, cycles, checksum = map(int, words[2:])
                            key = (memory, count, profile, op, rep)
                            if key not in expected_plan or key in observed or not 0 < cycles < 327680000:
                                raise RuntimeError('Invalid timing coverage/value: '+text)
                            expected = expected_sum(op, sequence(profile, count), flash_base if memory == 0 else ram_base, size)
                            if checksum != expected:
                                raise RuntimeError(f'Checksum mismatch: {text}; expected {expected}')
                            observed.add(key)
                            row = dict(variant=variant, optimization=opt, memory=memory, count=count, profile=profile,
                                operation=op, repetition=rep, cycles=cycles, calls=32768, checksum=checksum, elf_sha256=image['elf_sha256'])
                            rows.append(row)
                            with (output/'samples.jsonl').open('a', encoding='utf-8') as stream:
                                stream.write(json.dumps(row)+'\n')
                        elif text == 'LAYOUT DONE':
                            if observed != expected_plan:
                                raise RuntimeError('Missing timing windows')
                            break
                        else:
                            raise RuntimeError('Unexpected board report: '+text)
                    else:
                        raise TimeoutError('Board did not finish '+tag)
                image['windows'] = len(observed)
                save()
                print(f'PASS {tag}: {len(observed)} windows, all checksums correct', flush=True)
        receipt['completed'] = True
    finally:
        try:
            program('restore', backup, True)
            restored = output/'after.bin'
            run([args.programmer, *connection, '-u', '0x08000000', '0x10000', restored, '-rst'], output/'readback.log', 60)
            receipt['restored_sha256'] = sha(restored)
            receipt['restored_and_verified'] = receipt['restored_sha256'] == receipt['backup_sha256']
            if not receipt['restored_and_verified']:
                raise RuntimeError('Restored firmware differs from the backup')
            print('Original 64 KiB firmware restored and read-back verified.', flush=True)
        finally:
            save()
    groups = {}
    for row in rows:
        key = tuple(row[k] for k in ('optimization', 'memory', 'count', 'profile', 'operation', 'variant'))
        groups.setdefault(key, []).append(row['cycles']/row['calls'])
    summary = [dict(zip(('optimization', 'memory', 'count', 'profile', 'operation', 'variant'), key),
                    median=statistics.median(values), minimum=min(values), maximum=max(values)) for key, values in groups.items()]
    (output/'summary.json').write_text(json.dumps(summary, indent=2)+'\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cube', type=Path, required=True)
    parser.add_argument('--arm-cxx', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--variants', nargs='+', choices=VARIANTS, default=VARIANTS[:4])
    parser.add_argument('--optimizations', nargs='+', choices=['O2', 'Os'], default=['O2', 'Os'])
    parser.add_argument('--run', action='store_true')
    parser.add_argument('--serial', default='002A001F3033510135393935')
    parser.add_argument('--port', default='COM6')
    parser.add_argument('--programmer', default='C:/ST/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe')
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists():
        parser.error('Use a new output directory to preserve previous evidence')
    output.mkdir(parents=True)
    images = build(args, output)
    if args.run:
        measure(args, output, images)


if __name__ == '__main__':
    main()
