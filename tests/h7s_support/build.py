#!/usr/bin/env python3
"""Capture and build current C++20 H7S benches without accessing a device.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
ARM = ['-mcpu=cortex-m7', '-mthumb', '-mfpu=fpv5-d16', '-mfloat-abi=hard',
       '-fno-exceptions', '-fno-rtti']


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def lf_sha(path):
    return hashlib.sha256(Path(path).read_bytes().replace(b'\r\n', b'\n')).hexdigest()


def run(command, log, timeout=180):
    result = subprocess.run(list(map(str, command)), stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, encoding='utf-8', errors='replace', timeout=timeout)
    Path(log).write_text(result.stdout, encoding='utf-8')
    if result.returncode:
        raise RuntimeError(f'{log}: exit {result.returncode}\n{result.stdout[-6000:]}')
    return result.stdout


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def check_flash_sections(text, binary_bytes):
    """Reject loadable image bytes outside the complete internal Flash backup."""
    loads = []
    for row in re.finditer(r'^\s*\d+\s+(\S+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)[^\n]*\n([^\n]*)', text, re.M):
        name, size, address, load_address, attributes = row.groups()
        size, address, load_address = int(size, 16), int(address, 16), int(load_address, 16)
        if size and 'ALLOC' in attributes and 'LOAD' in attributes:
            if not 0x08000000 <= load_address < load_address + size <= 0x08010000:
                raise RuntimeError('Loadable section is outside backed-up internal Flash: ' + name)
            loads.append((name, address, load_address, size))
    if not loads or loads[0][:3] != ('.isr_vector', 0x08000000, 0x08000000):
        raise RuntimeError('Expected internal Flash vector table at 0x08000000')
    end = max(address + size for _, _, address, size in loads)
    if end - 0x08000000 != binary_bytes:
        raise RuntimeError('ELF load span and binary extent differ')
    return [0x08000000, end]


def build(args, output, *, fixture_sources, fixture_inputs=(), examples=()):
    """Build both release images from captured LF sources and a Cube copy."""
    output = Path(output).resolve()
    if output == ROOT or output.is_relative_to(ROOT / 'lib') or output.is_relative_to(ROOT / 'tests'):
        raise RuntimeError('Build output must not overlap source directories')
    compiler = Path(args.arm_cxx).resolve()
    if not compiler.is_file():
        raise RuntimeError('Compiler is not a file: ' + str(compiler))
    source_head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    source_status = subprocess.check_output(['git', 'status', '--porcelain', '-uall'], cwd=ROOT, text=True)
    sources = [Path(p).resolve() for p in fixture_sources]
    paths = {p.resolve() for p in (ROOT / 'lib').rglob('*') if p.is_file()}
    paths.update([Path(__file__).resolve(), *sources, *(Path(p).resolve() for p in fixture_inputs)])
    for relative in examples:
        paths.update(p.resolve() for p in (ROOT / relative).rglob('*') if p.is_file())
    # Preserve relative test-header imports without copying unrelated test trees.
    pending = list(paths)
    while pending:
        path = pending.pop()
        if not path.is_relative_to(ROOT) or not path.is_file():
            raise RuntimeError('Build input is not a repository file: ' + str(path))
        if path.suffix not in ('.cpp', '.hpp', '.h', '.c'):
            continue
        for include in re.findall(r'^\s*#\s*include\s*"([^"]+)"', path.read_text(encoding='utf-8'), re.M):
            dependency = (path.parent / include).resolve()
            if dependency.is_file() and dependency not in paths:
                paths.add(dependency)
                pending.append(dependency)
    before = {p.relative_to(ROOT).as_posix(): lf_sha(p) for p in sorted(paths)}
    captured = output / 'inputs'
    for relative, digest in before.items():
        target = captured / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes((ROOT / relative).read_bytes().replace(b'\r\n', b'\n'))
        if sha(target) != digest:
            raise RuntimeError('Source changed during capture: ' + relative)
    cube = output / 'scaffold'
    for relative in ('Boot/Core', 'Drivers'):
        shutil.copytree(args.cube.resolve() / relative, cube / relative)
    linker_source = 'Boot/STM32H7S3L8HX_FLASH.ld'
    shutil.copy2(args.cube.resolve() / linker_source, cube / linker_source)
    scaffold = {p.relative_to(cube).as_posix(): sha(p) for p in sorted(cube.rglob('*')) if p.is_file()}
    common = output / 'common'
    common.mkdir()
    version = run([compiler, '--version'], common / 'compiler.log').splitlines()[0]
    if not re.search(r'14\.3\.1(?:\s|$)', version):
        raise RuntimeError('These H7S benches require the CubeIDE ARM 14.3.1 compiler')
    def tool(name):
        return compiler.with_name('arm-none-eabi-' + name + compiler.suffix)
    flags = [*ARM, '-ffunction-sections', '-fdata-sections', '-DUSE_HAL_DRIVER',
             '-DSTM32H7S3xx', '--specs=nano.specs']
    include = ['-I' + str(cube / 'Boot/Core/Inc'), '-I' + str(captured / 'lib'),
        '-I' + str(captured / 'lib/boost_pfr/include'), '-I' + str(captured / 'lib/magic_enum')]
    if examples:
        include += ['-I' + str(captured / 'examples')]
    for relative in ('Drivers/STM32H7RSxx_HAL_Driver/Inc', 'Drivers/STM32H7RSxx_HAL_Driver/Inc/Legacy',
                     'Drivers/CMSIS/Device/ST/STM32H7RSxx/Include', 'Drivers/CMSIS/Include'):
        include += ['-isystem', str(cube / relative)]
    common_objects = []
    for source in [*sorted((cube / 'Drivers/STM32H7RSxx_HAL_Driver/Src').glob('*.c')),
                   *sorted((cube / 'Boot/Core/Src').glob('*.c'))]:
        obj = common / (source.stem + '.o')
        if obj in common_objects:
            raise RuntimeError('Duplicate Cube object name: ' + source.stem)
        run([tool('gcc'), *flags, *include, '-Os', '-std=gnu11', '-Wall', '-c', source, '-o', obj], obj.with_suffix('.log'))
        common_objects.append(obj)
    startup = common / 'startup.o'
    run([tool('gcc'), *ARM, '-x', 'assembler-with-cpp', '-c',
         cube / 'Boot/Core/Startup/startup_stm32h7s3l8hx.s', '-o', startup], common / 'startup.log')
    original = (cube / linker_source).read_text(encoding='utf-8')
    anchor = '  ._user_heap_stack :'
    if original.count(anchor) != 1:
        raise RuntimeError('Expected exactly one linker section anchor')
    linker = common / 'benchmark.ld'
    linker.write_text(original.replace(anchor,
        '  .dtcm_ids (NOLOAD) : { . = ALIGN(32); *(.dtcm_ids) . = ALIGN(32); } >DTCM\n' + anchor, 1), encoding='utf-8')
    images = []
    for opt in ('O2', 'Os'):
        directory = output / 'Current' / opt
        directory.mkdir(parents=True)
        cppflags = [*flags, *include, '-std=c++20', '-' + opt, '-Wall', '-Wextra', '-Werror',
                    '-pedantic-errors', '-fno-use-cxa-atexit', '-fstack-usage']
        objects = []
        for original_source in sources:
            source = captured / original_source.relative_to(ROOT)
            obj = directory / (source.stem + '.o')
            if obj in objects:
                raise RuntimeError('Duplicate fixture object name: ' + source.stem)
            run([compiler, *cppflags, '-c', source, '-o', obj], obj.with_suffix('.log'))
            objects.append(obj)
        elf, binary = directory / 'benchmark.elf', directory / 'benchmark.bin'
        run([compiler, *ARM, *common_objects, startup, *objects, '-T' + str(linker),
            '--specs=nano.specs', '--specs=nosys.specs', '-Wl,--gc-sections',
            '-Wl,-Map=' + str(directory / 'benchmark.map'), '-Wl,--start-group', '-lc', '-lm',
            '-Wl,--end-group', '-o', elf], directory / 'link.log')
        run([tool('objcopy'), '-O', 'binary', elf, binary], directory / 'binary.log')
        if not 0 < binary.stat().st_size <= 65536:
            raise RuntimeError('Image exceeds the complete internal Flash backup: ' + str(binary))
        sections = run([tool('objdump'), '-h', elf], directory / 'sections.log')
        flash_load_span = check_flash_sections(sections, binary.stat().st_size)
        run([tool('objdump'), '-dr', '-C', elf], directory / 'benchmark.asm')
        run([tool('size'), elf], directory / 'size.log')
        images.append(dict(variant='Current', optimization=opt, elf=str(elf), binary=str(binary),
            elf_sha256=sha(elf), binary_sha256=sha(binary), flash_bytes=binary.stat().st_size,
            library_sources={p: h for p, h in before.items() if p.startswith('lib/')},
            objects_sha256={p.name: sha(p) for p in objects},
            common_objects_sha256={p.name: sha(p) for p in [*common_objects, startup]},
            linker_sha256=sha(linker), flags=cppflags,
            flash_load_span=flash_load_span,
            stack_usage={p.name: dict(sha256=sha(p), bytes=p.stat().st_size) for p in sorted(directory.glob('*.su'))}))
        print(f'BUILT Current {opt}: {images[-1]["flash_bytes"]} / 65536 Flash bytes', flush=True)
    if before != {p: lf_sha(ROOT / p) for p in before}:
        raise RuntimeError('Build inputs changed during compilation')
    if scaffold != {p: sha(cube / p) for p in scaffold}:
        raise RuntimeError('Copied Cube inputs changed during compilation')
    write_json(output / 'build-inputs.json', before)
    write_json(output / 'images.json', images)
    write_json(output / 'build-provenance.json', dict(source_head=source_head,
        source_dirty=bool(source_status.strip()), input_lf_sha256=before,
        scaffold_sha256=scaffold, compiler=version, compiler_path=str(compiler),
        compiler_sha256=sha(compiler), required_clock_hz=600000000,
        internal_flash_address='0x08000000', internal_flash_bytes=65536))
    return images
