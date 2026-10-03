#!/usr/bin/env python3
"""Capture/verify a compact record of offline host and ARM qualification.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
This records local reports, not a new execution or hardware measurement.
Report hashes allow comparison with retained raw reports when available.
"""
import argparse
from copy import deepcopy
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
EXPECTED = {'Mixed': 12230, 'Scale': 2316}
CONFIGURATIONS = {family + '-' + opt for family in EXPECTED for opt in ('O2', 'Os', 'Og')}
BIG_PROBES = ('bigRead', 'bigCommand', 'bigService')
SCOPE = 'offline host execution and ARM compile/link inspection'
NULL_CHECKS = '-fno-delete-null-pointer-checks'
SANITIZERS = '-fsanitize=address,undefined'
ARM_FLAGS = frozenset(('-mcpu=cortex-m7', '-mthumb', '-mfpu=fpv5-d16', '-mfloat-abi=hard',
                       '-fno-exceptions', '-fno-rtti', '-ffunction-sections', '-fdata-sections'))

# These are evidence roles, not arbitrary display labels. Changing a pinned
# compiler or dropping one mode requires an explicit matrix/receipt update.
EXPECTED_RUNS = {
    'mingw13': ('host', r'^g\+\+\.exe .*MinGW.* 13\.1\.0$', 'g++.exe', frozenset()),
    'gcc13-null': ('host', r'^g\+\+ \(Ubuntu.*\) 13\.3\.0$', 'g++', frozenset((NULL_CHECKS,))),
    'clang18-sanitized': ('host', r'^Ubuntu clang version 18\.1\.3(?:\s|$)', 'clang++-18',
                          frozenset((SANITIZERS,))),
    'cubeide14': ('compile/link only', r'^arm-none-eabi-g\+\+\.exe .* 14\.3\.1(?:\s|$)',
                  'arm-none-eabi-g++.exe', frozenset()),
    'cubeide14-null': ('compile/link only', r'^arm-none-eabi-g\+\+\.exe .* 14\.3\.1(?:\s|$)',
                       'arm-none-eabi-g++.exe', frozenset((NULL_CHECKS,))),
    'arm13': ('compile/link only', r'^arm-none-eabi-g\+\+ .* 13\.2\.1(?:\s|$)',
               'arm-none-eabi-g++', frozenset()),
    'arm13-null': ('compile/link only', r'^arm-none-eabi-g\+\+ .* 13\.2\.1(?:\s|$)',
                    'arm-none-eabi-g++', frozenset((NULL_CHECKS,))),
}


def inputs():
    """Use the same normalized LF input set as mcu/run.py."""
    paths = sorted(path for directory in (ROOT / 'lib', HERE, HERE.parent / 'qualification')
                   for path in directory.rglob('*')
                   if path.is_file() and path.suffix in ('.h', '.hpp', '.cpp', '.c', '.pri', '.py'))
    paths += [HERE.parent / 'traversal/Fixture.hpp']
    return {path.relative_to(ROOT).as_posix():
            hashlib.sha256(path.read_bytes().replace(b'\r\n', b'\n')).hexdigest() for path in paths}


def require(condition, reason):
    if not condition:
        raise RuntimeError(reason)


