#!/usr/bin/env python3
"""Measure visitor-type multiplication using the retained qualification fixture.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
Only an external generated copy gains the fifth through eighth call sites.
The original fixture and historical measurements are never rewritten. ARM
sections are object sizes, not linked firmware sizes or measured MCU cycles.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--arm', action='store_true')
    parser.add_argument('--rows', type=int, nargs='+', default=[128, 256])
    args = parser.parse_args()
    if any(row < 1 or row > 256 for row in args.rows):
        parser.error('This bounded distinct-target fixture accepts 1..256 rows')
    out = args.build_dir.resolve()
    if out == ROOT or ROOT in out.parents:
        parser.error('Generated artifacts must stay outside the checkout')
    out.mkdir(parents=True, exist_ok=True)
    original = ROOT / 'tests/structured/qualification/Scale.cpp'
    content = original.read_text(encoding='utf-8')
    selectors = '#if VISITORS > 4\nSELECTOR(fifth, 5)\nSELECTOR(sixth, 6)\n'
    selectors += 'SELECTOR(seventh, 7)\nSELECTOR(eighth, 8)\n#endif\n'
    anchor = '} // namespace scale\n'
    if content.count(anchor) != 1:
        raise RuntimeError('Qualification fixture selector anchor changed')
    content = content.replace(anchor, selectors + anchor)
    switch = '\t\tcase 3:\n\t\t\treturn scale::fourth(id);\n'
    if content.count(switch) != 1:
        raise RuntimeError('Qualification fixture dispatch anchor changed')
    content = content.replace(switch, switch + '#if VISITORS > 4\n' + ''.join(
        f'\t\tcase {i}:\n\t\t\treturn scale::{name}(id);\n'
        for i, name in enumerate(('fifth', 'sixth', 'seventh', 'eighth'), 4)) + '#endif\n')
    source = out / 'VisitorScale.cpp'
    source.write_text(content, encoding='utf-8')
    compiler = Path(shutil.which(args.cxx) or args.cxx).absolute()
    env = os.environ.copy()
    env['PATH'] = str(compiler.parent) + os.pathsep + env.get('PATH', '')
    report = dict(completed=False, profiles=[], arm=args.arm,
                  original_sha256=hashlib.sha256(original.read_bytes()).hexdigest(),
                  generated_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                  compiler_sha256=hashlib.sha256(compiler.read_bytes()).hexdigest())
    report['source_head'] = subprocess.check_output(
        ['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    inputs = [Path(__file__).resolve(), original, *sorted((ROOT / 'lib').rglob('*.hpp')),
              *sorted((ROOT / 'lib').rglob('*.h'))]
    before = {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
              for path in inputs}
    report['input_hashes'] = before

    def save():
        (out / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')

    def run(command, label):
        start = time.perf_counter()
        with subprocess.Popen(list(map(str, command)), cwd=ROOT, env=env,
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
        elapsed = time.perf_counter() - start
        (out / (label + '.log')).write_text(
            f'COMMAND {list(map(str, command))!r}\nEXIT {result.returncode}\n'
            f'SECONDS {elapsed:.6f}\n{result.stdout}', encoding='utf-8')
        if result.returncode:
            report['failure'] = dict(command_label=label, returncode=result.returncode,
                                     log=label + '.log')
            save()
            raise RuntimeError(label + ': failed; see ' + str(out / (label + '.log')))
        print(label + ': pass', flush=True)
        return result.stdout, elapsed

    report['compiler'] = run([compiler, '--version'], 'compiler')[0].splitlines()[0]
    flags = [compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-pedantic-errors',
             '-fdiagnostics-color=never', '-Ilib', '-Ilib/boost_pfr/include', '-Ilib/magic_enum']
    suffix = '.exe' if os.name == 'nt' else ''
    if args.arm:
        flags += ['-mcpu=cortex-m7', '-mthumb', '-mfpu=fpv5-d16', '-mfloat-abi=hard',
                  '-ffunction-sections', '-fdata-sections', '-fstack-usage',
                  '-fno-exceptions', '-fno-rtti']
    report['flags'] = list(map(str, flags))
    for opt in (('O2', 'Os') if args.arm else ('O2',)):
        for rows in args.rows:
            for visitors, reuse in ((0, 0), (1, 0), (2, 0), (4, 0), (8, 0), (4, 1), (8, 1)):
                label = f'{opt}-{rows}-{visitors}-{reuse}'
                image = out / (label + ('.o' if args.arm else suffix))
                mode = ['-c'] if args.arm else ['-DSCALE_EXECUTE']
                row = dict(rows=rows, visitors=visitors, named_reuse=bool(reuse), opt=opt,
                           status='building')
                report['profiles'].append(row)
                save()
                output, elapsed = run([*flags, '-' + opt, f'-DROWS={rows}',
                                       f'-DVISITORS={visitors}', f'-DREUSE={reuse}',
                                       *mode, source, '-o', image], 'compile-' + label)
                row.update(compile_seconds=elapsed,
                           image_sha256=hashlib.sha256(image.read_bytes()).hexdigest())
                if args.arm:
                    tool = compiler.with_name('arm-none-eabi-size' + suffix)
                    sections = run([tool, '-A', image], 'sections-' + label)[0]
                    parsed = {name: int(size) for name, size in re.findall(
                        r'^(\S+)\s+(\d+)\s+\d+\s*$', sections, re.M)}
                    row['sections'] = {kind: sum(size for name, size in parsed.items()
                                                if name.startswith('.' + kind))
                                       for kind in ('text', 'rodata', 'data', 'bss')}
                else:
                    actual = json.loads(run([image], 'execute-' + label)[0])
                    if actual != dict(checks=rows * max(1, visitors) + 2, failures=0):
                        raise RuntimeError('Visitor fixture correctness coverage changed')
                    row['correctness'] = actual
                row['status'] = 'compiled' if args.arm else 'pass'
                save()
    report['completed'] = True
    if report['generated_sha256'] != hashlib.sha256(source.read_bytes()).hexdigest():
        raise RuntimeError('Generated visitor source changed during measurement')
    if before != {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                  for path in inputs}:
        raise RuntimeError('Build inputs changed during visitor measurement')
    save()


if __name__ == '__main__':
    main()
