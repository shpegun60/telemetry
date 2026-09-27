#!/usr/bin/env python3
"""Offline receipt coverage/checksum controls; not a new run of current firmware.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""
import argparse
import copy
import json
from pathlib import Path
import re
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from golden import fixture


def expected_checksums():
    data = fixture('edge')
    state, offsets = 0x93a17, []
    for _ in range(256):
        state = (state * 1664525 + 1013904223) & 0xffffffff
        offsets.append(state % len(data))
    def token(offset, size):
        chunk = data[offset:offset + size]
        return len(chunk) + chunk[0] + chunk[-1]
    result = [sum(token(offsets[i & 255], size) for i in range(4096)) for size in (16, 64, 256)]
    result += [128 * sum(token(i, size) for i in range(0, len(data), size)) for size in (64, 256)]
    return result


def verify(receipt):
    if not receipt['completed'] or not receipt['restored_and_verified']:
        raise ValueError('Incomplete or unrestored run')
    if receipt['board'] != 'NUCLEO-H7S3L8' or receipt['backup_sha256'] != receipt['restored_sha256']:
        raise ValueError('Unexpected board or restore')
    if len(receipt['images']) != 2 or {i['optimization'] for i in receipt['images']} != {'O2', 'Os'}:
        raise ValueError('Image matrix incomplete')
    if set(receipt['runs']) != {'O2', 'Os'}:
        raise ValueError('Run matrix incomplete')
    checksums = expected_checksums()
    for image in receipt['images']:
        if not 0 < image['flash_bytes'] <= 65536 or not image['library_sources'] or not image['objects_sha256']:
            raise ValueError('Image provenance incomplete')
        for digest in [image['elf_sha256'], image['binary_sha256'], *image['library_sources'].values(), *image['objects_sha256'].values()]:
            if re.fullmatch(r'[0-9a-f]{64}', digest) is None:
                raise ValueError('Malformed artifact digest')
        report = receipt['runs'][image['optimization']]
        if report['ready'] != [600000000, 1486, 744] or report['check'] != 0 or not report['done']:
            raise ValueError('Device setup or byte check failed')
        expected = {(v, p, r) for v in range(2) for p in range(5) for r in range(5)}
        for variant, profile, repeat, iterations, cycles, checksum in report['timing']:
            key = (variant, profile, repeat)
            if key not in expected or iterations != (4096 if profile < 3 else 128) or not 0 < cycles < 100000000 or checksum != checksums[profile]:
                raise ValueError('Measurement missing/duplicate/incorrect')
            expected.remove(key)
        if expected:
            raise ValueError('Missing measurement')


def self_test(receipt):
    mutations = [
        lambda r: r.update(completed=False), lambda r: r.update(restored_and_verified=False),
        lambda r: r.update(restored_sha256='0' * 64), lambda r: r.update(board='different'),
        lambda r: r['images'].pop(), lambda r: r['images'][0].update(library_sources={}),
        lambda r: r['images'][0].update(elf_sha256='bad'), lambda r: r['runs'].pop('Os'),
        lambda r: r['runs']['O2'].update(check=1), lambda r: r['runs']['O2'].update(done=False),
        lambda r: r['runs']['O2']['ready'].__setitem__(0, 1),
        lambda r: r['runs']['O2']['timing'].pop(),
        lambda r: r['runs']['O2']['timing'].append(r['runs']['O2']['timing'][0]),
        lambda r: r['runs']['O2']['timing'][0].__setitem__(5, 0),
        lambda r: r['runs']['O2']['timing'][0].__setitem__(4, 0),
        lambda r: r['runs']['O2']['timing'][0].__setitem__(3, 1),
    ]
    for mutate in mutations:
        altered = copy.deepcopy(receipt)
        mutate(altered)
        try:
            verify(altered)
        except ValueError:
            continue
        raise AssertionError('Receipt mutation was accepted')
    print(f'H7S receipt: {len(mutations)} mutation controls passed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--receipt', type=Path, default=Path(__file__).with_name('receipt.json'))
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    receipt = json.loads(args.receipt.read_text())
    verify(receipt)
    if args.self_test:
        self_test(receipt)
    print('Retained descriptor receipt is internally consistent')
