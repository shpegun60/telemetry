/* Strict client parsing, numeric edges and JS/C++ interoperability. MIT.
 * Authors: Ruslan Kovtun (shpegun60), codexAi.
 */
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import * as codec from '../../../web/telemetry.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const [directory, device] = process.argv.slice(2);
const read = name => new Uint8Array(fs.readFileSync(path.join(directory, name)));
const hex = name =>
    new Uint8Array(Buffer.from(fs.readFileSync(name, 'utf8').replace(/\s/g, ''), 'hex'));
let checks = 0;
const check = condition => {
	assert.ok(condition);
	++checks;
};
const equalBytes = (left, right) => {
	assert.deepEqual([...left ], [...right ]);
	++checks;
};

function reject(fn, reason = undefined)
{
	assert.throws(fn, reason ? {name : 'TelemetryFormatError', message : reason}
	                         : codec.TelemetryFormatError);
	++checks;
}

const models = new Map();
for (const name of ['empty', 'mixed', 'edge']) {
	const bytes = hex(path.join(here, '../descriptor', name + '.hex'));
	const model = codec.parseDescriptor(bytes);
	models.set(name, {bytes, model});
	check(model.fingerprint === codec.descriptorFingerprint(bytes));
	check(Object.isFrozen(model) && Object.isFrozen(model.types));
	// Every truncated prefix, including a missing final byte, is rejected.
	for (let end = 0; end < bytes.length; ++end)
		reject(() => codec.parseDescriptor(bytes.subarray(0, end)));
	const wrapped = new Uint8Array(bytes.length + 9);
	wrapped.set(bytes, 3);
	check(codec.parseDescriptor(new DataView(wrapped.buffer, 3, bytes.length)).fingerprint ===
	      model.fingerprint);
}
check(models.get('empty').model.fields.length === 0);
check(models.get('edge').model.fieldCatalogs[1].count === 0);
const wide = models.get('edge').model.types.find(type => type.kind === 2 && type.wireBytes === 8);
check(wide.entries[0].code === 0xffffffffffffffffn);

function rehash(bytes)
{
	new DataView(bytes.buffer).setBigUint64(16, codec.descriptorFingerprint(bytes), true);
	return bytes;
}

function mutation(offset, edit, reason)
{
	const bytes = models.get('mixed').bytes.slice();
	edit(new DataView(bytes.buffer), offset, bytes);
	reject(() => codec.parseDescriptor(rehash(bytes)), reason);
}

const mixed = models.get('mixed').model;
mutation(8, (view, at) => view.setUint16(at, 63, true), /Header size/);
mutation(10, (view, at) => view.setUint16(at, 1, true), /Header size/);
mutation(24, (view, at) => view.setUint32(at, 0xffffffff, true), /Type budget/);
mutation(28, (view, at) => view.setUint32(at, 0xffffffff, true), /Catalog budget/);
mutation(32, (view, at) => view.setUint32(at, 0xffffffff, true), /Endpoint budget/);
mutation(56, (view, at) => view.setUint32(at, 63, true), /Section offsets/);
mutation(mixed.types[12].offset + 1, (view, at) => view.setUint8(at, 2), /Record version/);
mutation(mixed.types[12].offset + 2, (view, at) => view.setUint16(at, 1, true), /reserved/);
mutation(mixed.types[12].offset + 20, (view, at) => view.setUint32(at, 12, true), /Backward/);
mutation(mixed.types[12].offset + 36, (view, at) => view.setUint8(at, 0), /Enum code order/);
mutation(mixed.types[15].offset + 24, (view, at) => view.setUint32(at, 15, true), /Backward/);
mutation(mixed.types[15].offset + 28, (view, at) => view.setUint32(at, 0xffffffff, true),
         /String budget/);
mutation(mixed.types[15].offset + 32, (view, at) => view.setUint8(at, 0), /Embedded NUL/);
mutation(mixed.types[15].offset + 32, (view, at) => view.setUint8(at, 0xff), /UTF-8/);
mutation(mixed.types[13].offset + 24, (view, at) => view.setUint32(at, 0xffffffff, true),
         /Array element budget/);
mutation(mixed.types[15].offset + 20, (view, at) => view.setUint32(at, 257, true),
         /Struct member budget/);
mutation(mixed.fields[0].offset + 16, (view, at) => view.setUint8(at, 0), /capabilities/);
mutation(mixed.fields[1].offset + 8, (view, at) => view.setUint32(at, 0, true), /positional ID/);
mutation(mixed.fields[5].offset + 24,
         (view, at, bytes) => bytes.set(new TextEncoder().encode('Enabled'), at),
         /Duplicate endpoint/);
