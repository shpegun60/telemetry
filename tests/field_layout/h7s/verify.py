#!/usr/bin/env python3
"""Verify retained timing coverage, checksums and firmware restoration offline."""
# Author: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
import argparse
import copy
import csv
from datetime import datetime, timezone
import json
from pathlib import Path
import runpy

HERE = Path(__file__).resolve().parent
RUNNER = runpy.run_path(str(HERE/'run.py'))


def valid_hash(value):
    return isinstance(value, str) and len(value) == 64 and all(c in '0123456789abcdef' for c in value)


def verify_artifacts(receipt, root, record=False):
    for image in receipt['images']:
        if image['variant'] not in RUNNER['VARIANTS'] or image['optimization'] not in ('O2', 'Os'):
            raise ValueError('Unknown build variant')
        directory = root/image['variant']/image['optimization']
        for filename, key in (('benchmark.elf', 'elf_sha256'), ('benchmark.bin', 'binary_sha256')):
            if RUNNER['sha'](directory/filename) != image[key]:
                raise ValueError('Retained artifact differs from receipt: '+str(directory/filename))
        hashes = {name: RUNNER['sha'](directory/name) for name in ('Probe.o', 'Benchmark.o')}
        if record:
            if 'objects_sha256' in image and image['objects_sha256'] != hashes:
                raise ValueError('Existing object hashes differ')
            image['objects_sha256'] = hashes
        elif image['objects_sha256'] != hashes:
            raise ValueError('Retained objects differ from receipt')


