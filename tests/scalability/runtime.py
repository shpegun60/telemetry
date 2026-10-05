#!/usr/bin/env python3
"""Measure erased runtime indexes and inspect ARM ABI sizes, without a device.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
Generated sources, objects and logs belong to the caller's output directory.
Host nanoseconds include the fixture loop and are not Cortex-M cycle evidence.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
EXPECTED_CHECKS = 581758


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--arm', action='store_true')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--resources', action='store_true',
                        help='Measure actual typed descriptor/value files instead of erased indexes')
    parser.add_argument('--rows', type=int, nargs='+', default=[32, 128])
    args = parser.parse_args()
    if args.arm and (args.sanitize or args.resources):
        parser.error('Resource timing and sanitizer modes require host execution')
    out = args.build_dir.resolve()
    if out == ROOT or ROOT in out.parents:
        parser.error('Generated artifacts must stay outside the checkout')
    out.mkdir(parents=True, exist_ok=True)
    compiler = Path(shutil.which(args.cxx) or args.cxx).absolute()
    environment = os.environ.copy()
    environment['PATH'] = str(compiler.parent) + os.pathsep + environment.get('PATH', '')
    environment.setdefault('ASAN_OPTIONS', 'detect_leaks=1:detect_stack_use_after_return=1')
    environment.setdefault('UBSAN_OPTIONS', 'halt_on_error=1')
    inputs = [Path(__file__).resolve(), *sorted((ROOT / 'lib').rglob('*.hpp')),
              *sorted((ROOT / 'lib').rglob('*.h')),
              *sorted((ROOT / 'lib').rglob('*.cpp')),
              ROOT / 'tests/scalability/Runtime.cpp', ROOT / 'tests/scalability/Layout.cpp',
              ROOT / 'tests/scalability/Resources.cpp']
    before = {str(p.relative_to(ROOT)): digest(p) for p in inputs}
    report = dict(completed=False, source_head=subprocess.check_output(
        ['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        source_dirty=bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT)),
        input_hashes=before, compiler_sha256=digest(compiler), arm=args.arm,
        sanitize=args.sanitize, images={})

    def run(command, label):
        start = time.perf_counter()
        with subprocess.Popen(list(map(str, command)), cwd=ROOT, env=environment,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              encoding='utf-8', errors='replace',
                              start_new_session=os.name == 'posix') as process:
            timed_out = False
            try:
                output = process.communicate(timeout=180)[0]
            except subprocess.TimeoutExpired:
                timed_out = True
                if os.name == 'posix':
                    os.killpg(process.pid, signal.SIGKILL)
                else:
                    subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                    process.kill()
                output = process.communicate()[0]
            result = subprocess.CompletedProcess(command, 124 if timed_out else process.returncode,
                                                 stdout=output)
        (out / (label + '.log')).write_text(
            f'COMMAND {list(map(str, command))!r}\nEXIT {result.returncode}\n'
            f'SECONDS {time.perf_counter() - start:.6f}\n{result.stdout}', encoding='utf-8')
        if result.returncode:
            report['failure'] = dict(command_label=label, returncode=result.returncode,
                                     log=label + '.log')
            (out / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
            raise RuntimeError(label + ': failed\n' + result.stdout)
        print(label + ': pass', flush=True)
        return result.stdout

    report['compiler'] = run([compiler, '--version'], 'compiler').splitlines()[0]
    flags = [compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-pedantic-errors',
             '-fdiagnostics-color=never', '-Ilib', '-Ilib/boost_pfr/include', '-Ilib/magic_enum']
    if args.arm:
        flags += ['-mcpu=cortex-m7', '-mthumb', '-mfpu=fpv5-d16', '-mfloat-abi=hard',
                  '-ffunction-sections', '-fdata-sections', '-fstack-usage',
                  '-fno-exceptions', '-fno-rtti']
        report['flags'] = list(map(str, flags))
        suffix = '.exe' if os.name == 'nt' else ''
        tool = lambda name: compiler.with_name('arm-none-eabi-' + name + suffix)
        for opt in ('O2', 'Os', 'Og'):
            image = out / (opt + '-layout.o')
            run([*flags, '-' + opt, '-c', ROOT / 'tests/scalability/Layout.cpp', '-o', image],
                'compile-' + opt)
            raw = out / (opt + '-layout.bin')
            run([tool('objcopy'), '-O', 'binary', '-j', '.rodata.scalability_layout', image, raw],
                'layout-' + opt)
            if len(raw.read_bytes()) != 28:
                raise RuntimeError('ARM layout table was omitted or changed')
            values = struct.unpack('<7I', raw.read_bytes())
            report['images'][opt] = dict(sha256=digest(image), layout=dict(zip(
                ('FieldEntry', 'CommandEntry', 'ServiceEntry', 'Catalog', 'ModelView',
                 'ValueToken', 'DescriptorSegment'), values)))
            run([tool('objdump'), '-dr', image], 'disassembly-' + opt)
            run([tool('size'), '-A', image], 'sections-' + opt)
        report['scope'] = 'ARM compile/object inspection; no device execution'
    elif args.resources:
        report['profiles'] = []
        for rows in args.rows:
            if rows <= 0 or rows > 256:
                raise RuntimeError('Resource fixture rows must be in 1..256; larger tables have separate compiler probes')
            image = out / (f'resources-{rows}' + ('.exe' if os.name == 'nt' else ''))
            extras = ['-g', '-fno-omit-frame-pointer', '-fsanitize=address,undefined',
                      '-fno-sanitize-recover=all'] if args.sanitize else []
            source = ROOT / 'lib/resource/telemetry/v3/detail/Values.cpp'
            run([*flags, '-O1' if args.sanitize else '-O2', f'-DROWS={rows}', *extras,
                 ROOT / 'tests/scalability/Resources.cpp', source, '-o', image], f'compile-{rows}')
            profile = json.loads(run([image], f'execute-{rows}'))
            if profile['rows'] != rows or profile['checks'] != 59 + 4 * rows:
                raise RuntimeError('Resource fixture coverage changed')
            if profile['type_count'] != 12 or profile['values_wire_bytes'] != 24 + 5 * rows:
                raise RuntimeError('Repeated scalar registry/value layout changed')
            profile['image_sha256'] = digest(image)
            report['profiles'].append(profile)
        report['scope'] = 'Host actual typed Model and files; timing is not MCU cycles'
        report['timing_valid_for_comparison'] = not args.sanitize
    else:
        for opt in (('O1',) if args.sanitize else ('O2', 'Os')):
            image = out / (opt + '-runtime' + ('.exe' if os.name == 'nt' else ''))
            extras = ['-g', '-fno-omit-frame-pointer', '-fsanitize=address,undefined',
                      '-fno-sanitize-recover=all'] if args.sanitize else []
            run([*flags, '-' + opt, *extras, ROOT / 'tests/scalability/Runtime.cpp', '-o', image],
                'compile-' + opt)
            records = [json.loads(line) for line in run([image], 'execute-' + opt).splitlines()]
            summary = records.pop()
            if summary['checks'] != EXPECTED_CHECKS or len(records) != 50:
                raise RuntimeError('Runtime profile or exact correctness coverage changed')
            expected = {(count, spread, op) for count in (32, 128, 1024, 16384, 65536)
                        for spread in (False, True) for op in range(5)}
            actual = {(r['rows_per_family'], r['spread'], r['operation']) for r in records}
            if actual != expected:
                raise RuntimeError('Runtime benchmark population changed')
            report['images'][opt] = dict(sha256=digest(image), summary=summary, timings=records)
        report['scope'] = 'Host correctness and index timing; not typed Model capacity'
        report['timing_valid_for_comparison'] = not args.sanitize
    if before != {str(p.relative_to(ROOT)): digest(p) for p in inputs}:
        raise RuntimeError('Source changed during validation')
    report['completed'] = True
    (out / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