mutation(mixed.commands[0].offset + 12, (view, at) => view.setUint32(at, 1, true), /Request must/);
mutation(mixed.services[0].offset + 16, (view, at) => view.setUint32(at, 10, true),
         /Response must/);
for (const [key, value] of Object.entries(codec.defaultLimits)) {
	reject(() => codec.parseDescriptor(models.get('mixed').bytes, {[key] : value + 1}), /tighten/);
}
reject(() => codec.parseDescriptor(models.get('mixed').bytes, {stringBytes : 2}), /String budget/);
reject(() => codec.parseDescriptor(models.get('mixed').bytes, {typeDepth : 0}), /Type depth/);
reject(() => codec.parseDescriptor(models.get('mixed').bytes, {expandedValueNodes : 0}),
       /Expanded/);

// Synthetic shapes exercise depth and expansion even when wireBytes is zero.
function structuralDescriptor(extra)
{
	const empty = models.get('empty').bytes;
	const records = extra.map(([ child, count, kind, name = 'x' ], index) => {
		const text = new TextEncoder().encode(name);
		const result = new Uint8Array(kind === 3 ? 32 + text.length : 28);
		const view = new DataView(result.buffer);
		result[0] = result[1] = 1;
		view.setUint32(4, result.length - 8, true);
		view.setUint32(8, 12 + index, true);
		result[12] = kind;
		if (kind === 3) {
			view.setUint32(20, count, true);
			if (count !== 0) {
				view.setUint32(24, child, true);
				view.setUint32(28, text.length, true);
				result.set(text, 32);
			}
		} else {
			view.setUint32(20, child, true);
			view.setUint32(24, count, true);
		}
		return result;
	});
	// Empty Struct has no member/name tail.
	for (let i = 0; i < extra.length; ++i) {
		if (extra[i][2] === 3 && extra[i][1] === 0)
			records[i] = records[i].slice(0, 24);
		const view = new DataView(records[i].buffer);
		view.setUint32(4, records[i].length - 8, true);
		view.setUint32(8, 12 + i, true);
	}
	const length = empty.length + records.reduce((sum, row) => sum + row.length, 0);
	const bytes = new Uint8Array(length);
	bytes.set(empty);
	let offset = empty.length;
	for (const row of records) {
		bytes.set(row, offset);
		offset += row.length;
	}
	const view = new DataView(bytes.buffer);
	view.setUint32(12, length, true);
	view.setUint32(24, 12 + extra.length, true);
	view.setUint32(56, length, true);
	view.setUint32(60, length, true);
	return rehash(bytes);
}

const nested = [ [ 2, 0, 3 ] ];
for (let i = 0; i < 32; ++i)
	nested.push([ 12 + i, 1, 3 ]);
reject(() => codec.parseDescriptor(structuralDescriptor(nested)), /Type depth/);
reject(() => codec.parseDescriptor(
           structuralDescriptor([ [ 2, 0, 3 ], [ 12, 65536, 4 ], [ 13, 4, 4 ] ])),
       /Expanded/);
const prototypeModel =
    codec.parseDescriptor(structuralDescriptor([ [ 2, 0, 3 ], [ 12, 1, 3, '__proto__' ] ]));
const prototypeValue = codec.decodeValue(prototypeModel, 13, new Uint8Array());
check(Object.getPrototypeOf(prototypeValue) === null && Object.hasOwn(prototypeValue, '__proto__'));
equalBytes(codec.encodeValue(prototypeModel, 13, prototypeValue), new Uint8Array());