def verify(receipt, rows):
    if receipt.get('completed') is not True or receipt.get('restored_and_verified') is not True:
        raise ValueError('Session did not complete and restore the board')
    if receipt.get('backup_sha256') != receipt.get('restored_sha256') or not valid_hash(receipt.get('backup_sha256')):
        raise ValueError('Restoration hashes differ or are missing')
    images = {(x['variant'], x['optimization']): x for x in receipt['images']}
    if len(images) != len(receipt['images']) or not images:
        raise ValueError('Missing/duplicate images')
    for image in images.values():
        objects = image.get('objects_sha256', {})
        if set(objects) != {'Probe.o', 'Benchmark.o'} or not all(valid_hash(h) for h in objects.values()):
            raise ValueError('Missing or invalid object hashes')
        if not valid_hash(image['elf_sha256']) or not valid_hash(image['binary_sha256']):
            raise ValueError('Invalid image hashes')
    for comparison in receipt.get('probe_equivalence', []):
        variants, opt = comparison['variants'], comparison['optimization']
        if len(variants) < 2 or len(set(variants)) != len(variants):
            raise ValueError('Invalid equivalence group')
        if len({images[v, opt]['objects_sha256']['Probe.o'] for v in variants}) != 1:
            raise ValueError('Claimed equivalent Probe objects differ')
    observed = set()
    for row in rows:
        variant, opt = row['variant'], row['optimization']
        image = images[variant, opt]
        if image['elf_sha256'] != row['elf_sha256']:
            raise ValueError('Row belongs to a different image')
        device = image['device']
        if device['clock_hz'] != 600000000 or device['data_cache_bytes'] != 32768:
            raise ValueError('Unexpected device geometry')
        memory, count, profile, op, repetition = [int(row[k]) for k in ('memory', 'count', 'profile', 'operation', 'repetition')]
        if memory not in range(3) or count != (128 if memory < 2 else 1024) or profile not in range(6) or op not in range(7) or repetition not in range(5):
            raise ValueError('Unexpected test parameters')
        if op >= 5 and (memory != 0 or profile != 0):
            raise ValueError('Unexpected known-ID control')
        if int(row['calls']) != 32768 or not 0 < int(row['cycles']) < 327680000:
            raise ValueError('Invalid timing interval')
        key = (variant, opt, memory, count, profile, op, repetition)
        if key in observed:
            raise ValueError('Duplicate timing window')
        observed.add(key)
        base = device['flash_base' if memory == 0 else 'ram_base']
        expected = RUNNER['expected_sum'](op, RUNNER['sequence'](profile, count), base, RUNNER['LAYOUTS'][variant][0])
        if int(row['checksum']) != expected:
            raise ValueError('Incorrect result checksum')
    expected = {(v, o, m, 128 if m < 2 else 1024, p, op, rep)
        for v, o in images for m in range(3) for p in range(6) for op in range(7) for rep in range(5)
        if op < 5 or (m == 0 and p == 0)}
    if observed != expected or any(image['windows'] != 460 for image in images.values()):
        raise ValueError('Incomplete timing coverage')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--receipt', type=Path, default=HERE/'receipt.json')
    parser.add_argument('--samples', type=Path, default=HERE/'samples.csv')
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--artifacts', type=Path, help='Also verify retained local ELF, binary and object bytes')
    parser.add_argument('--record-objects', type=Path, help='Supplement an old receipt into a new file; requires --artifacts')
    parser.add_argument('--compare-probes', nargs='+', choices=RUNNER['VARIANTS'], help='Record an explicit Probe equivalence claim')
    args = parser.parse_args()
    receipt = json.loads(args.receipt.read_text())
    with args.samples.open(newline='') as stream:
        rows = list(csv.DictReader(stream))
    if args.record_objects:
        if args.artifacts is None or args.record_objects.exists():
            parser.error('Recording requires --artifacts and a new output filename')
        verify_artifacts(receipt, args.artifacts, record=True)
        receipt['object_hash_provenance'] = {
            'recorded_at': datetime.now(timezone.utc).isoformat(),
            'method': 'Supplemented from retained local objects after matching original ELF and binary hashes; no hardware rerun.',
        }
        if args.compare_probes:
            receipt['probe_equivalence'] = [dict(optimization=opt, variants=args.compare_probes)
                for opt in sorted({x['optimization'] for x in receipt['images']})]
    elif args.compare_probes:
        parser.error('--compare-probes requires --record-objects')
    verify(receipt, rows)
    if args.artifacts:
        verify_artifacts(receipt, args.artifacts)
        print('Retained ELF, binary and object bytes match the receipt')
    if args.record_objects:
        args.record_objects.write_text(json.dumps(receipt, indent=2)+'\n', encoding='utf-8')
    if args.self_test:
        mutants = []
        mutants.append((copy.deepcopy(receipt), copy.deepcopy(rows[:-1])))
        mutants.append((copy.deepcopy(receipt), copy.deepcopy(rows + [rows[0]])))
        bad = copy.deepcopy(rows); bad[0]['checksum'] = str(int(bad[0]['checksum']) ^ 1)
        mutants.append((copy.deepcopy(receipt), bad))
        bad = copy.deepcopy(rows); bad[0]['cycles'] = '0'
        mutants.append((copy.deepcopy(receipt), bad))
        bad = copy.deepcopy(receipt); bad['restored_and_verified'] = False
        mutants.append((bad, copy.deepcopy(rows)))
        bad = copy.deepcopy(receipt); bad['restored_sha256'] = '0'*64
        mutants.append((bad, copy.deepcopy(rows)))
        bad = copy.deepcopy(rows); bad[0]['elf_sha256'] = '0'*64
        mutants.append((copy.deepcopy(receipt), bad))
        bad = copy.deepcopy(receipt); del bad['images'][0]['objects_sha256']['Probe.o']
        mutants.append((bad, copy.deepcopy(rows)))
        bad = copy.deepcopy(receipt); bad['images'][0]['objects_sha256']['Benchmark.o'] = 'not-a-hash'
        mutants.append((bad, copy.deepcopy(rows)))
        if receipt.get('probe_equivalence'):
            bad = copy.deepcopy(receipt)
            claim = bad['probe_equivalence'][0]
            next(x for x in bad['images'] if x['variant'] == claim['variants'][0]
                and x['optimization'] == claim['optimization'])['objects_sha256']['Probe.o'] = '0'*64
            mutants.append((bad, copy.deepcopy(rows)))
        for bad_receipt, bad_rows in mutants:
            try:
                verify(bad_receipt, bad_rows)
            except (ValueError, KeyError):
                continue
            raise RuntimeError('An invalid evidence record was accepted')
        print(f'{len(mutants)} evidence mutation controls rejected')
        if args.artifacts:
            bad = copy.deepcopy(receipt)
            bad['images'][0]['objects_sha256']['Probe.o'] = '0'*64
            try:
                verify_artifacts(bad, args.artifacts)
            except ValueError:
                print('Changed object identity rejected against retained bytes')
            else:
                raise RuntimeError('Incorrect retained object identity accepted')
    print(f'{len(rows)} timing windows and firmware restoration verified')


if __name__ == '__main__':
    main()