def verify(receipt, *, current=True):
    require(type(receipt['format_version']) is int and receipt['format_version'] == 1 and receipt['scope'] == SCOPE,
            'Receipt is not offline qualification format 1')
    require(re.fullmatch(r'[0-9a-f]{40}', receipt['source_head']) is not None,
            'Invalid source HEAD')
    require(type(receipt['source_dirty']) is bool, 'Missing source dirty state')
    hashes = receipt['input_lf_sha256']
    require(hashes and all(re.fullmatch(r'[0-9a-f]{64}', value) for value in hashes.values()),
            'Invalid input digests')
    if current:
        require(hashes == inputs(), 'Receipt inputs differ from the current source tree')
    require(set(receipt['runs']) == set(EXPECTED_RUNS), 'Receipt seven-run matrix changed')
    for name, run in receipt['runs'].items():
        require(run['compiler'] and '-std=c++20' in run['flags'], name + ': missing compiler/flags')
        execution, compiler_pattern, driver, modes = EXPECTED_RUNS[name]
        require(run['execution'] == execution and re.search(compiler_pattern, run['compiler']) is not None,
                name + ': wrong compiler or execution role')
        require(run['flags'][0].replace('\\', '/').rsplit('/', 1)[-1] == driver,
                name + ': wrong compiler driver')
        actual_modes = {flag for flag in run['flags'] if flag == NULL_CHECKS or flag.startswith('-fsanitize=')}
        require(actual_modes == modes, name + ': wrong sanitizer/null-check mode')
        if execution == 'compile/link only':
            require(ARM_FLAGS.issubset(run['flags']), name + ': missing Cortex-M7 ABI flags')
        if SANITIZERS in modes:
            require('-fno-omit-frame-pointer' in run['flags'], name + ': missing sanitizer frame-pointer flag')
        require(re.fullmatch(r'[0-9a-f]{64}', run['report_sha256']) is not None,
                name + ': missing raw report digest')
        require(set(run['configurations']) == CONFIGURATIONS, name + ': incomplete O2/Os/Og matrix')
        arm = run['execution'] == 'compile/link only'
        require(arm or run['execution'] == 'host', name + ': invalid execution scope')
        require(type(run['commands']) is int and run['commands'] == (67 if arm else 52),
                name + ': changed tool-command count')
        require(type(run['checks']) is int and run['checks'] == (0 if arm else 3 * sum(EXPECTED.values())),
                name + ': changed executed-condition count')
        for label, configuration in run['configurations'].items():
            family = label.split('-')[0]
            if not arm:
                require(type(configuration['checks']) is int and configuration['checks'] == EXPECTED[family],
                        name + '/' + label + ': changed condition count')
                require(type(configuration['consumer_checks']) is int and
                        configuration['consumer_checks'] == (97 if family == 'Mixed' else 0),
                        name + '/' + label + ': changed consumer count')
            elif family == 'Mixed':
                frames = configuration['big_probe_frames']
                for probe in BIG_PROBES:
                    selected = [frame for function, frame in frames.items() if '::' + probe + '(' in function]
                    require(len(selected) == 1 and type(selected[0]) is int and 0 <= selected[0] <= 256,
                            name + '/' + label + ': missing/large individual frame for ' + probe)
        if arm:
            control = run['linked_symbol_control']
            require(control['execution'] == 'nm only' and
                    {'_malloc_r', '_printf_r'}.issubset(control['rejected_symbols']),
                    name + ': real newlib symbol control missing')
            require(re.fullmatch(r'[0-9a-f]{64}', control['elf_sha256']) is not None,
                    name + ': negative ELF digest missing')
    return dict(runs=len(receipt['runs']), inputs=len(hashes), execution_scope=SCOPE)


def capture(reports):
    current = inputs()
    receipt = dict(format_version=1, scope=SCOPE,
                   captured_at_utc=datetime.now(timezone.utc).isoformat(),
                   source_head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                   source_dirty=bool(subprocess.check_output(
                       ['git', 'status', '--porcelain', '-uall', '--', *current], cwd=ROOT, text=True).strip()),
                   input_lf_sha256=current, runs={})
    for argument in reports:
        name, separator, value = argument.partition('=')
        require(separator and name and name not in receipt['runs'], 'Use unique NAME=REPORT arguments')
        raw = Path(value).read_bytes()
        report = json.loads(raw)
        require(report['input_lf_sha256'] == current, name + ': raw report inputs are stale')
        run = {key: report[key] for key in ('compiler', 'flags', 'commands', 'checks', 'execution',
                                           'gate_controls', 'codegen_controls')}
        run['report_sha256'] = hashlib.sha256(raw).hexdigest()
        if 'linked_symbol_control' in report:
            run['linked_symbol_control'] = report['linked_symbol_control']
        run['configurations'] = {}
        for label, data in report['configurations'].items():
            record = {key: data[key] for key in ('checks', 'consumer_checks', 'sections', 'native_codegen')
                      if key in data}
            record['big_probe_frames'] = {
                function.replace('\\', '/'): frame['bytes'] for function, frame in data['frames'].items()
                if any('::' + probe + '(' in function for probe in BIG_PROBES)}
            run['configurations'][label] = record
        receipt['runs'][name] = run
    verify(receipt)
    return receipt


