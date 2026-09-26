#!/usr/bin/env python3
"""Review helper (resource slice): independent decoding of the C++ fixtures.

Re-derives FNV-1a 64 semantic fingerprints from wire bytes using the README's
postorder rule, parses records with the same structural rules as
web/telemetryBinary.js, and reports the DecoderCheck truncation count.
Usage: python decode_fixtures.py <fixtures-dir>
"""
import struct
import sys
from pathlib import Path

MASK = (1 << 64) - 1
WIDTHS = [0, 1, 1, 2, 4, 8, 1, 2, 4, 8, 4, 8]


def fnv(data, h=0xcbf29ce484222325):
    for b in data:
        h = ((h ^ b) * 0x100000001b3) & MASK
    return h


def records(b, header=44):
    off = struct.unpack_from("<I", b, 8)[0]
    out = []
    while off < len(b):
        t, v, f, size = struct.unpack_from("<BBHI", b, off)
        out.append((t, v, f, b[off:off + 8 + size]))
        off += 8 + size
    assert off == len(b), "records overrun"
    return out


def semantic(b, commands):
    """Postorder: schema enums before their Field; commands param enums before the
    Parameter, all parameters before the Command. Each record hashed as
    [type, version, flags, payload, u32 size]."""
    h = fnv(b[:8])
    parent = None
    parameter = None

    def rec(part):
        nonlocal h
        h = fnv(part[0:4], h)
        h = fnv(part[8:], h)
        h = fnv(part[4:8], h)

    for t, _, _, part in records(b):
        if t == 4:
            rec(part)
        elif commands and t == 3:
            if parameter is not None:
                rec(parameter)
            parameter = part
        else:
            if parameter is not None:
                rec(parameter)
                parameter = None
            if parent is not None:
                rec(parent)
                parent = None
            if t == (2 if commands else 3):
                parent = part
            else:
                rec(part)
    if parameter is not None:
        rec(parameter)
    if parent is not None:
        rec(parent)
    return h


def main():
    d = Path(sys.argv[1])
    for name in ["schema.bin", "all-schema.bin", "golden-schema.bin", "commands.bin",
                 "golden-commands.bin", "zero-commands.bin", "reserved-commands.bin",
                 "slot-commands.bin"]:
        b = (d / name).read_bytes()
        magic, major, minor, hsize, total, fp, count = struct.unpack_from("<4sHHIIQI", b, 0)
        counts = struct.unpack_from("<4I", b, 28)
        recs = records(b)
        ok = semantic(b, "commands" in name) == fp
        print(f"{name:22} {magic.decode()} v{major}.{minor} header={hsize} total={total}/{len(b)} "
              f"records={count}/{len(recs)} counts={counts} fp={fp:016x} semantic-match={ok}")
    sb, cb, vb = ((d / n).read_bytes() for n in ("schema.bin", "commands.bin", "values.bin"))
    trunc = len(sb) + (len(sb) - 44) + len(cb) + (len(cb) - 44) + len(vb)
    print("DecoderCheck truncation cases for these fixtures:", trunc)
    for name in ("values.bin", "all-values.bin", "golden-values.bin"):
        v = (d / name).read_bytes()
        magic, major, minor, fp, n = struct.unpack_from("<4sHHQI", v, 0)
        print(f"{name:22} {magic.decode()} v{major}.{minor} fp={fp:016x} fields={n} bytes={len(v)}")


if __name__ == "__main__":
    main()
