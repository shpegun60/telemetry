"""Independent explicit fixtures for descriptor v3.0 (not producer-generated).

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""
import struct
from parser import fingerprint


def u32(*values):
    return struct.pack('<' + 'I' * len(values), *values)


def text(value):
    encoded = value.encode('utf-8')
    return u32(len(encoded)) + encoded


def record(kind, payload):
    return struct.pack('<BBHI', kind, 1, 0, len(payload)) + payload


def type_record(index, kind, wire, tail=b''):
    return record(1, u32(index) + bytes((kind, 0, 0, 0)) + u32(wire) + tail)


def struct_type(index, wire, members):
    return type_record(index, 3, wire, u32(len(members)) + b''.join(u32(t) + text(n) for t, n in members))


def enum_type(index, base, width, entries):
    return type_record(index, 2, width, u32(base, len(entries)) + b''.join(
        (code & ((1 << (8 * width)) - 1)).to_bytes(width, 'little') + text(name) for code, name in entries))


def fixture(which):
    types = [type_record(0, 0, 0)]
    for code, size in enumerate((1, 1, 1, 2, 2, 4, 4, 8, 8, 4, 8), 1):
        types.append(type_record(code, 1, size, bytes((code, 0, 0, 0))))
    catalogs, endpoints = [], []
    counts = [0, 0, 0, 0, 0, 0]
    if which != 'empty':
        types += [enum_type(12, 2, 1, [(0, 'Off'), (1, 'Run'), (2, 'Fault')]),
                  type_record(13, 4, 6, u32(4, 3)),
                  struct_type(14, 5, [(10, 'volts'), (1, 'active')]),
                  struct_type(15, 7, [(10, 'target'), (4, 'rpm'), (1, 'enabled')])]
        field_groups = [('motor', [(1, 'Enabled', 1), (4, 'RPM', 1), (10, 'Temperature', 1),
                                    (11, 'Precise', 1), (12, 'Mode', 1), (13, 'Samples', 1),
                                    (14, 'State', 1), (15, 'Config', 3)])]
        command_groups = [('motor', [(15, 'Configure'), (0, 'Reset')])]
        service_groups = [('motor', [(15, 15, 'Echo')])]
        if which == 'edge':
            types += [struct_type(16, 0, []), type_record(17, 4, 10, u32(14, 2)),
                      type_record(18, 4, 0, u32(2, 0)),
                      struct_type(19, 17, [(15, 'config'), (17, 'phases'), (16, 'empty'), (18, 'zero')]),
                      enum_type(20, 5, 2, [(-1000, 'N\u00e9gatif'), (2000, 'Haut')]),
                      enum_type(21, 8, 8, [(0xffffffffffffffff, 'Max')])]
            field_groups += [('empty', []), ('\u03bc', [(16, 'Empty', 1), (19, 'Box', 1),
                                                      (20, 'Signed', 1), (21, 'Wide', 1), (6, 'Late', 3)])]
            service_groups += [('extra', [(0, 0, 'Ping')])]
        for category, groups in enumerate((field_groups, command_groups, service_groups), 1):
            first = 0
            counts[(category - 1) * 2] = len(groups)
            for group, (name, entries) in enumerate(groups):
                catalogs.append(record(2, bytes((category, 0, 0, 0)) + u32(group, first, len(entries)) + text(name)))
                for local, entry in enumerate(entries):
                    payload = u32(group << 16 | local, entry[0])
                    if category == 1:
                        payload += bytes((entry[2], 0, 0, 0)) + text(entry[1])
                    elif category == 2:
                        payload += text(entry[1])
                    else:
                        payload += u32(entry[1]) + text(entry[2])
                    endpoints.append(record(category + 2, payload))
                first += len(entries)
            counts[(category - 1) * 2 + 1] = first
    type_bytes, catalog_bytes, endpoint_bytes = b''.join(types), b''.join(catalogs), b''.join(endpoints)
    total = 64 + len(type_bytes) + len(catalog_bytes) + len(endpoint_bytes)
    header = b'TDS3' + struct.pack('<4HIQ10I', 3, 0, 64, 0, total, 0, len(types), *counts,
                                 64, 64 + len(type_bytes), 64 + len(type_bytes) + len(catalog_bytes))
    result = bytearray(header + type_bytes + catalog_bytes + endpoint_bytes)
    struct.pack_into('<Q', result, 16, fingerprint(result))
    return bytes(result)


if __name__ == '__main__':
    from pathlib import Path
    for name in ('mixed', 'edge', 'empty'):
        data = fixture(name)
        path = Path(__file__).with_name(name + '.hex')
        path.write_text('\n'.join(data[i:i + 32].hex(' ') for i in range(0, len(data), 32)) + '\n')
        print(name, len(data), f'{fingerprint(data):016x}')
