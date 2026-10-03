#!/usr/bin/env python3
"""Verify archived Stage 08 measurements; optionally verify local image hashes.

Authors: Ruslan Kovtun (shpegun60), codexAi. MIT license.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path

ALIGNMENTS = {'Natural': None, 'Aligned8': 8, 'Aligned32': 32}
LOCAL_BUDGETS = {'Local' + str(size): size for size in (0, 16, 32, 64)}
DISPATCH_SNAPSHOTS = ('Before', 'Local', 'Pointer', 'Boundary')
DISPATCH_VARIANTS = (*DISPATCH_SNAPSHOTS, 'Context')
LAYOUTS = {'Natural': [24, 4, 20, 4, 24, 4], 'Aligned8': [24, 8, 24, 8, 24, 8], 'Aligned32': [32, 32, 32, 32, 32, 32]}
LAYOUTS.update({name: LAYOUTS['Natural'] for name in LOCAL_BUDGETS})
LAYOUTS.update({name: LAYOUTS['Natural'] for name in DISPATCH_VARIANTS})
DIRECT_LAYOUTS = {name: [28, 4, 20, 4, 24, 4] for name in ALIGNMENTS}
DIRECT_LAYOUTS['Aligned8'] = [32, 8, 24, 8, 24, 8]
DIRECT_LAYOUTS['Aligned32'] = [32, 32, 32, 32, 32, 32]
CHECKSUMS = [21, 1, 1, 38, 21, 1, 1, 17, 17, 1, 1, 1, 1, 17, 4, 1, 17, 17, 17, 17, 17, 17, 1, 21, 1, 1, 36, 1, 1, 56, 52, 1, 1, 72, 84, 1, 1, 104]

def operation_count(receipt):
    return 38 if receipt.get('storage_probes', False) else 23 if receipt.get('components', False) else 13 if receipt.get('legacy', False) else 4

def verify(receipt):
    if not receipt['completed'] or not receipt['restored_and_verified']:
        raise ValueError('Run did not complete with a verified restore')
    if not receipt['images']:
        raise ValueError('No images were measured')
    keys = [image['variant'] + '-' + image['optimization'] for image in receipt['images']]
    variants = {image['variant'] for image in receipt['images']}
    if not variants <= set(ALIGNMENTS) or len(set(keys)) != len(keys) or set(keys) != {variant + '-' + optimization for variant in variants for optimization in ('O2', 'Os')}:
        raise ValueError('Incomplete or duplicate image matrix')
    if set(receipt['runs']) != set(keys):
        raise ValueError('Image/run mismatch')
    if receipt['board'] != 'NUCLEO-H7S3L8' or receipt['backup_sha256'] != receipt['restored_sha256']:
        raise ValueError('Board identity or restored Flash differs')
    if receipt.get('components', False) and (not receipt.get('legacy', False)):
        raise ValueError('Component measurements require the legacy controls')
    if receipt.get('storage_probes', False) and (not receipt.get('components', False)):
        raise ValueError('Storage measurements require component controls')
    for image in receipt['images']:
        key = image['variant'] + '-' + image['optimization']
        result = receipt['runs'][key]
        if receipt.get('storage_policy', False) and result.get('policy') != [image['local_bytes']]:
            raise ValueError('Runtime storage policy differs from the built configuration')
        if image['variant'] in LOCAL_BUDGETS and image.get('local_bytes') != LOCAL_BUDGETS[image['variant']]:
            raise ValueError('Storage policy differs from the selected variant')
        layouts = DIRECT_LAYOUTS if image.get('direct_contexts', False) else LAYOUTS
        if result['check'] != [0, 0] or result['ready'] != [600000000, 32768, *layouts[image['variant']], 16384]:
            raise ValueError('Wrong device/layout/correctness report: ' + key)
        legacy = receipt.get('legacy', False)
        if legacy and result['legacy_layout'] != [96, 32, 20, 4]:
            raise ValueError('Legacy layout differs from the RW32 baseline')
        expected = {(m, p, o, r) for m in range(3) for p in range(3) for o in range(operation_count(receipt)) for r in range(5) if o < 7 or (m == 0 and p == 0)}
        for memory, profile, op, rep, cycles, checksum in result['timing']:
            item = (memory, profile, op, rep)
            if item not in expected or not 0 < cycles < 100000000 or checksum != CHECKSUMS[op] * 16384:
                raise ValueError('Invalid/duplicate measurement: ' + str(item))
            expected.remove(item)
        if expected or not result['done']:
            raise ValueError('Missing measurements: ' + key)

CHECKS = {'verify': verify}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--receipt', type=Path, required=True)
    p.add_argument('--artifacts', type=Path)
    p.add_argument('--self-test', action='store_true')
    args = p.parse_args()
    receipt = json.loads(args.receipt.read_text())
    CHECKS['verify'](receipt)
    if receipt['backup_sha256'] != receipt['restored_sha256']:
        raise ValueError('Backup and restore hashes differ')
    if args.artifacts:
        for image in receipt['images']:
            folder = args.artifacts/image['variant']/image['optimization']
            for filename, key in (('benchmark.elf', 'elf_sha256'), ('benchmark.bin', 'binary_sha256')):
                actual = hashlib.sha256((folder/filename).read_bytes()).hexdigest()
                if actual != image[key]:
                    raise ValueError('Image hash mismatch: '+str(folder/filename))
            for relative, expected in image['library_sources'].items():
                actual = hashlib.sha256((args.artifacts/image['variant']/relative).read_bytes()).hexdigest()
                if actual != expected:
                    raise ValueError('Source hash mismatch: '+relative)
    if args.self_test:
        key = next(iter(receipt['runs']))
        mutations = [
            lambda r: r.update(completed=False),
            lambda r: r.update(restored_and_verified=False),
            lambda r: r['runs'][key].update(check=[1, 0]),
            lambda r: r['runs'][key]['ready'].__setitem__(0, 1),
            lambda r: r['runs'][key]['ready'].__setitem__(2, 1),
            lambda r: r['runs'][key]['timing'].pop(),
            lambda r: r['runs'][key]['timing'].append(r['runs'][key]['timing'][0]),
            lambda r: r['runs'][key]['timing'][0].__setitem__(4, 0),
            lambda r: r['runs'][key]['timing'][0].__setitem__(5, 0),
            lambda r: r['runs'][key].update(done=False),
            lambda r: r.update(images=[]),
            lambda r: r['images'].pop(),
            lambda r: r['images'].append(r['images'][0]),
            lambda r: r['runs'].pop(key),
            lambda r: r.update(restored_sha256='0'*64),
            lambda r: r.update(board='different'),
        ]
        if receipt.get('storage_policy', False):
            mutations += [
                lambda r: r['runs'][key].update(policy=[-1]),
                lambda r: r['images'][0].update(local_bytes=-1),
            ]
        if receipt.get('storage_probes', False):
            mutations.append(lambda r: r.update(components=False))
        if 'direct_contexts' in receipt['images'][0]:
            mutations.append(lambda r: r['images'][0].update(
                direct_contexts=not r['images'][0]['direct_contexts']))
        for mutation in mutations:
            broken = copy.deepcopy(receipt)
            mutation(broken)
            try:
                CHECKS['verify'](broken)
            except ValueError:
                continue
            raise ValueError('A corrupted receipt was accepted')
        print(f'{len(mutations)} altered receipts rejected')
    print('Stage 08 archived receipt verified; current-source execution is not implied')


if __name__ == '__main__':
    main()
