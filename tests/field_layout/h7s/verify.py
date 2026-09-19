#!/usr/bin/env python3
"""Verify retained timing coverage, checksums and firmware restoration offline."""
# Author: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
import argparse
import copy
import csv
import json
from pathlib import Path
import runpy

HERE = Path(__file__).resolve().parent
RUNNER = runpy.run_path(str(HERE/'run.py'))


def verify(receipt, rows):
    if receipt.get('completed') is not True or receipt.get('restored_and_verified') is not True:
        raise ValueError('Session did not complete and restore the board')
    if receipt.get('backup_sha256') != receipt.get('restored_sha256') or len(receipt.get('backup_sha256', '')) != 64:
        raise ValueError('Restoration hashes differ or are missing')
    images = {(x['variant'], x['optimization']): x for x in receipt['images']}
    if len(images) != len(receipt['images']) or not images:
        raise ValueError('Missing/duplicate images')
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
    args = parser.parse_args()
    receipt = json.loads(args.receipt.read_text())
    with args.samples.open(newline='') as stream:
        rows = list(csv.DictReader(stream))
    verify(receipt, rows)
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
        for bad_receipt, bad_rows in mutants:
            try:
                verify(bad_receipt, bad_rows)
            except (ValueError, KeyError):
                continue
            raise RuntimeError('An invalid evidence record was accepted')
        print(f'{len(mutants)} evidence mutation controls rejected')
    print(f'{len(rows)} timing windows and firmware restoration verified')


if __name__ == '__main__':
    main()
