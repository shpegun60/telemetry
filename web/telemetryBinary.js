/**
 * Reference browser decoder for telemetry resource binary v2.1.
 * Authors: Ruslan Kovtun (shpegun60), codexAi. MIT; see ../LICENSE.
 * 64-bit integers and fingerprints are BigInt. No MCU headers or host endian assumptions.
 */
export const WireScalarType = Object.freeze({
    Null: 0, Bool: 1, U8: 2, U16: 3, U32: 4, U64: 5,
    S8: 6, S16: 7, S32: 8, S64: 9, F32: 10, F64: 11
});
const widths = [0, 1, 1, 2, 4, 8, 1, 2, 4, 8, 4, 8];
const utf8 = new TextDecoder('utf-8', {fatal: true, ignoreBOM: true});
function require(condition, message) {
    if (!condition) throw new Error(`Telemetry binary: ${message}`);
}
function width(type) {
    require(Number.isInteger(type) && type >= 0 && type < widths.length, 'unknown scalar type');
    return widths[type];
}
class Reader {
    constructor(input) {
        if (input instanceof ArrayBuffer) this.data = new Uint8Array(input);
        else if (ArrayBuffer.isView(input)) this.data = new Uint8Array(input.buffer, input.byteOffset, input.byteLength);
        else throw new TypeError('Expected ArrayBuffer or an ArrayBuffer view');
        this.view = new DataView(this.data.buffer, this.data.byteOffset, this.data.byteLength);
        this.offset = 0;
    }
    get remaining() { return this.data.length - this.offset; }
    reserve(length) {
        require(Number.isSafeInteger(length) && length >= 0 && length <= this.remaining, 'truncated data');
        const start = this.offset;
        this.offset += length;
        return start;
    }
    u8() { return this.view.getUint8(this.reserve(1)); }
    u16() { return this.view.getUint16(this.reserve(2), true); }
    u32() { return this.view.getUint32(this.reserve(4), true); }
    u64() { return this.view.getBigUint64(this.reserve(8), true); }
    bytes(length) { const start = this.reserve(length); return this.data.subarray(start, start + length); }
    text(length) {
        const bytes = this.bytes(length).slice();
        // Preserve arbitrary byte strings too. UTF-8 is a UI convention, not a wire restriction.
        let text = null;
        try { text = utf8.decode(bytes); } catch { /* raw bytes remain lossless */ }
        return {text, bytes};
    }
    string() { return this.text(this.u32()); }
    done() { require(this.remaining === 0, 'unexpected trailing bytes'); }
}
function payload(r, type) {
    const size = width(type);
    const start = r.reserve(size);
    let value;
    switch (type) {
        case 0: value = null; break;
        case 1: {
            const byte = r.view.getUint8(start);
            require(byte <= 1, 'invalid bool payload');
            value = byte === 1; break;
        }
        case 2: value = r.view.getUint8(start); break;
        case 3: value = r.view.getUint16(start, true); break;
        case 4: value = r.view.getUint32(start, true); break;
        case 5: value = r.view.getBigUint64(start, true); break;
        case 6: value = r.view.getInt8(start); break;
        case 7: value = r.view.getInt16(start, true); break;
        case 8: value = r.view.getInt32(start, true); break;
        case 9: value = r.view.getBigInt64(start, true); break;
        case 10: value = r.view.getFloat32(start, true); break;
        case 11: value = r.view.getFloat64(start, true); break;
    }
    return {value, bytes: r.data.slice(start, start + size)};
}
function scalar(r) {
    const type = r.u8(), state = r.u8(), payloadSize = r.u8();
    require(payloadSize === width(type), 'scalar width mismatch');
    require(state === (type === 0 ? 0 : 1), 'noncanonical scalar state');
    return {type, state, payloadSize, ...payload(r, type)};
}
function limits(r, type) {
    const min = scalar(r), max = scalar(r), defaultValue = scalar(r);
    require([min, max, defaultValue].every(s => s.type === type), 'limit type mismatch');
    return {min, max, default: defaultValue};
}
function identity(group, position, id) {
    require(group <= 65535 && position <= 65535 && id === group * 65536 + position, 'invalid positional ID');
}
function label(object, name, text) {
    object[name] = text.text;
    object[`${name}Bytes`] = text.bytes;
}
function header(input, magic) {
    const r = new Reader(input);
    require(String.fromCharCode(...r.bytes(4)) === magic, 'wrong file magic');
    const major = r.u16(), minor = r.u16();
    require(major === 2, 'unsupported major version');
    require(minor === 1, 'unsupported minor version');
    const headerSize = r.u32(), totalSize = r.u32(), fingerprint = r.u64(), recordCount = r.u32();
    require(headerSize >= 44 && headerSize <= r.data.length && totalSize === r.data.length, 'invalid file size');
    const counts = [r.u32(), r.u32(), r.u32(), r.u32()];
    r.reserve(headerSize - 44);
    require(recordCount <= Math.floor(r.remaining / 8), 'impossible record count');
    return {r, major, minor, fingerprint, recordCount, counts};
}
function records(h, knownTypes, visit) {
    const unknownRecords = [];
    for (let i = 0; i < h.recordCount; ++i) {
        const type = h.r.u8(), version = h.r.u8(), flags = h.r.u16(), size = h.r.u32();
        const bytes = h.r.bytes(size);
        const r = new Reader(bytes);
        if (version === 1 && knownTypes.includes(type)) {
            // A known version is not enough if flags change its semantics.
            // Unknown types/versions remain opaque, including their flags.
            require(flags === 0, 'unsupported record flags');
            visit(type, r);
            r.done();
        } else unknownRecords.push({type, version, flags, payload: bytes.slice()});
    }
    h.r.done();
    return unknownRecords;
}