// A valid model can reach the last packed u32 ID. Use the largest positional
// components, not bitwise JS arithmetic which would turn the ID negative.
function lastIdDescriptor()
{
	const record = (kind, body) => {
		const bytes = new Uint8Array(8 + body.length);
		const view = new DataView(bytes.buffer);
		bytes[0] = kind;
		bytes[1] = 1;
		view.setUint32(4, body.length, true);
		bytes.set(body, 8);
		return bytes;
	};
	const catalogs = [], fields = [];
	let catBytes = 0, fieldBytes = 0;
	for (let i = 0; i < 65536; ++i) {
		const name = new TextEncoder().encode('g' + i);
		const body = new Uint8Array(20 + name.length);
		const view = new DataView(body.buffer);
		body[0] = 1;
		view.setUint32(4, i, true);
		view.setUint32(12, i === 65535 ? 65536 : 0, true);
		view.setUint32(16, name.length, true);
		body.set(name, 20);
		const row = record(2, body);
		catalogs.push(row);
		catBytes += row.length;
	}
	for (let i = 0; i < 65536; ++i) {
		const name = new TextEncoder().encode('v' + i);
		const body = new Uint8Array(16 + name.length);
		const view = new DataView(body.buffer);
		view.setUint32(0, 65535 * 65536 + i, true);
		view.setUint32(4, 1, true);
		body[8] = 3;
		view.setUint32(12, name.length, true);
		body.set(name, 16);
		const row = record(3, body);
		fields.push(row);
		fieldBytes += row.length;
	}
	const empty = models.get('empty').bytes;
	const bytes = new Uint8Array(empty.length + catBytes + fieldBytes);
	bytes.set(empty);
	const view = new DataView(bytes.buffer);
	view.setUint32(12, bytes.length, true);
	view.setUint32(28, 65536, true);
	view.setUint32(32, 65536, true);
	view.setUint32(56, empty.length, true);
	view.setUint32(60, empty.length + catBytes, true);
	let offset = empty.length;
	for (const row of [...catalogs, ...fields]) {
		bytes.set(row, offset);
		offset += row.length;
	}
	return rehash(bytes);
}

const lastIdModel = codec.parseDescriptor(lastIdDescriptor());
check(lastIdModel.fields.at(-1).id === 0xffffffff);
equalBytes(codec.encodeFieldWrite(lastIdModel, 0xffffffff, true), Uint8Array.of(1));
reject(() => codec.encodeFieldWrite(lastIdModel, 0x100000000, true), /u32/);

// Leaves, including exact extrema and native floating representation.
const leaves = new Map([
	[ 1, [ false, true ] ],
	[ 2, [ 0, 255 ] ],
	[ 3, [ -128, 127 ] ],
	[ 4, [ 0, 65535 ] ],
	[ 5, [ -32768, 32767 ] ],
	[ 6, [ 0, 0xffffffff ] ],
	[ 7, [ -2147483648, 2147483647 ] ],
	[ 8, [ 0n, 0xffffffffffffffffn ] ],
	[ 9, [ -(1n << 63n), (1n << 63n) - 1n ] ],
	[ 10, [ -0, 0, 0.1, 1.401298464324817e-45, Infinity, -Infinity, NaN ] ],
	[ 11, [ -0, 0, 0.1, Number.MIN_VALUE, Infinity, -Infinity, NaN ] ],
]);
for (const [id, values] of leaves) {
	for (const value of values) {
		const encoded = codec.encodeValue(mixed, id, value);
		const decoded = codec.decodeValue(mixed, id, encoded);
		check(Object.is(decoded, id === 10 ? Math.fround(value) : value));
	}
	const width = mixed.types[id].wireBytes;
	reject(() => codec.decodeValue(mixed, id, new Uint8Array(width - 1)), /length/);
	reject(() => codec.decodeValue(mixed, id, new Uint8Array(width + 1)), /length/);
}
reject(() => codec.decodeValue(mixed, 1, Uint8Array.of(2)), /bool/);
reject(() => codec.encodeValue(mixed, 1, 1), /boolean/);
for (const value of [-1n, 1n << 64n, 42])
	reject(() => codec.encodeValue(mixed, 8, value));
for (const value of [-(1n << 63n) - 1n, 1n << 63n, 42])
	reject(() => codec.encodeValue(mixed, 9, value));
for (const value of [-1, 65536, 1.5, NaN, Infinity])
	reject(() => codec.encodeValue(mixed, 4, value));
reject(() => codec.encodeValue(mixed, 10, Number.MAX_VALUE), /F32/);
reject(() => codec.encodeValue(mixed, 13, [ 1, 2 ]), /array length/);
reject(() => codec.encodeValue(mixed, 15, {target : 1, rpm : 2}), /member count/);
reject(() => codec.encodeValue({...mixed}, 1, true), /validated/);
reject(() => codec.encodeServiceRequest(mixed, -1, undefined), /u32/);
reject(() => codec.encodeServiceRequest(mixed, 0.5, undefined), /u32/);
equalBytes(codec.encodeValue(mixed, 12, 255), Uint8Array.of(255)); // unknown enum

const cache = new codec.DescriptorCache(2);
check(cache.load(models.get('mixed').bytes) === cache.load(models.get('mixed').bytes));
cache.load(models.get('edge').bytes);
cache.load(models.get('empty').bytes);
check(cache.size === 2 && cache.capacity === 2);
assert.throws(() => {
	cache.capacity = 10000;
}, TypeError);
++checks;
cache.clear();
check(cache.size === 0);

