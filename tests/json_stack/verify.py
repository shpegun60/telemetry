#!/usr/bin/env python3
"""Offline coverage, JSON-value and restoration checks for the stack fixture."""
# Author: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import struct

HERE = Path(__file__).resolve().parent
CASES = ('control512', 'schema_native', 'schema_custom', 'values_f32', 'values_f64',
         'values_integer', 'truncated_schema', 'truncated_values', 'null_buffer')


def checksum(text):
    value = 2166136261
    for byte in text.encode('ascii'):
        value = ((value ^ byte)*16777619) & 0xffffffff
    return value


def valid_hash(text):
    return isinstance(text, str) and len(text) == 64 and all(c in '0123456789abcdef' for c in text)


def json_object(pairs):
    result = dict(pairs)
    if len(result) != len(pairs):
        raise ValueError('Duplicate JSON keys')
    return result


def reject_constant(text):
    raise ValueError('Non-JSON numeric literal: '+text)


def verify_artifacts(receipt, root):
    for image in receipt['images']:
        directory = root/'Current'/image['optimization']
        expected = dict(image['objects_sha256'], **{'benchmark.elf': image['elf_sha256'], 'benchmark.bin': image['binary_sha256']})
        for name, digest in expected.items():
            if hashlib.sha256((directory/name).read_bytes()).hexdigest() != digest:
                raise ValueError('Retained artifact mismatch: '+str(directory/name))


def number(sample, f32):
    tiny, small, big = (2.0**-149, 2.0**-126, float.fromhex('0x1.fffffep127')) if f32 else (
        2.0**-1074, 2.0**-1022, float.fromhex('0x1.fffffffffffffp1023'))
    values = [0.0, -0.0, 1.0, -1.0, tiny, -tiny, small, -small, big, -big,
              1.2345678901234567, 0.00001, 0.0001, 1e20, 1e-20, 9999999.5, -12.7, 230.125,
              math.inf, -math.inf, math.nan]
    value = values[sample]
    return struct.unpack('<f', struct.pack('<f', value))[0] if f32 else value


def verify_json(row):
    case, text = row['case'], row['json']
    if row['length'] != len(text.encode('ascii')) or row['checksum'] != checksum(text):
        raise ValueError('Output length/checksum mismatch')
    if case == 0 or case >= 6:
        if text:
            raise ValueError('Control/truncated calls must return length zero')
        return
    data = json.loads(text, object_pairs_hook=json_object, parse_constant=reject_constant)
    if case in (3, 4):
        expected = number(row['sample'], case == 3)
        # Preserve the sign of JSON -0 for the floating-point cases.
        data = json.loads(text, parse_int=float, object_pairs_hook=json_object, parse_constant=reject_constant)
        if set(data) != {'v'} or len(data['v']) != 1:
            raise ValueError('Unexpected value shape')
        actual = data['v'][0]
        if not math.isfinite(expected):
            if actual is not None:
                raise ValueError('Non-finite values must serialize to null')
        else:
            fmt = '<f' if case == 3 else '<d'
            if type(actual) not in (int, float) or struct.pack(fmt, actual) != struct.pack(fmt, expected):
                raise ValueError('Floating value did not round trip')
    elif case == 5:
        if data != {'v': [2**64-1, -2**63, 2**32-1, -2**31, True]} or data['v'][-1] is not True:
            raise ValueError('Integer values lost precision')
    else:
        if len(data['schema']) != 8 or len(data['catalogs']) != 1:
            raise ValueError('Unexpected schema shape')
        catalog = data['catalogs'][0]
        if catalog['id'] != 0 or catalog['name'] != 'v':
            raise ValueError('Unexpected catalog')
        fields = catalog['fields']
        expected = [('f32', 'f32', 'V', None, None, 0), ('f64', 'f64', '', None, None, 0),
                    ('u64', 'u64', '', None, None, 0), ('bool', 'bool', '', False, True, False)] if case == 1 else [
                    ('f32', 'f32"\\\n', 'V', -999.5, 1234.75, 230.125),
                    ('f64', 'f64', '', -1.7e308, 1.7e308, 1.2345678901234567),
                    ('u16', 'mode', '', 0, 2, 0)]
        if len(fields) != len(expected):
            raise ValueError('Missing schema fields')
        for i, (field, values) in enumerate(zip(fields, expected)):
            if field['id'] != i or field['i'] != i or field['w'] is not False or tuple(field[k] for k in ('t', 'n', 'u', 'min', 'max', 'default')) != values:
                raise ValueError('Incorrect schema metadata')
        if case == 2 and fields[2]['enum'] != {'0': 'Off', '1': 'Auto', '2': 'Manual'}:
            raise ValueError('Incorrect enum dictionary')
        if case == 1 and any(type(fields[3][key]) is not bool for key in ('min', 'max', 'default')):
            raise ValueError('Boolean schema metadata lost its type')


