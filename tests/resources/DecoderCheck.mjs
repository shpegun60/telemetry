// Cross-language fixtures, format evolution and malformed/truncated input (MIT).
// Authors: Ruslan Kovtun (shpegun60), codexAi.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {parseSchema, parseCommands, parseValues} from '../../web/telemetryBinary.js';
const directory = process.argv[2];
const read = name => fs.readFileSync(path.join(directory, name));
const sb = read('schema.bin'), cb = read('commands.bin'), vb = read('values.bin');
const schema = parseSchema(sb), commands = parseCommands(cb), values = parseValues(vb, schema);
assert.equal(schema.fields.length, 3);
assert.equal(schema.catalogs[0].name, 'me"ter');
assert.equal(schema.fields[0].name, 'U"a\u00e9');
assert.equal(schema.fields[0].unit, 'V\n');
assert.equal(schema.fields[0].policyFlags, 1);
assert.equal(schema.fields[0].accessFlags, 3);
assert.equal(schema.fields[0].min.value, -10);
assert.equal(schema.fields[0].max.value, 300);
assert.equal(schema.fields[0].default.value, 1);
assert.equal(schema.flags.get(1), 'Persistent');
assert.deepEqual(schema.fields[1].enumEntries.map(e => e.name), ['Off', 'Auto', 'Man\0ual']);
assert.equal(schema.fields[2].max.value, 18446744073709551615n);
assert.equal(schema.fields[2].enumEntries[1].code.value, 18446744073709551615n);
assert.equal(commands.commands[0].name, 'Con\nfigure');
assert.equal(commands.commands[0].parameters[0].default.value, 1);
assert.equal(commands.commands[0].parameters[1].unit, '');
assert.equal(commands.commands[0].parameters[2].name, null);
assert.equal(commands.commands[0].parameters[2].unit, null);
assert.equal(commands.commands[0].parameters[2].presenceFlags, 0);
assert.deepEqual(values.values.map(v => v.value), [1, 1, 18446744073709551615n]);
const allSchema = parseSchema(read('all-schema.bin'));
const allValues = parseValues(read('all-values.bin'), allSchema);
assert.deepEqual(allValues.values.map(v => v.value), [1, -0, 255, 0x1234, 0x12345678,
    18446744073709551615n, -1, -32768, -2147483648, -9223372036854775808n, true, null, null]);
assert(Object.is(allValues.values[1].value, -0));
assert.equal(allSchema.fields[12].fieldFlags, 1);
assert.equal(allSchema.catalogs[1].fields.length, 0);
for (const [name, decode] of [['golden-schema.bin', parseSchema], ['golden-commands.bin', parseCommands]]) {
    const b = read(name); assert.equal(decode(b).catalogs[0].name, 'm');
}
assert.equal(parseValues(read('golden-values.bin'), parseSchema(read('golden-schema.bin'))).values[0].value, 1);

