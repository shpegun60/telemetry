#!/usr/bin/env python3
"""Check retained MCU evidence without serial, toolchain or board access."""
# Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
import argparse
import copy
import json
from pathlib import Path
import statistics

HERE = Path(__file__).resolve().parent
BASELINE = 'f1cfbd891ef2f0c23f0fb2dae70be8ed4bd6fcaf'
NAMES = ('schema_first', 'schema_last', 'commands_first', 'commands_last',
         'values_first', 'values_last', 'enum_sequential', 'parameters_sequential')
SIZES = (60230, 71724, 2068)
PAIRS = {(v, o) for v in ('Baseline', 'Current') for o in ('O2', 'Os')}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def valid_hash(value, size=64):
    return (isinstance(value, str) and len(value) == size
            and all(c in '0123456789abcdef' for c in value))


def measurements(receipt):
    """Validate complete raw rows; usable before the board is restored."""
    images = receipt['images']
    require(len(images) == 4 and {(i['variant'], i['optimization']) for i in images} == PAIRS,
            'Expected exactly four distinct images')
    require(set(receipt['runs']) == {v + '-' + o for v, o in PAIRS}, 'Unexpected run coverage')
    checksums, file_checksums, summaries = {}, {}, {}
    for image in images:
        key = image['variant'] + '-' + image['optimization']
        data = receipt['runs'][key]
        require(data.get('idle') is True and data.get('done') is True, 'Missing idle/completion')
        expected_ready = [2, 7 if image['variant'] == 'Baseline' else 6,
                          2 if image['optimization'] == 'O2' else 0,
                          600000000, 256, 256, 5, 8, *SIZES]
        require(data['ready'] == expected_ready, 'Wrong clock, layout or file sizes')
        timing, stacks, files = data['timing'], data['stacks'], data['files']
        require(len(timing) == 40 and all(len(r) == 4 for r in timing)
                and {(r[0], r[1]) for r in timing} == {(o, r) for o in range(8) for r in range(5)},
                'Incomplete/duplicate timing coverage')
        require(len(stacks) == 18 and all(len(r) == 4 for r in stacks)
                and {(r[0], r[1]) for r in stacks} == {(o, p) for o in range(9) for p in range(2)},
                'Incomplete/duplicate stack coverage')
        require(len(files) == 6 and all(len(r) == 5 for r in files)
                and {(r[0], r[1]) for r in files} == {(f, c) for f in range(3) for c in (31, 256)},
                'Incomplete/duplicate complete-file coverage')
        require(all(type(n) is int for rows in (timing, stacks, files) for row in rows for n in row),
                'Non-integer measurement')
        for number, _, length, checksum, getters in files:
            require(length == SIZES[number] and 0 <= checksum <= 0xffffffff
                    and getters == (512 if number == 2 else 0), 'Invalid complete-file result')
            require(file_checksums.setdefault(number, checksum) == checksum,
                    'Complete bytes differ across revisions/chunk sizes')
        summary = {}
        for operation, name in enumerate(NAMES):
            ts = [r for r in timing if r[0] == operation]
            ss = [r for r in stacks if r[0] == operation]
            expected = ss[0][3]
            require(0 <= expected <= 0xffffffff and all(r[3] == expected for r in ss),
                    'Stack checksum differs between fill patterns')
            if operation >= 6:
                require(expected == (16 if operation == 6 else 10), 'Wrong sequential visit result')
            require(all(0 < r[2] < 0xffffffff and r[3] == (expected * 256) & 0xffffffff for r in ts),
                    'DWT/checksum validation failed')
            require(all(0 < r[2] <= 16384 for r in ss), 'Stack guard exceeded')
            require(checksums.setdefault(operation, expected) == expected,
                    'Selected payload/visitor result differs across revisions')
            summary[name] = {
                'median_cycles': statistics.median(r[2] / 256 for r in ts),
                'min_cycles': min(r[2] / 256 for r in ts),
                'max_cycles': max(r[2] / 256 for r in ts),
                'stack_bytes': max(r[2] for r in ss)}
        require(all(512 <= r[2] <= 16384 and r[3] == 0 for r in stacks if r[0] == 8),
                '512-byte stack control failed')
        summaries[key] = summary
    return summaries