/** Decode a full descriptive schema. Raw string/scalar bytes are retained alongside UI values. */
export function parseSchema(input) {
    const h = header(input, 'TSCH');
    const schema = {major: h.major, minor: h.minor, fingerprint: h.fingerprint,
        catalogs: [], fields: [], flags: new Map(), flagDefinitions: []};
    const byId = new Map();
    let enumCount = 0;
    schema.unknownRecords = records(h, [1, 2, 3, 4], (type, r) => {
        if (type === 1) {
            const value = r.u32(), name = r.string();
            require(!schema.flags.has(value), 'duplicate flag definition');
            schema.flags.set(value, name.text);
            schema.flagDefinitions.push({value, name: name.text, nameBytes: name.bytes});
        } else if (type === 2) {
            const index = r.u32(), fieldCount = r.u32();
            require(index === schema.catalogs.length && index <= 65535 && fieldCount <= 65536, 'invalid catalog index/count');
            const c = {index, fieldCount, fields: []}; label(c, 'name', r.string()); schema.catalogs.push(c);
        } else if (type === 3) {
            const catalogIndex = r.u32(), index = r.u32(), id = r.u32(); identity(catalogIndex, index, id);
            const policyFlags = r.u32(), enums = r.u32();
            const declaredType = r.u8(), valueType = r.u8(), accessFlags = r.u8(), fieldFlags = r.u8();
            width(declaredType); width(valueType);
            require((accessFlags & ~3) === 0 && (fieldFlags & ~1) === 0, 'unsupported field flags');
            require(!(fieldFlags & 1) || (valueType === 0 && accessFlags === 0), 'reserved field has capabilities');
            const nl = r.u32(), ul = r.u32();
            const f = {catalogIndex, index, id, policyFlags, enumCount: enums,
                declaredType, valueType, accessFlags, fieldFlags, enumEntries: []};
            label(f, 'name', r.text(nl)); label(f, 'unit', r.text(ul)); Object.assign(f, limits(r, declaredType));
            const catalog = schema.catalogs[catalogIndex];
            require(catalog && index === catalog.fields.length && index < catalog.fieldCount && !byId.has(id), 'invalid field position');
            catalog.fields.push(f); schema.fields.push(f); byId.set(id, f);
        } else if (type === 4) {
            const id = r.u32(), ordinal = r.u32(), code = scalar(r), name = r.string();
            const f = byId.get(id);
            require(f && ordinal === f.enumEntries.length && ordinal < f.enumCount && code.type === f.declaredType, 'invalid field enum entry');
            f.enumEntries.push({ordinal, code, name: name.text, nameBytes: name.bytes}); ++enumCount;
        } else return false;
        return true;
    });
    require(schema.catalogs.length === h.counts[0] && schema.fields.length === h.counts[1] &&
        enumCount === h.counts[2] && schema.flags.size === h.counts[3], 'schema count mismatch');
    require(schema.catalogs.every(c => c.fields.length === c.fieldCount) &&
        schema.fields.every(f => f.enumEntries.length === f.enumCount), 'incomplete schema');
    // Flat value ordering is positional, never whatever record arrival order happened to be.
    schema.fields = schema.catalogs.flatMap(c => c.fields);
    return schema;
}