// Unknown records have explicit size/version and can be skipped, including at the front.
function withUnknown(bytes, version = 1) {
    const extra = Buffer.alloc(25, 0xa5);
    extra[0] = 99; extra[1] = version; extra.writeUInt16LE(0, 2); extra.writeUInt32LE(17, 4);
    const b = Buffer.concat([bytes.subarray(0, 40), extra, bytes.subarray(40)]);
    b.writeUInt32LE(b.length, 12); b.writeUInt32LE(bytes.readUInt32LE(20) + 1, 20);
    return b;
}
for (const [bytes, decode] of [[sb, parseSchema], [cb, parseCommands]]) {
    const plain = decode(bytes);
    for (const version of [1, 255]) {
        const newer = decode(withUnknown(bytes, version));
        assert.equal(newer.fingerprint, plain.fingerprint);
        assert.equal(newer.unknownRecords.length, 1);
        assert.equal(newer.unknownRecords[0].payload.length, 17);
        assert.deepEqual(newer.catalogs, plain.catalogs);
    }
    // Header extensions and minor versions are compatible; major versions are not.
    const extended = Buffer.concat([bytes.subarray(0, 40), Buffer.alloc(4), bytes.subarray(40)]);
    extended.writeUInt32LE(44, 8); extended.writeUInt32LE(extended.length, 12); extended.writeUInt16LE(1, 6);
    assert.deepEqual(decode(extended).catalogs, plain.catalogs);
    const major = Buffer.from(bytes); major.writeUInt16LE(2, 4); assert.throws(() => decode(major));
}
// Every truncation is rejected, also when the declared total size is adjusted.
let rejected = 0;
for (const [bytes, decode] of [[sb, parseSchema], [cb, parseCommands], [vb, b => parseValues(b, schema)]]) {
    for (let n = 0; n < bytes.length; ++n) {
        assert.throws(() => decode(bytes.subarray(0, n))); ++rejected;
        if (n >= 40 && bytes !== vb) {
            const shortened = Buffer.from(bytes.subarray(0, n)); shortened.writeUInt32LE(n, 12);
            assert.throws(() => decode(shortened)); ++rejected;
        }
    }
    assert.throws(() => decode(Buffer.concat([bytes, Buffer.alloc(1)])));
    const magic = Buffer.from(bytes); magic[0] = 0; assert.throws(() => decode(magic));
    // Views with nonzero byteOffset must behave exactly like a standalone ArrayBuffer.
    const padded = Buffer.concat([Buffer.alloc(13), bytes, Buffer.alloc(7)]);
    assert.deepEqual(decode(padded.subarray(13, 13 + bytes.length)), decode(bytes));
}
const mismatch = Buffer.from(vb); mismatch.writeUInt32LE((schema.fingerprint ^ 1) >>> 0, 8);
assert.throws(() => parseValues(mismatch, schema), /fingerprint/);
const badStatus = Buffer.from(vb); badStatus[16] = 2; assert.throws(() => parseValues(badStatus, schema));
const unavailable = Buffer.from(vb); unavailable[16] = 1;
assert.throws(() => parseValues(unavailable, schema), /zero/);
unavailable.fill(0, 17, 21); assert.equal(parseValues(unavailable, schema).values[0].value, null);
for (const [bytes, decode] of [[sb, parseSchema], [cb, parseCommands]]) {
    for (const offset of [8, 12, 20, 24, 28, 32, 36, 44]) {
        const b = Buffer.from(bytes); b.writeUInt32LE(0xffffffff, offset); assert.throws(() => decode(b));
    }
}
// Bit-preserving values decode for subnormal, negative zero, infinities and NaN.
const singleSchema = parseSchema(read('golden-schema.bin'));
for (const [bits, expected] of [[0x80000000, -0], [0x00000001, 2 ** -149], [0x7f800000, Infinity], [0xff800000, -Infinity], [0x7fc12345, NaN]]) {
    const b = read('golden-values.bin'); b.writeUInt32LE(bits, 17);
    const value = parseValues(b, singleSchema).values[0];
    assert(Object.is(value.value, expected));
    assert.equal(new DataView(value.bytes.buffer).getUint32(0, true), bits);
}
// Deterministic bounded mutations may be valid metadata changes; no decoder may hang.
let state = 0x173ab91;
for (const [bytes, decode] of [[sb, parseSchema], [cb, parseCommands], [vb, b => parseValues(b, schema)]]) {
    for (let i = 0; i < 2000; ++i) {
        const b = Buffer.from(bytes);
        state ^= state << 13; state ^= state >>> 17; state ^= state << 5;
        const position = (state >>> 0) % b.length; b[position] ^= 1 << (i % 8);
        try { decode(b); } catch (e) { assert(e instanceof Error); }
    }
}
console.log(`Browser binary decoder: fixtures, BigInt, unknown records, ${rejected} truncations and 6000 mutations passed`);