def verify(receipt):
    """Completion requires measured coverage AND a verified firmware restore."""
    require(receipt.get('completed') is True and receipt.get('restored_and_verified') is True,
            'Incomplete session or restoration')
    require(not receipt.get('run_error') and not receipt.get('restore_error'), 'Session has errors')
    require(valid_hash(receipt.get('backup_sha256'))
            and receipt['backup_sha256'] == receipt.get('restored_sha256'), 'Restore hash mismatch')
    require(receipt.get('board') == 'NUCLEO-H7S3L8' and valid_hash(receipt.get('head'), 40)
            and bool(receipt.get('compiler')), 'Missing board/source/compiler identity')
    inputs = receipt.get('build_inputs', {})
    require(bool(inputs) and all(valid_hash(v) for v in inputs.values()), 'Missing build hashes')
    require(any(k.endswith('/ResourceBenchmark.cpp') for k in inputs), 'Missing fixture source')
    libraries = {}
    for image in receipt['images']:
        require(valid_hash(image.get('elf_sha256')) and valid_hash(image.get('binary_sha256'))
                and 0 < image['flash_bytes'] <= 65536, 'Invalid image identity/extent')
        objects = image['objects_sha256']
        require(set(objects) == {'ResourceBenchmark.o', 'StackCall.o', 'TelemetryAbi.o',
                                'SchemaFile.o', 'CommandsFile.o', 'ValuesFile.o'}
                and all(valid_hash(v) for v in objects.values()), 'Missing object identity')
        library = image['library_sources']
        require(bool(library) and all(valid_hash(v) for v in library.values()), 'Missing library hashes')
        require(libraries.setdefault(image['variant'], library) == library,
                'Source changed between optimizations')
        if image['variant'] == 'Baseline':
            require(image.get('baseline_commit') == BASELINE, 'Wrong baseline')
    require(receipt['summary'] == measurements(receipt), 'Stored summary differs from raw samples')


def self_test(receipt):
    verify(receipt)
    # Each change removes or contradicts evidence required for a success claim.
    def run(r):
        return r['runs']['Current-O2']

    mutations = [
        lambda r: r.update(completed=False),
        lambda r: r.update(restored_and_verified=False),
        lambda r: r.update(restored_sha256='0' * 64),
        lambda r: r.update(run_error='timeout'),
        lambda r: r['images'].pop(),
        lambda r: r['images'].__setitem__(1, r['images'][0]),
        lambda r: r['images'][0].update(elf_sha256='invalid'),
        lambda r: r['images'][0].update(flash_bytes=65537),
        lambda r: r['images'][0].update(baseline_commit='0' * 40),
        lambda r: r.update(build_inputs={}),
        lambda r: run(r).update(idle=False),
        lambda r: run(r).update(done=False),
        lambda r: run(r)['ready'].__setitem__(3, 300000000),
        lambda r: run(r)['timing'].pop(),
        lambda r: run(r)['timing'].__setitem__(1, run(r)['timing'][0]),
        lambda r: run(r)['timing'][0].__setitem__(2, 0),
        lambda r: run(r)['timing'][0].__setitem__(3, 0),
        lambda r: run(r)['stacks'].pop(),
        lambda r: run(r)['stacks'][-1].__setitem__(2, 511),
        lambda r: run(r)['files'].pop(),
        lambda r: run(r)['files'][0].__setitem__(3, 0),
        lambda r: run(r)['files'][2].__setitem__(4, 511),
        lambda r: r['summary']['Current-O2']['values_last'].update(stack_bytes=1),
    ]
    for n, mutate in enumerate(mutations):
        candidate = copy.deepcopy(receipt)
        mutate(candidate)
        try:
            verify(candidate)
        except (ValueError, KeyError, TypeError):
            continue
        raise RuntimeError(f'Invalid evidence accepted by negative control {n}')
    print(f'PASS: positive control and {len(mutations)} evidence rejection controls')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--receipt', type=Path, default=HERE / 'direct-cursor-receipt.json')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    receipt = json.loads(args.receipt.read_text(encoding='utf-8'))
    verify(receipt)
    if args.self_test:
        self_test(receipt)
    print('PASS: four images, 160 timing windows, 72 stack probes, 24 complete file transfers')


if __name__ == '__main__':
    main()
