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
for (const decoded of [schema, commands, values]) {
    assert.equal(decoded.major, 2);
    assert.equal(decoded.minor, 1);
}
assert.equal(typeof schema.fingerprint, 'bigint');
assert.equal(typeof commands.fingerprint, 'bigint');
assert.equal(values.schemaFingerprint, schema.fingerprint);
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
const zeroCommands = parseCommands(read('zero-commands.bin'));
const reservedCommands = parseCommands(read('reserved-commands.bin'));
const slotCommands = parseCommands(read('slot-commands.bin'));
assert.equal(zeroCommands.commands[0].reserved, false);
assert.equal(reservedCommands.commands[0].reserved, true);
assert.equal(reservedCommands.commands[0].commandFlags, 1);
assert.equal(reservedCommands.commands[0].parameterCount, 0);
assert.notEqual(reservedCommands.fingerprint, zeroCommands.fingerprint);
assert.deepEqual(slotCommands, zeroCommands); // Empty slot retains command capability.
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

// Independent BigInt arithmetic over wire payloads checks the encoder's entire
// semantic hash domain, including the postorder used for fields and parameters.
function semanticFingerprint(bytes, isCommands) {
    let hash = 0xcbf29ce484222325n;
    const add = part => {
        for (const byte of part) hash = BigInt.asUintN(64, (hash ^ BigInt(byte)) * 0x100000001b3n);
    };
    const record = part => {
        add(part.subarray(0, 4));
        add(part.subarray(8));
        add(part.subarray(4, 8));
    };
    add(bytes.subarray(0, 8));
    let parent = null, parameter = null;
    const flushParameter = () => {
        if (parameter) record(parameter);
        parameter = null;
    };
    const flushParent = () => {
        flushParameter();
        if (parent) record(parent);
        parent = null;
    };
    for (let offset = 44; offset < bytes.length;) {
        const type = bytes[offset], size = bytes.readUInt32LE(offset + 4);
        const part = bytes.subarray(offset, offset + 8 + size);
        offset += part.length;
        if (type === 4) record(part);
        else if (isCommands && type === 3) {
            flushParameter(); parameter = part;
        } else {
            flushParent();
            if (type === (isCommands ? 2 : 3)) parent = part;
            else record(part);
        }
    }
    flushParent();
    return hash;
}
for (const name of ['schema.bin', 'all-schema.bin', 'golden-schema.bin',
                    'commands.bin', 'golden-commands.bin', 'zero-commands.bin',
                    'reserved-commands.bin', 'slot-commands.bin']) {
    const bytes = read(name);
    assert.equal(semanticFingerprint(bytes, name.includes('commands')), bytes.readBigUInt64LE(16));
}

// These are complete published v1 goldens, not merely v2 bytes with a changed version.
const legacySchema = Buffer.from(
    '545343480100000028000000960000007033fbfe0300000001000000010000000000000001000000' +
    '0101000012000000010000000a00000050657273697374656e74020100000d00000000000000' +
    '01000000010000006d03010000370000000000000000000000000000000000000000000000' +
    '0a0a0100010000000100000056560a0104000000000a0104000096430a010400006643', 'hex');
const legacyCommands = Buffer.from(
    '54434d44010000002800000099000000366c723c0300000001000000010000000100000000000000' +
    '010100000d0000000000000001000000010000006d02010000190000000000000000000000' +
    '00000000010000000000000001000000430301000033000000000000000000000000000000' +
    '000000000a030000010000000100000050560a0104000000000a0104000096430a010400006643', 'hex');
const legacyValues = Buffer.from('5456414c010000007033fbfe01000000000000803f', 'hex');
assert.throws(() => parseSchema(legacySchema), /major version/);
assert.throws(() => parseCommands(legacyCommands), /major version/);
assert.throws(() => parseValues(legacyValues, schema), /major version/);