def self_test(receipt):
    """Changes to source identity, counts, scope and required controls fail."""
    host, arm = 'mingw13', 'cubeide14'
    cases = []
    for field, value in (('format_version', 2), ('scope', 'hardware'), ('source_head', 'invalid')):
        changed = deepcopy(receipt)
        changed[field] = value
        cases.append(changed)
    changed = deepcopy(receipt)
    changed['input_lf_sha256'][next(iter(changed['input_lf_sha256']))] = '0' * 64
    cases.append(changed)
    changed = deepcopy(receipt)
    changed['runs'][host]['checks'] -= 1
    cases.append(changed)
    changed = deepcopy(receipt)
    changed['runs'][host]['configurations']['Mixed-O2']['checks'] -= 1
    cases.append(changed)
    changed = deepcopy(receipt)
    changed['runs'][host]['configurations']['Mixed-O2']['consumer_checks'] = 96
    cases.append(changed)
    changed = deepcopy(receipt)
    changed['runs'][arm]['linked_symbol_control']['rejected_symbols'] = []
    cases.append(changed)
    changed = deepcopy(receipt)
    frames = changed['runs'][arm]['configurations']['Mixed-O2']['big_probe_frames']
    frames[next(iter(frames))] = 4096
    cases.append(changed)
    # A valid remaining host/ARM pair is insufficient: every named role stays
    # required, including each compiler's null-check/sanitizer counterpart.
    for name in EXPECTED_RUNS:
        changed = deepcopy(receipt)
        del changed['runs'][name]
        cases.append(changed)
    changed = deepcopy(receipt)
    changed['runs'] = {name: changed['runs'][name] for name in (host, arm)}
    cases.append(changed)
    changed = deepcopy(receipt)
    changed['runs']['extra'] = deepcopy(changed['runs'][host])
    cases.append(changed)
    for name in EXPECTED_RUNS:
        if name == host:
            continue
        changed = deepcopy(receipt)
        changed['runs'][name] = deepcopy(changed['runs'][host])
        cases.append(changed)
    for name, (_, _, _, modes) in EXPECTED_RUNS.items():
        changed = deepcopy(receipt)
        if modes:
            changed['runs'][name]['flags'] = [flag for flag in changed['runs'][name]['flags'] if flag not in modes]
        else:
            changed['runs'][name]['flags'].append(NULL_CHECKS)
        cases.append(changed)
    changed = deepcopy(receipt)
    changed['runs'][arm]['flags'].remove('-mfloat-abi=hard')
    cases.append(changed)
    for changed in cases:
        try:
            verify(changed)
        except RuntimeError:
            continue
        raise AssertionError('Changed receipt accepted')
    return dict(rejected_mutations=len(cases))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_subparsers(dest='mode', required=True)
    create = modes.add_parser('capture')
    create.add_argument('--report', action='append', required=True, metavar='NAME=REPORT')
    create.add_argument('--output', type=Path, required=True)
    check = modes.add_parser('verify')
    check.add_argument('receipt', type=Path)
    check.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.mode == 'capture':
        receipt = capture(args.report)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(verify(receipt), sort_keys=True))
    else:
        receipt = json.loads(args.receipt.read_text(encoding='utf-8'))
        result = verify(receipt)
        if args.self_test:
            result['controls'] = self_test(receipt)
        print(json.dumps(result, sort_keys=True))


if __name__ == '__main__':
    main()
