#!/usr/bin/env python3
"""Check recorded target coverage/provenance, not a new hardware run. MIT."""
import argparse
import copy
import json
from pathlib import Path
import re


def verify(receipt):
    if not receipt['completed'] or not receipt['restored_and_verified']:
        raise ValueError('Incomplete or unrestored run')
    if receipt['board'] != 'NUCLEO-H7S3L8' or receipt['backup_sha256'] != receipt['restored_sha256']:
        raise ValueError('Device/restore mismatch')
    if len(receipt['images']) != 2 or {i['optimization'] for i in receipt['images']} != {'O2', 'Os'}:
        raise ValueError('Incomplete images')
    if set(receipt['runs']) != {'O2', 'Os'}:
        raise ValueError('Incomplete run matrix')
    for image in receipt['images']:
        if not 0 < image['flash_bytes'] <= 65536 or not image['library_sources'] or not image['objects_sha256']:
            raise ValueError('Missing build provenance')
        for digest in [image['elf_sha256'], image['binary_sha256'], *image['library_sources'].values(), *image['objects_sha256'].values()]:
            if re.fullmatch(r'[0-9a-f]{64}', digest) is None:
                raise ValueError('Malformed digest')
        report = receipt['runs'][image['optimization']]
        if report['ready'] != [600000000, 24, 32] or report['check'] != [0, 4300] or not report['done']:
            raise ValueError('Device correctness/setup failure')
        expected = {(p, r) for p in range(8) for r in range(5)}
        for profile, repeat, iterations, cycles, checksum in report['timing']:
            key = (profile, repeat)
            if key not in expected or iterations != (256 if profile == 6 else 4096):
                raise ValueError('Measurement coverage mismatch')
            if not 0 < cycles < 100000000 or checksum != (24, 24, 24, 24, 29, 29, 4120, 8)[profile] * iterations:
                raise ValueError('Invalid cycles/checksum')
            expected.remove(key)
        if expected:
            raise ValueError('Missing measurements')


def self_test(receipt):
    mutations = [
        lambda r: r.update(completed=False), lambda r: r.update(restored_and_verified=False),
        lambda r: r.update(restored_sha256='0'*64), lambda r: r.update(board='wrong'),
        lambda r: r['images'].pop(), lambda r: r['images'][0].update(library_sources={}),
        lambda r: r['images'][0].update(binary_sha256='bad'), lambda r: r['runs'].pop('Os'),
        lambda r: r['runs']['O2'].update(check=1), lambda r: r['runs']['O2'].update(done=False),
        lambda r: r['runs']['O2']['ready'].__setitem__(0, 1), lambda r: r['runs']['O2']['timing'].pop(),
        lambda r: r['runs']['O2']['timing'].append(r['runs']['O2']['timing'][0]),
        lambda r: r['runs']['O2']['timing'][0].__setitem__(4, 0),
        lambda r: r['runs']['O2']['timing'][0].__setitem__(3, 0),
        lambda r: r['runs']['O2']['timing'][0].__setitem__(2, 1),
    ]
    for mutate in mutations:
        altered = copy.deepcopy(receipt); mutate(altered)
        try:
            verify(altered)
        except ValueError:
            continue
        raise AssertionError('Receipt mutation accepted')
    print(f'H7S Bind/Exchange: {len(mutations)} receipt mutation controls passed')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--receipt', type=Path, default=Path(__file__).with_name('receipt.json'))
    p.add_argument('--self-test', action='store_true')
    a = p.parse_args()
    r = json.loads(a.receipt.read_text()); verify(r)
    if a.self_test: self_test(r)
    print('Recorded Stage 11 device evidence is internally consistent')