const model = codec.parseDescriptor(read('descriptor.bin'));
const requestType = model.services[0].requestTypeId;
const request = codec.decodeValue(model, requestType, read('request.bin'));
check(request.config.serial === 0xffffffffffffffffn && request.config.offset === -(1n << 63n));
check(Object.is(request.config.gain, -0) && request.config.mode === -2);
equalBytes(codec.encodeServiceRequest(model, 0, request), read('request.bin'));
const response = codec.decodeServiceResponse(model, 0, read('response.bin'));
check(response.checksum === 263168 && response.config.serial === request.config.serial);
check(codec.decodeValues(model, read('values.bin'))[0].value.mode === -2);
equalBytes(codec.encodeFieldWrite(model, 0, request.config),
           codec.encodeCommandRequest(model, 0, request.config));
equalBytes(codec.encodeCommandRequest(model, 1), new Uint8Array());
equalBytes(codec.encodeServiceRequest(model, 1), new Uint8Array());
check(codec.decodeServiceResponse(model, 1, new Uint8Array()) === undefined);
reject(() => codec.encodeServiceRequest(model, 0x100000000, request), /u32/);
reject(() => codec.encodeServiceRequest(model, 65536, request), /Unknown endpoint/);

function invoke(operation, id, payload)
{
	const input = path.join(directory, 'js-input.bin');
	const output = path.join(directory, 'js-output.bin');
	fs.writeFileSync(input, payload);
	const result = spawnSync(device, [ operation, String(id), input, output ], {encoding : 'utf8'});
	assert.equal(result.status, 0, result.stderr);
	++checks;
	const [dispatch, endpoint, written, calls] = result.stdout.trim().split(/\s+/).map(Number);
	const bytes = new Uint8Array(fs.readFileSync(output));
	check(written === bytes.length);
	return {dispatch, endpoint, bytes, calls};
}

let result = invoke('service', 0, codec.encodeServiceRequest(model, 0, request));
check(result.dispatch === 0 && result.endpoint === 0 && result.calls === 1);
equalBytes(result.bytes, read('response.bin'));
for (const [multiplier, status] of [[ 0, 1 ], [ 1, 3 ], [ 2, 2 ], [ 3, 4 ]]) {
	result = invoke('service', 0, codec.encodeServiceRequest(model, 0, {...request, multiplier}));
	check(result.dispatch === 0 && result.endpoint === status && result.calls === 1 &&
	      result.bytes.length === 0);
}
result = invoke('service', 2, codec.encodeServiceRequest(model, 2, request));
check(result.dispatch === 9 && result.calls === 0 && result.bytes.length === 0);
result = invoke('service', 0, read('request.bin').subarray(1));
check(result.dispatch === 5 && result.calls === 0);
result = invoke('write', 0, codec.encodeFieldWrite(model, 0, request.config));
check(result.dispatch === 0 && result.endpoint === 0 && result.calls === 1);
check(codec.decodeFieldValue(model, 0, result.bytes).offset === request.config.offset);
result = invoke('command', 0, codec.encodeCommandRequest(model, 0, request.config));
check(result.dispatch === 0 && result.endpoint === 0 && result.calls === 1);
result = invoke('command', 1, codec.encodeCommandRequest(model, 1));
check(result.dispatch === 0 && result.endpoint === 0 && result.calls === 1);

const resourcesModel = codec.parseDescriptor(read('resources-descriptor.bin'));
const resourcesBytes = read('resources-values.bin');
const values = codec.decodeValues(resourcesModel, resourcesBytes);
check(values.length === 10 && values[0].value === 0x12345678);
check(Object.is(values[2].value, -0) && values[3].value === -2);
check(values[8].status === 1 && values[9].status === 1);
for (let end = 0; end < resourcesBytes.length; ++end)
	reject(() => codec.decodeValues(resourcesModel, resourcesBytes.subarray(0, end)));
const changed = resourcesBytes.slice();
changed[16] ^= 1;
reject(() => codec.decodeValues(resourcesModel, changed), /fingerprint/);
changed.set(resourcesBytes);
changed[60] = 2;
reject(() => codec.decodeValues(resourcesModel, changed), /status/);
changed.set(resourcesBytes);
changed[61] = 1;
reject(() => codec.decodeValues(resourcesModel, changed), /reserved/);
reject(() => codec.encodeFieldWrite(resourcesModel, 0, 1), /Read-only/);
console.log(JSON.stringify({checks, failures : 0, cppInterop : true}));
