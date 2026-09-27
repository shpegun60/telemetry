"""Independent Stage 10 values/header oracle, not the Stage 12 client. MIT."""
from pathlib import Path
import struct
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'descriptor'))
from parser import parse


def values(data, descriptor):
    if len(data) < 24:
        raise ValueError('short header')
    magic, major, minor, count, size, fingerprint = struct.unpack_from('<4sHHIIQ', data)
    fields = descriptor['endpoints'][0]
    if magic != b'TVL3' or (major, minor) != (3, 0):
        raise ValueError('version/magic')
    if fingerprint != descriptor['fingerprint']:
        raise ValueError('descriptor fingerprint')
    if count != len(fields) or size != len(data):
        raise ValueError('count/size')
    pos, result = 24, []
    for field in fields:
        width = descriptor['types'][field['type']]['wire']
        if pos + 1 + width > len(data):
            raise ValueError('short token')
        status, payload = data[pos], data[pos + 1:pos + 1 + width]
        if status not in (0, 1) or (status == 1 and any(payload)):
            raise ValueError('status/payload')
        result.append((status, payload))
        pos += 1 + width
    if pos != len(data):
        raise ValueError('trailing bytes')
    return result


def check(directory):
    directory = Path(directory)
    descriptor = parse((directory / 'descriptor.bin').read_bytes())
    data = (directory / 'values.bin').read_bytes()
    # Independently specified wire bytes, including unknown enum code -2,
    # negative zero and payload-less types. Only identity comes from descriptor.
    payloads = [bytes.fromhex(x) for x in (
        '0078563412', '0001', '000000000000000080', '00feff',
        '0034120000c03f00', '0001000001ffff', '00', '00', '0100000000', '0100000000000000')]
    expected = struct.pack('<4sHHIIQ', b'TVL3', 3, 0, 10, 73, descriptor['fingerprint']) + b''.join(payloads)
    frozen = bytes.fromhex(Path(__file__).with_name('values.hex').read_text())
    assert data == expected == frozen
    assert len(values(data, descriptor)) == 10
    checks = 1

    def reject(changed):
        nonlocal checks
        try:
            values(changed, descriptor)
        except ValueError:
            checks += 1
        else:
            raise AssertionError('Accepted invalid values file')

    for end in range(len(data)):
        reject(data[:end])
    for offset in range(24):
        changed = bytearray(data)
        changed[offset] ^= 0x80
        reject(changed)
    for offset in (24, 29, 31, 40, 43, 51, 58, 59, 60, 65):
        changed = bytearray(data); changed[offset] = 2; reject(changed)
    for offset in list(range(61, 65)) + list(range(66, 73)):
        changed = bytearray(data); changed[offset] = 1; reject(changed)
    changed = bytearray(data + b'\x00'); struct.pack_into('<I', changed, 12, len(changed)); reject(changed)
    other = dict(descriptor, fingerprint=descriptor['fingerprint'] ^ 1)
    try:
        values(data, other)
    except ValueError as error:
        assert str(error) == 'descriptor fingerprint'
        checks += 1
    else:
        raise AssertionError('Wrong descriptor accepted')
    return checks
