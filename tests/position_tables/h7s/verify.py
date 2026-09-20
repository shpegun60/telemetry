#!/usr/bin/env python3
"""Validate command stride evidence without a board or serial dependency."""
# Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
import argparse
import copy
import csv
import hashlib
import json
from pathlib import Path
import statistics

HERE = Path(__file__).resolve().parent


def valid_hash(value, size=64):
    return isinstance(value, str) and len(value) == size and all(c in '0123456789abcdef' for c in value)


def checksum(count, profile):
    state, total = 0x19a753, 0
    for i in range(1024):
        state ^= state << 13 & 0xffffffff
        state ^= state >> 17
        state ^= state << 5 & 0xffffffff
        index = 0 if profile == 0 else i % count if profile == 1 else state % count
        total += index + 1
    return total * 32


def summary(rows):
    result = []
    for mem in (0, 1):
        for count in (128, 1024):
            for profile in range(3):
                medians = [statistics.median(int(r['cycles']) / int(r['calls']) for r in rows
                    if tuple(int(r[k]) for k in ('memory', 'count', 'profile', 'stride'))
                    == (mem, count, profile, stride)) for stride in (20, 24)]
                result.append(dict(memory=mem, count=count, profile=profile,
                                   natural20=medians[0], padded24=medians[1]))
    return result


def verify(receipt, rows):
    if receipt.get('completed') is not True or receipt.get('restored_and_verified') is not True:
        raise ValueError('Incomplete session or restoration')
    if (not valid_hash(receipt.get('backup_sha256'))
            or receipt['backup_sha256'] != receipt.get('restored_sha256')):
        raise ValueError('Firmware restoration differs')
    if receipt.get('board') != 'NUCLEO-H7S3L8' or not valid_hash(receipt.get('base_commit'), 40):
        raise ValueError('Missing board/source identity')
    if not receipt.get('sources') or not all(valid_hash(v) for v in receipt['sources'].values()):
        raise ValueError('Missing source hashes')
    images = {i['stride']: i for i in receipt['images']}
    if len(receipt['images']) != 2 or set(images) != {20, 24}:
        raise ValueError('Expected exactly two image layouts')
    for image in images.values():
        if (image['optimization'] != 'O2' or image['variant'] != 'Current'
                or not valid_hash(image['elf_sha256']) or not valid_hash(image['binary_sha256'])
                or not 0 < image['flash_bytes'] <= 65536
                or set(image['objects_sha256']) != {'CommandStride.o'}
                or not all(valid_hash(v) for v in image['objects_sha256'].values())
                or not image['library_sources']
                or not all(valid_hash(v) for v in image['library_sources'].values())):
            raise ValueError('Invalid image or build identity')
    # Only the candidate descriptor's reserved padding word may differ.
    left, right = (images[s]['library_sources'] for s in (20, 24))
    if set(left) != set(right):
        raise ValueError('Candidate library file sets differ')
    changed = [k.replace('\\', '/') for k in left if left[k] != right[k]]
    if changed != ['lib/telemetry/command/TelemetryCommand.h']:
        raise ValueError('Unexpected candidate library differences')
    observed = set()
    for row in rows:
        session, stride, mem, count, profile, repetition = (
            int(row[k]) for k in ('session', 'stride', 'memory', 'count', 'profile', 'repetition'))
        key = session, mem, count, profile, repetition
        if (session not in range(4) or stride != (20, 24, 24, 20)[session]
                or mem not in (0, 1) or count not in (128, 1024)
                or profile not in range(3) or repetition not in range(9) or key in observed):
            raise ValueError('Unexpected or duplicate measurement')
        observed.add(key)
        if (int(row['calls']) != 32768 or int(row['checksum']) != checksum(count, profile)
                or not 0 < int(row['cycles']) < 32768000
                or row['elf_sha256'] != images[stride]['elf_sha256']):
            raise ValueError('Invalid count, checksum or image association')
    if len(observed) != 432 or receipt['summary'] != summary(rows):
        raise ValueError('Missing measurements or incorrect summary')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--receipt', type=Path, default=HERE / 'receipt.json')
    parser.add_argument('--samples', type=Path, default=HERE / 'samples.csv')
    parser.add_argument('--rebuild', type=Path, default=HERE / 'rebuild.json')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    receipt = json.loads(args.receipt.read_text())
    if hashlib.sha256(args.samples.read_bytes()).hexdigest() != receipt['samples_sha256']:
        raise ValueError('Sample file hash differs')
    with args.samples.open(newline='') as stream:
        rows = list(csv.DictReader(stream))
    verify(receipt, rows)
    rebuild = json.loads(args.rebuild.read_text())
    if (rebuild.get('matches_retained_measurement') is not True
            or rebuild.get('restored_and_verified') is not True
            or rebuild.get('backup_sha256') != receipt['backup_sha256']
            or rebuild.get('restored_sha256') != receipt['restored_sha256']
            or not rebuild.get('sources')
            or not all(valid_hash(v) for v in rebuild['sources'].values())):
        raise ValueError('Invalid final-source rebuild record')
    rebuilt = {image['stride']: image for image in rebuild['images']}
    if len(rebuild['images']) != 2 or set(rebuilt) != {20, 24}:
        raise ValueError('Missing rebuilt candidates')
    for measured in receipt['images']:
        for key in ('elf_sha256', 'binary_sha256', 'objects_sha256'):
            if measured[key] != rebuilt[measured['stride']][key]:
                raise ValueError('Final-source artifact differs from measured image')
    if args.self_test:
        mutants = [(copy.deepcopy(receipt), rows[:-1]), (copy.deepcopy(receipt), rows + [rows[0]])]
        for key, value in (('cycles', '0'), ('checksum', '0'), ('stride', '24'), ('elf_sha256', '0' * 64)):
            changed = copy.deepcopy(rows); changed[0][key] = value
            mutants.append((copy.deepcopy(receipt), changed))
        for key, value in (('restored_and_verified', False), ('restored_sha256', '0' * 64)):
            changed = copy.deepcopy(receipt); changed[key] = value
            mutants.append((changed, rows))
        changed = copy.deepcopy(receipt); changed['summary'][0]['natural20'] += 1
        mutants.append((changed, rows))
        for bad_receipt, bad_rows in mutants:
            try:
                verify(bad_receipt, bad_rows)
            except (ValueError, KeyError):
                continue
            raise RuntimeError('Invalid evidence was accepted')
        print(f'{len(mutants)} evidence mutation controls rejected')
    print('432 timing windows, final rebuild identity and exact restoration verified')


if __name__ == '__main__':
    main()
