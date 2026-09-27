"""Golden and malformed-input checks for the independent bounded v3 parser.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""
from dataclasses import replace
from pathlib import Path
import struct
from golden import fixture, type_record, struct_type, u32
from parser import parse, fingerprint, InvalidDescriptor, Limits


def rehash(data):
    data = bytearray(data)
    struct.pack_into('<Q', data, 16, fingerprint(data))
    return bytes(data)


def edit(data, offset, size, value):
    data = bytearray(data)
    data[offset:offset + size] = value.to_bytes(size, 'little')
    return rehash(data)


def check(produced=None):
    checks = 0

    def bad(data, reason, limits=Limits()):
        nonlocal checks
        try:
            parse(data, limits)
        except InvalidDescriptor as error:
            assert reason in str(error), (reason, str(error))
        else:
            raise AssertionError('Accepted invalid descriptor: ' + reason)
        checks += 1

    for name in ('mixed', 'edge', 'empty'):
        expected = fixture(name)
        frozen = bytes.fromhex(Path(__file__).with_name(name + '.hex').read_text())
        assert expected == frozen, 'Independent fixture disagrees with frozen ' + name
        if produced:
            assert (Path(produced) / (name + '.bin')).read_bytes() == frozen, 'C++ bytes differ: ' + name
        parse(frozen)
        checks += 1

    data = fixture('edge')
    model = parse(data)
    types, catalogs, endpoints = model['types'], model['catalogs'], model['endpoints']
    assert model['offsets'] == (64, 797, 992)
    assert model['total'] == 1486 and model['fingerprint'] == 0xc380b060ffc7ce47
    assert types[20]['entries'] == [(-1000, 'N\u00e9gatif'), (2000, 'Haut')]
    assert types[21]['entries'] == [(0xffffffffffffffff, 'Max')]
    assert types[19]['wire'] == 17 and types[19]['depth'] == 3 and types[19]['nodes'] == 14
    assert endpoints[0][7]['type'] == endpoints[1][0]['type'] == endpoints[2][0]['type'] == endpoints[2][0]['response'] == 15
    assert endpoints[0][-1]['capabilities'] == 3  # empty setter slot still writable
    assert catalogs[0][1]['count'] == 0 and endpoints[0][8]['id'] == 2 << 16

    for offset in range(len(data)):
        altered = bytearray(data)
        altered[offset] ^= 0x80
        # Hash/header corruption must always be rejected, whatever changed.
        try:
            parse(altered)
        except InvalidDescriptor:
            checks += 1
        else:
            raise AssertionError(f'Undetected mutation at byte {offset}')

    for offset, size, value, reason in (
        (4, 2, 4, 'header version'), (6, 2, 1, 'header version'), (8, 2, 63, 'header version'),
        (10, 2, 1, 'header version'), (12, 4, len(data) + 1, 'total length'),
        (24, 4, 0xffffffff, 'type budget'), (28, 4, 0xffffffff, 'catalog budget'),
        (32, 4, 0xffffffff, 'endpoint budget'), (52, 4, 65, 'section offsets'),
        (56, 4, 63, 'section offsets'), (60, 4, len(data) + 1, 'section offsets')):
        bad(edit(data, offset, size, value), reason)

    for item in types + sum(catalogs, []) + sum(endpoints, []):
        offset = item['offset']
        bad(edit(data, offset, 1, 255), 'record kind')
        bad(edit(data, offset + 1, 1, 2), 'record version')
        bad(edit(data, offset + 2, 2, 1), 'reserved bytes')
        bad(edit(data, offset + 4, 4, 0xffffffff), 'record length')

    bad(edit(data, types[2]['offset'] + 8, 4, 1), 'positional type ID')
    bad(edit(data, types[2]['offset'] + 12, 1, 7), 'type kind')
    bad(edit(data, types[2]['offset'] + 13, 1, 1), 'reserved bytes')
    bad(edit(data, types[2]['offset'] + 20, 1, 10), 'builtin scalar code')
    bad(edit(data, types[2]['offset'] + 16, 4, 2), 'wire size mismatch')
    bad(edit(data, types[20]['offset'] + 20, 4, 10), 'enum underlying integer')
    bad(edit(data, types[20]['offset'] + 24, 4, 65537), 'enum budget')
    bad(edit(data, types[20]['offset'] + 28, 2, 2000), 'enum code order')
    bad(edit(data, types[19]['offset'] + 24, 4, 19), 'backward type reference')
    bad(edit(data, types[19]['offset'] + 24, 4, 0), 'backward type reference')
    bad(edit(data, types[19]['offset'] + 20, 4, 257), 'struct member budget')
    bad(edit(data, types[17]['offset'] + 20, 4, 17), 'backward type reference')
    bad(edit(data, types[17]['offset'] + 20, 4, 0), 'backward type reference')
    bad(edit(data, types[17]['offset'] + 24, 4, 65537), 'array element budget')
    bad(edit(data, types[19]['offset'] + 16, 4, 0xffffffff), 'value wire budget')

    cat = catalogs[0][0]['offset']
    bad(edit(data, cat + 8, 1, 3), 'catalog category')
    bad(edit(data, cat + 9, 1, 1), 'reserved bytes')
    bad(edit(data, cat + 12, 4, 1), 'catalog group index')
    bad(edit(data, cat + 16, 4, 1), 'catalog first entry')
    bad(edit(data, cat + 20, 4, 65537), 'catalog entry count')
    bad(edit(data, cat + 24, 4, 0xffffffff), 'string budget')
    bad(edit(data, cat + 28, 1, 0), 'embedded NUL')
    bad(edit(data, cat + 28, 1, 0xff), 'invalid UTF-8')
    other = catalogs[0][1]['offset']
    altered = bytearray(data)
    altered[other + 28:other + 33] = b'motor'
    bad(rehash(altered), 'duplicate catalog name')
    ep = endpoints[0][0]['offset']
    bad(edit(data, ep + 8, 4, 1), 'endpoint positional ID')
    bad(edit(data, ep + 12, 4, 0), 'Void Field')
    bad(edit(data, ep + 12, 4, len(types)), 'endpoint type reference')
    bad(edit(data, ep + 16, 1, 5), 'Field capabilities')
    bad(edit(data, ep + 17, 1, 1), 'reserved bytes')
    bad(edit(data, endpoints[1][0]['offset'] + 12, 4, 6), 'request must')
    bad(edit(data, endpoints[2][0]['offset'] + 16, 4, 6), 'response must')
    bad(edit(data, ep + 20, 4, 6), 'extra or missing payload')  # would leave a metadata tail

    for parameter, value, reason in (
        ('descriptor_bytes', len(data) - 1, 'descriptor budget'), ('types', 21, 'type budget'),
        ('catalogs', 5, 'catalog budget'), ('endpoints', 1, 'endpoint budget'),
        ('string_bytes', 3, 'string budget'), ('members', 2, 'struct member budget'),
        ('array_elements', 1, 'array element budget'), ('enum_entries', 1, 'enum budget'),
        ('wire_bytes', 8, 'value wire budget'), ('depth', 1, 'type depth budget'),
        ('expanded_nodes', 3, 'expanded node budget')):
        bad(data, reason, replace(Limits(), **{parameter: value}))

    # Metadata identity includes all labels and capabilities, but changing an
    # endpoint label does not modify the shared Type or Service records.
    for offset in (cat + 28, ep + 24, types[19]['offset'] + 32, types[20]['offset'] + 34):
        altered = bytearray(data)
        altered[offset] = ord('Q')
        changed = rehash(altered)
        assert fingerprint(changed) != fingerprint(data)
        parse(changed)
        checks += 1
    changed = edit(data, ep + 16, 1, 3)
    parse(changed)
    assert changed[64:model['offsets'][1]] == data[64:model['offsets'][1]]
    service_start = endpoints[2][0]['offset']
    assert changed[service_start:] == data[service_start:]

    # Zero-byte types can create huge logical values. Both depth and expanded
    # nodes are independent limits, not proxies for a byte allocation.
    def types_only(extra):
        raw = bytearray(fixture('empty') + b''.join(extra))
        struct.pack_into('<I', raw, 12, len(raw))
        struct.pack_into('<I', raw, 24, 12 + len(extra))
        struct.pack_into('<II', raw, 56, len(raw), len(raw))
        return rehash(raw)
    extra = [struct_type(12, 0, [])]
    extra += [type_record(13, 4, 0, u32(12, 65536)), type_record(14, 4, 0, u32(13, 5))]
    bad(types_only(extra), 'expanded node budget')
    extra = [struct_type(12, 0, [])]
    extra += [type_record(i, 4, 0, u32(i - 1, 0)) for i in range(13, 45)]
    bad(types_only(extra), 'type depth budget')
    print(f'Independent descriptor parser: {checks} positive/rejection checks passed')
    return checks


if __name__ == '__main__':
    import sys
    check(sys.argv[1] if len(sys.argv) > 1 else None)