def verify_rows(image, rows):
    expected = {(case, sample, pattern, rep) for case in range(9)
        for sample in range(21 if case in (3, 4) else 1) for pattern in range(2) for rep in range(3)}
    observed = set()
    for row in rows:
        key = tuple(row[k] for k in ('case', 'sample', 'pattern', 'repetition'))
        if key not in expected or key in observed:
            raise ValueError('Unexpected/duplicate stack measurement')
        observed.add(key)
        if row['optimization'] != image['optimization'] or row['elf_sha256'] != image['elf_sha256']:
            raise ValueError('Measurement belongs to another image')
        if not isinstance(row['used'], int) or not 0 <= row['used'] < 16384 - 256:
            raise ValueError('Stack guard margin exhausted')
        if row['case'] == 0 and not 512 <= row['used'] <= 1024:
            raise ValueError('512-byte control was not detected')
        verify_json(row)
    if observed != expected:
        raise ValueError('Missing stack measurements')


def verify(receipt):
    if receipt['completed'] is not True or receipt['restored_and_verified'] is not True:
        raise ValueError('Session did not complete and restore')
    if not valid_hash(receipt['backup_sha256']) or receipt['backup_sha256'] != receipt['restored_sha256']:
        raise ValueError('Restoration hashes differ')
    images = {image['optimization']: image for image in receipt['images']}
    if set(images) != {'O2', 'Os'} or len(receipt['images']) != 2 or len(receipt['samples']) != 588:
        raise ValueError('Incomplete build matrix')
    for opt, image in images.items():
        if image['device'] != dict(clock_hz=600000000, stack_bytes=16384, guard_bytes=256):
            raise ValueError('Unexpected device/stack')
        if image['windows'] != 294 or not 0 < image['flash_bytes'] <= 65536:
            raise ValueError('Incomplete or oversized image')
        if not valid_hash(image['elf_sha256']) or not valid_hash(image['binary_sha256']):
            raise ValueError('Invalid image identity')
        if set(image['objects_sha256']) != {'JsonStack.o', 'StackCall.o', 'TelemetryJson.o'} or not all(valid_hash(h) for h in image['objects_sha256'].values()):
            raise ValueError('Invalid object identities')
        verify_rows(image, [r for r in receipt['samples'] if r['optimization'] == opt])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--receipt', type=Path, default=HERE/'receipt.json')
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--artifacts', type=Path, help='Check original local object, ELF and binary files too')
    args = parser.parse_args()
    receipt = json.loads(args.receipt.read_text(encoding='utf-8'))
    verify(receipt)
    if args.artifacts:
        verify_artifacts(receipt, args.artifacts)
        print('Retained object and image bytes match')
    if args.self_test:
        mutants = []
        bad = copy.deepcopy(receipt); bad['samples'].pop(); mutants.append(bad)
        bad = copy.deepcopy(receipt); bad['samples'][1] = bad['samples'][0]; mutants.append(bad)
        bad = copy.deepcopy(receipt); bad['samples'][0]['used'] = 0; mutants.append(bad)
        bad = copy.deepcopy(receipt); bad['samples'][0]['used'] = 16384; mutants.append(bad)
        bad = copy.deepcopy(receipt); bad['samples'][0]['checksum'] ^= 1; mutants.append(bad)
        bad = copy.deepcopy(receipt); bad['samples'][0]['elf_sha256'] = '0'*64; mutants.append(bad)
        bad = copy.deepcopy(receipt); bad['restored_and_verified'] = False; mutants.append(bad)
        bad = copy.deepcopy(receipt); bad['restored_sha256'] = '0'*64; mutants.append(bad)
        bad = copy.deepcopy(receipt); row = next(r for r in bad['samples'] if r['case'] == 3)
        row.update(json='{"v":[7]}', length=9, checksum=checksum('{"v":[7]}')); mutants.append(bad)
        bad = copy.deepcopy(receipt); row = next(r for r in bad['samples'] if r['case'] == 3 and r['sample'] == 2)
        row.update(json='{"v":[true]}', length=12, checksum=checksum('{"v":[true]}')); mutants.append(bad)
        for bad in mutants:
            try:
                verify(bad)
            except (ValueError, KeyError):
                continue
            raise RuntimeError('Invalid evidence accepted')
        print(f'{len(mutants)} mutation controls rejected')
        if args.artifacts:
            bad = copy.deepcopy(receipt); bad['images'][0]['objects_sha256']['TelemetryJson.o'] = '0'*64
            try:
                verify_artifacts(bad, args.artifacts)
            except ValueError:
                print('Changed object identity rejected against retained bytes')
            else:
                raise RuntimeError('Incorrect artifact identity accepted')
    print(f'{len(receipt["samples"])} stack measurements, JSON results and restoration verified')
    for case, name in enumerate(CASES):
        print(name + ': ' + ', '.join(f'{opt} {max(r["used"] for r in receipt["samples"] if r["case"] == case and r["optimization"] == opt)} B' for opt in ('O2', 'Os')))


if __name__ == '__main__':
    main()
