"""Critic trial: a host decoder for schema.bin + values.bin written only from
lib/resource/telemetry/README.md (format 2.1), to test whether the spec is sufficient.

python tests/review/critic/decode_bin.py <directory with schema.bin and values.bin>
"""
import os
import struct
import sys

TYPES = {0: ("null", 0, None), 1: ("bool", 1, "<?"), 2: ("u8", 1, "<B"), 3: ("u16", 2, "<H"),
         4: ("u32", 4, "<I"), 5: ("u64", 8, "<Q"), 6: ("s8", 1, "<b"), 7: ("s16", 2, "<h"),
         8: ("s32", 4, "<i"), 9: ("s64", 8, "<q"), 10: ("f32", 4, "<f"), 11: ("f64", 8, "<d")}


class Reader:
    def __init__(self, data, pos=0):
        self.d, self.p = data, pos

    def take(self, fmt):
        v = struct.unpack_from(fmt, self.d, self.p)
        self.p += struct.calcsize(fmt)
        return v if len(v) > 1 else v[0]

    def bytes(self, n):
        b = self.d[self.p:self.p + n]
        self.p += n
        return b

    def string(self):
        return self.bytes(self.take("<I")).decode("utf-8", "replace")

    def scalar(self):
        t, state, size = self.take("<BBB")
        payload = self.bytes(size)
        if t == 0 or state == 0:
            return None
        return struct.unpack(TYPES[t][2], payload)[0]


def parse_schema(data):
    r = Reader(data)
    magic = r.bytes(4)
    assert magic == b"TSCH", magic
    major, minor, header_size, total, fingerprint, records = r.take("<HHIIQI")
    catalogs, fields, enums, flags = r.take("<IIII")
    assert (major, minor) == (2, 1) and header_size == 44 and total == len(data)
    r.p = header_size
    out = {"fingerprint": fingerprint, "flags": {}, "catalogs": [], "fields": []}
    for _ in range(records):
        rtype, version, rflags, size = r.take("<BBHI")
        end = r.p + size
        if rtype == 1:
            value = r.take("<I")
            out["flags"][value] = r.string()
        elif rtype == 2:
            ci, count = r.take("<II")
            out["catalogs"].append({"index": ci, "count": count, "name": r.string()})
        elif rtype == 3:
            ci, fi, fid, policy, enum_count = r.take("<IIIII")
            declared, value_type, access, fflags = r.take("<BBBB")
            name_len, unit_len = r.take("<II")
            name = r.bytes(name_len).decode()
            unit = r.bytes(unit_len).decode()
            mn, mx, df = r.scalar(), r.scalar(), r.scalar()
            out["fields"].append({"id": fid, "catalog": ci, "pos": fi, "name": name, "unit": unit,
                                  "type": TYPES[declared][0], "valueType": value_type,
                                  "writable": bool(access & 2), "persistent": bool(policy & 1),
                                  "reserved": bool(fflags & 1), "min": mn, "max": mx, "default": df,
                                  "enum": {}})
        elif rtype == 4:
            fid, ordinal = r.take("<II")
            code = r.scalar()
            out["fields"][-1]["enum"][code] = r.string()
        r.p = end  # Unknown records are skipped by size, as the spec allows.
    assert len(out["fields"]) == fields and len(out["catalogs"]) == catalogs
    return out


def parse_values(data, schema):
    r = Reader(data)
    assert r.bytes(4) == b"TVAL"
    major, minor, fingerprint, count = r.take("<HHQI")
    if fingerprint != schema["fingerprint"]:
        raise ValueError("values do not match this schema; reload schema.bin")
    values = []
    for field in schema["fields"]:
        status = r.take("<B")
        name, width, fmt = TYPES[field["valueType"]]
        payload = r.bytes(width)
        values.append(struct.unpack(fmt, payload)[0] if status == 0 and fmt else None)
    assert r.p == len(data) and count == len(values)
    return values


if __name__ == "__main__":
    folder = sys.argv[1]
    schema = parse_schema(open(os.path.join(folder, "schema.bin"), "rb").read())
    values = parse_values(open(os.path.join(folder, "values.bin"), "rb").read(), schema)
    print(f"fingerprint {schema['fingerprint']:016x}, flags {schema['flags']}")
    for field, value in zip(schema["fields"], values):
        shown = field["enum"].get(value, value) if field["enum"] else value
        print(f"  id {field['id']:>6} {field['name']:12s} {shown!s:>12} {field['unit']:5s} {field['type']:4s} "
              f"w={int(field['writable'])} persistent={int(field['persistent'])} "
              f"min={field['min']} max={field['max']} default={field['default']}")
