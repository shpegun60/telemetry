#!/usr/bin/env python3
"""Verify archived Stage 08 measurements; optionally verify local image hashes.

Authors: Ruslan Kovtun (shpegun60), codexAi. MIT license.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import runpy

CHECKS = runpy.run_path(str(Path(__file__).with_name('run.py')))


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
