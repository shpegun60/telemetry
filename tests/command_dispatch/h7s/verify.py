#!/usr/bin/env python3
"""Verify retained command-dispatch timing and restoration evidence offline."""
# Author: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
import argparse
import copy
import csv
import json
from pathlib import Path
import statistics


HERE = Path(__file__).resolve().parent
OPERATIONS = ('known', 'runtime_native', 'erased_scalar')
OPTIMIZATIONS = ('O2', 'Os')
ITERATIONS = 65536
REPETITIONS = 9


def valid_hash(value, length=64):
    return (isinstance(value, str) and len(value) == length
            and all(character in '0123456789abcdef' for character in value))


def expected_summary(rows):
    result = {}
    for optimization in OPTIMIZATIONS:
        result[optimization] = {}
        for operation, name in enumerate(OPERATIONS):
            samples = [int(row['cycles']) / int(row['calls']) for row in rows
                       if row['optimization'] == optimization
                       and int(row['operation']) == operation]
            result[optimization][name] = {
                'median_cycles': statistics.median(samples),
                'min_cycles': min(samples),
                'max_cycles': max(samples),
            }
    return result


def verify(receipt, rows):
    if receipt.get('completed') is not True or receipt.get('restored_and_verified') is not True:
        raise ValueError('Session did not complete and restore the board')
    if (receipt.get('backup_sha256') != receipt.get('restored_sha256')
            or not valid_hash(receipt.get('backup_sha256'))):
        raise ValueError('Restoration hashes differ or are missing')
    if not valid_hash(receipt.get('source_commit'), 40):
        raise ValueError('Missing source commit')
    if (receipt.get('board') != 'NUCLEO-H7S3L8'
            or not valid_hash(receipt.get('fixture_sha256'))
            or not valid_hash(receipt.get('runner_sha256'))):
        raise ValueError('Invalid fixture identity')

    images = {image['optimization']: image for image in receipt.get('images', [])}
    if set(images) != set(OPTIMIZATIONS) or len(images) != len(receipt['images']):
        raise ValueError('Missing or duplicate images')
    for image in images.values():
        if image.get('variant') != 'Current' or image.get('windows') != 27:
            raise ValueError('Unexpected image identity or coverage')
        if (not valid_hash(image.get('elf_sha256'))
                or not valid_hash(image.get('binary_sha256'))
                or not 0 < int(image.get('flash_bytes', 0)) <= 65536):
            raise ValueError('Invalid image hashes or size')
        objects = image.get('objects_sha256', {})
        if (set(objects) != {'CommandDispatchBenchmark.o'}
                or not all(valid_hash(value) for value in objects.values())):
            raise ValueError('Invalid fixture object identity')
        sources = image.get('library_sources', {})
        if not sources or not all(valid_hash(value) for value in sources.values()):
            raise ValueError('Invalid library source identity')

    observed = set()
    for row in rows:
        optimization = row['optimization']
        operation = int(row['operation'])
        repetition = int(row['repetition'])
        cycles = int(row['cycles'])
        calls = int(row['calls'])
        checksum = int(row['checksum'])
        if (optimization not in OPTIMIZATIONS or operation not in range(len(OPERATIONS))
                or repetition not in range(REPETITIONS)):
            raise ValueError('Unexpected timing parameters')
        key = (optimization, operation, repetition)
        if key in observed:
            raise ValueError('Duplicate timing window')
        observed.add(key)
        if (calls != ITERATIONS or checksum != ITERATIONS
                or not 0 < cycles < ITERATIONS * 1000):
            raise ValueError('Invalid timing interval or checksum')
        if row['elf_sha256'] != images[optimization]['elf_sha256']:
            raise ValueError('Timing row belongs to another image')
    expected = {(optimization, operation, repetition)
        for optimization in OPTIMIZATIONS
        for operation in range(len(OPERATIONS))
        for repetition in range(REPETITIONS)}
    if observed != expected:
        raise ValueError('Incomplete timing coverage')
    if receipt.get('summary') != expected_summary(rows):
        raise ValueError('Summary does not match timing rows')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--receipt', type=Path, default=HERE / 'receipt.json')
    parser.add_argument('--samples', type=Path, default=HERE / 'samples.csv')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    receipt = json.loads(args.receipt.read_text(encoding='utf-8'))
    with args.samples.open(newline='', encoding='utf-8') as stream:
        rows = list(csv.DictReader(stream))
    verify(receipt, rows)
    if args.self_test:
        mutants = []
        mutants.append((copy.deepcopy(receipt), copy.deepcopy(rows[:-1])))
        mutants.append((copy.deepcopy(receipt), copy.deepcopy(rows + [rows[0]])))
        bad = copy.deepcopy(rows); bad[0]['checksum'] = str(ITERATIONS - 1)
        mutants.append((copy.deepcopy(receipt), bad))
        bad = copy.deepcopy(rows); bad[0]['cycles'] = '0'
        mutants.append((copy.deepcopy(receipt), bad))
        bad = copy.deepcopy(rows); bad[0]['elf_sha256'] = '0' * 64
        mutants.append((copy.deepcopy(receipt), bad))
        bad = copy.deepcopy(receipt); bad['restored_and_verified'] = False
        mutants.append((bad, copy.deepcopy(rows)))
        bad = copy.deepcopy(receipt); bad['restored_sha256'] = '0' * 64
        mutants.append((bad, copy.deepcopy(rows)))
        bad = copy.deepcopy(receipt); bad['summary']['O2']['known']['median_cycles'] += 1
        mutants.append((bad, copy.deepcopy(rows)))
        bad = copy.deepcopy(receipt); del bad['images'][0]['objects_sha256']['CommandDispatchBenchmark.o']
        mutants.append((bad, copy.deepcopy(rows)))
        for bad_receipt, bad_rows in mutants:
            try:
                verify(bad_receipt, bad_rows)
            except (KeyError, ValueError):
                continue
            raise RuntimeError('An invalid evidence record was accepted')
        print(f'{len(mutants)} evidence mutation controls rejected')
    print(f'{len(rows)} timing windows and exact firmware restoration verified')


if __name__ == '__main__':
    main()