/** Decode command descriptions only; this module never invokes a command. */
export function parseCommands(input) {
    const h = header(input, 'TCMD');
    const result = {major: h.major, minor: h.minor, fingerprint: h.fingerprint, catalogs: [], commands: []};
    const byId = new Map(); let parameterCount = 0, enumCount = 0;
    result.unknownRecords = records(h, [1, 2, 3, 4], (type, r) => {
        if (type === 1) {
            const index = r.u32(), commandCount = r.u32();
            require(index === result.catalogs.length && index <= 65535 && commandCount <= 65536, 'invalid command catalog');
            const c = {index, commandCount, commands: []}; label(c, 'name', r.string()); result.catalogs.push(c);
        } else if (type === 2) {
            const catalogIndex = r.u32(), index = r.u32(), id = r.u32(); identity(catalogIndex, index, id);
            const c = {catalogIndex, index, id, parameterCount: r.u32(), commandFlags: r.u32(), parameters: []};
            require((c.commandFlags & ~1) === 0, 'unsupported command flags');
            c.reserved = (c.commandFlags & 1) !== 0;
            require(!c.reserved || c.parameterCount === 0, 'reserved command has parameters');
            label(c, 'name', r.string()); const catalog = result.catalogs[catalogIndex];
            require(catalog && index === catalog.commands.length && index < catalog.commandCount && !byId.has(id), 'invalid command position');
            catalog.commands.push(c); result.commands.push(c); byId.set(id, c);
        } else if (type === 3) {
            const commandId = r.u32(), index = r.u32(), parameterFlags = r.u32(), enums = r.u32();
            require(parameterFlags === 0, 'unsupported parameter flags');
            const scalarType = r.u8(), presenceFlags = r.u8(); require(r.u16() === 0, 'parameter reserved bits');
            width(scalarType); require((presenceFlags & ~3) === 0, 'unknown parameter presence');
            const nl = r.u32(), ul = r.u32();
            require(((presenceFlags & 1) || nl === 0) && ((presenceFlags & 2) || ul === 0), 'absent label has bytes');
            const p = {commandId, index, parameterFlags, enumCount: enums, type: scalarType, presenceFlags, enumEntries: []};
            label(p, 'name', r.text(nl)); label(p, 'unit', r.text(ul));
            if (!(presenceFlags & 1)) p.name = null;
            if (!(presenceFlags & 2)) p.unit = null;
            Object.assign(p, limits(r, scalarType)); const c = byId.get(commandId);
            require(c && index === c.parameters.length && index < c.parameterCount, 'invalid parameter position');
            c.parameters.push(p); ++parameterCount;
        } else if (type === 4) {
            const commandId = r.u32(), parameterIndex = r.u32(), ordinal = r.u32(), code = scalar(r), name = r.string();
            const p = byId.get(commandId)?.parameters[parameterIndex];
            require(p && ordinal === p.enumEntries.length && ordinal < p.enumCount && code.type === p.type, 'invalid parameter enum entry');
            p.enumEntries.push({ordinal, code, name: name.text, nameBytes: name.bytes}); ++enumCount;
        } else return false;
        return true;
    });
    require(result.catalogs.length === h.counts[0] && result.commands.length === h.counts[1] &&
        parameterCount === h.counts[2] && enumCount === h.counts[3], 'command count mismatch');
    require(result.catalogs.every(c => c.commands.length === c.commandCount) &&
        result.commands.every(c => c.parameters.length === c.parameterCount &&
            c.parameters.every(p => p.enumEntries.length === p.enumCount)), 'incomplete command description');
    result.commands = result.catalogs.flatMap(c => c.commands);
    return result;
}

/** Decode a complete live-values file using its matching schema. U64/S64 stay BigInt. */
export function parseValues(input, schema) {
    const r = new Reader(input);
    require(String.fromCharCode(...r.bytes(4)) === 'TVAL', 'wrong values magic');
    const major = r.u16(), minor = r.u16(); require(major === 2, 'unsupported major version');
    require(minor === 1, 'unsupported minor version');
    const fingerprint = r.u64(), fieldCount = r.u32();
    require(fingerprint === schema.fingerprint, 'schema fingerprint mismatch');
    require(fieldCount === schema.fields.length, 'values count mismatch');
    const values = [];
    for (const field of schema.fields) {
        const status = r.u8(); require(status === 0 || status === 1, 'invalid value status');
        const decoded = payload(r, field.valueType);
        if (status === 1) require(decoded.bytes.every(b => b === 0), 'unavailable payload must be zero');
        else require(field.valueType !== 0, 'Null field cannot be available');
        values.push({id: field.id, status, value: status === 0 ? decoded.value : null, bytes: decoded.bytes});
    }
    r.done(); return {major, minor, schemaFingerprint: fingerprint, values};
}