// Unknown records have explicit size/version and can be skipped, including at the front.
function withUnknown(bytes, version = 1, flags = 0, type = 99) {
    const extra = Buffer.alloc(25, 0xa5);
    extra[0] = type; extra[1] = version; extra.writeUInt16LE(flags, 2); extra.writeUInt32LE(17, 4);
    const b = Buffer.concat([bytes.subarray(0, 44), extra, bytes.subarray(44)]);
    b.writeUInt32LE(b.length, 12); b.writeUInt32LE(bytes.readUInt32LE(24) + 1, 24);
    return b;
}
function recordOffsets(bytes) {
    const offsets = [];
    for (let n = bytes.readUInt32LE(8); n < bytes.length; n += 8 + bytes.readUInt32LE(n + 4)) offsets.push(n);
    return offsets;
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
    // Opaque future records retain flags; understood v1 records must not
    // silently discard semantics represented by any of the 16 header bits.
    for (const [version, type] of [[1, 99], [2, 1]]) {
        const future = decode(withUnknown(bytes, version, 0xffff, type));
        assert.equal(future.unknownRecords[0].flags, 0xffff);
        assert.deepEqual(future.catalogs, plain.catalogs);
    }
    for (const offset of recordOffsets(bytes)) {
        for (let bit = 0; bit < 16; ++bit) {
            const unsupported = Buffer.from(bytes);
            unsupported.writeUInt16LE(1 << bit, offset + 2);
            assert.throws(() => decode(unsupported), /record flags/);
        }
    }
    // Header extensions remain bounded within the supported format version.
    const extended = Buffer.concat([bytes.subarray(0, 44), Buffer.alloc(4), bytes.subarray(44)]);
    extended.writeUInt32LE(48, 8); extended.writeUInt32LE(extended.length, 12); extended.writeUInt16LE(1, 6);
    assert.deepEqual(decode(extended).catalogs, plain.catalogs);
    for (const version of [0, 1, 3, 65535]) {
        const major = Buffer.from(bytes); major.writeUInt16LE(version, 4);
        assert.throws(() => decode(major), /major version/);
    }
    for (const version of [0, 2, 65535]) {
        const minor = Buffer.from(bytes); minor.writeUInt16LE(version, 6);
        assert.throws(() => decode(minor), /minor version/);
    }
}
// Capability/record flags affect interpretation. Policy flags are intentionally
// forward-compatible and retain unknown application bits.
const fieldOffset = recordOffsets(sb).find(n => sb[n] === 3) + 8;
for (const [offset, mask] of [[fieldOffset + 22, 4], [fieldOffset + 23, 2]]) {
    const b = Buffer.from(sb); b[offset] |= mask;
    assert.throws(() => parseSchema(b), /field flags/);
}
const reservedField = Buffer.from(sb); reservedField[fieldOffset + 23] = 1;
assert.throws(() => parseSchema(reservedField), /reserved field/);
const policy = Buffer.from(sb); policy.writeUInt32LE(0x80000000, fieldOffset + 12);
assert.equal(parseSchema(policy).fields[0].policyFlags, 0x80000000);
const commandOffset = recordOffsets(cb).find(n => cb[n] === 2) + 8;
const parameterOffset = recordOffsets(cb).find(n => cb[n] === 3) + 8;
const commandFlags = Buffer.from(cb); commandFlags.writeUInt32LE(2, commandOffset + 16);
assert.throws(() => parseCommands(commandFlags), /command flags/);
const reservedWithArgs = Buffer.from(cb); reservedWithArgs.writeUInt32LE(1, commandOffset + 16);
assert.throws(() => parseCommands(reservedWithArgs), /reserved command/);
const parameterFlags = Buffer.from(cb); parameterFlags.writeUInt32LE(1, parameterOffset + 8);
assert.throws(() => parseCommands(parameterFlags), /parameter flags/);
// Every truncation is rejected, also when the declared total size is adjusted.
let rejected = 0;
for (const [bytes, decode] of [[sb, parseSchema], [cb, parseCommands], [vb, b => parseValues(b, schema)]]) {
    for (let n = 0; n < bytes.length; ++n) {
        assert.throws(() => decode(bytes.subarray(0, n))); ++rejected;
        if (n >= 44 && bytes !== vb) {
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
// Every fingerprint bit participates, especially the previously absent upper word.
for (let bit = 0n; bit < 64n; ++bit) {
    const mismatch = Buffer.from(vb); mismatch.writeBigUInt64LE(schema.fingerprint ^ (1n << bit), 8);
    assert.throws(() => parseValues(mismatch, schema), /fingerprint/);
}
for (const version of [0, 1, 3, 65535]) {
    const major = Buffer.from(vb); major.writeUInt16LE(version, 4);
    assert.throws(() => parseValues(major, schema), /major version/);
}
for (const version of [0, 2, 65535]) {
    const minor = Buffer.from(vb); minor.writeUInt16LE(version, 6);
    assert.throws(() => parseValues(minor, schema), /minor version/);
}
const badStatus = Buffer.from(vb); badStatus[20] = 2; assert.throws(() => parseValues(badStatus, schema));
const unavailable = Buffer.from(vb); unavailable[20] = 1;
assert.throws(() => parseValues(unavailable, schema), /zero/);
unavailable.fill(0, 21, 25); assert.equal(parseValues(unavailable, schema).values[0].value, null);
for (const [bytes, decode] of [[sb, parseSchema], [cb, parseCommands]]) {
    for (const offset of [8, 12, 24, 28, 32, 36, 40, 48]) {
        const b = Buffer.from(bytes); b.writeUInt32LE(0xffffffff, offset); assert.throws(() => decode(b));
    }
}
// Bit-preserving values decode for subnormal, negative zero, infinities and NaN.
const singleSchema = parseSchema(read('golden-schema.bin'));
for (const [bits, expected] of [[0x80000000, -0], [0x00000001, 2 ** -149], [0x7f800000, Infinity], [0xff800000, -Infinity], [0x7fc12345, NaN]]) {
    const b = read('golden-values.bin'); b.writeUInt32LE(bits, 21);
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
