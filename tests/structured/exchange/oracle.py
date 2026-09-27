"""Independent frozen wire bytes. No imports from implementation helpers. MIT."""
import struct


def check(path):
    bind = b'TSBN' + struct.pack('<HHQ', 3, 0, 0x0102030405060708)
    payload = struct.pack('<IB', 0x12345678, 1)
    request = b'TSRQ' + struct.pack('<HHIIIBBH', 3, 0, 0x10203040, 0, 5, 3, 0, 0) + payload
    response = b'TSRP' + struct.pack('<HHIIIBBBB', 3, 0, 0x10203040, 0, 5, 3, 0, 0, 0) + payload
    expected = bind + request + response
    actual = path.read_bytes()
    if actual != expected:
        raise AssertionError(f'wire golden differs: {actual.hex()} != {expected.hex()}')
    return len(expected)
