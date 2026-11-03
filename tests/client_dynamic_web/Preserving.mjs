// Same runtime codecs and FIFO; preserve IEEE wire bits without exposing DTO storage. MIT.
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {pathToFileURL} from 'node:url';
import {FakeClock, flush} from '../client_web/Trace.mjs';

const [dynamicPath, runtimePath, descriptorPath] = process.argv.slice(2);
const {DynamicClient} = await import(pathToFileURL(dynamicPath));
const {Status} = await import(pathToFileURL(runtimePath));
const {parseDescriptor, encodeValue, encodeValuePreserving, decodeValue} =
    await import(new URL('./telemetry.js', pathToFileURL(dynamicPath)));
const descriptor = await readFile(descriptorPath), model = parseDescriptor(descriptor);
let checks = 0;
const equal = (actual, expected) => {
	++checks;
	assert.deepEqual(actual, expected);
};
const rejects = action => {
	++checks;
	assert.throws(action);
};

for (const [code, vectors] of [
         [ 10, [ 0x7fc12345n, 0xffc54321n, 0x7f812345n, 0xff854321n, 0n, 0x80000000n ] ], [
	         11,
	         [
		         0x7ff8123456789abcn, 0xfff8abcdef123456n, 0x7ff0123456789abcn, 0xfff0abcdef123456n,
		         0n, 0x8000000000000000n
	         ]
         ]]) {
	const type = model.types.find(row => row.kind === 1 && row.scalarCode === code);
	for (const bits of vectors) {
		const original = new Uint8Array(type.wireBytes), view = new DataView(original.buffer);
		if (code === 10)
			view.setUint32(0, Number(bits), true);
		else
			view.setBigUint64(0, bits, true);
		const value = decodeValue(model, type.id, original);
		equal(encodeValuePreserving(model, type.id, value, original), original);
		const changed = encodeValuePreserving(model, type.id, 1.25, original);
		equal(changed, encodeValue(model, type.id, 1.25));
		const buffer = Buffer.from(original);
		equal(encodeValuePreserving(model, type.id, value, buffer.subarray(0)), original);
		rejects(
		    () => encodeValuePreserving(model, type.id, value, new Uint8Array(type.wireBytes + 1)));
	}
	const finite = encodeValue(model, type.id, 1.25);
	const freshNaN = encodeValuePreserving(model, type.id, NaN, finite);
	equal(Number.isNaN(decodeValue(model, type.id, freshNaN)), true);
}

const dto = number => ({
	number,
	enabled : true,
	gains : [ 0, 65535, 17 ],
	mode : -123,
	large : 0xffffffffffffffffn,
	negative : -0x8000000000000000n,
	f32 : NaN,
	f64 : -0
});
const seed = () => {
	const bytes = encodeValue(model, 14, dto(1)), view = new DataView(bytes.buffer);
	equal(bytes.length, 41);
	view.setUint32(29, 0xff812345, true);
	view.setBigUint64(33, 0x7ff0123456789abcn, true);
	return bytes;
};
const old = seed(), edited = decodeValue(model, 14, old);
edited.number = 42;
const expected = new Uint8Array(old);
new DataView(expected.buffer).setUint32(0, 42, true);
equal(encodeValuePreserving(model, 14, edited, old), expected);
const noncanonical = new Uint8Array(old);
noncanonical[4] = 2;
rejects(() => encodeValuePreserving(model, 14, edited, noncanonical));
rejects(() => encodeValuePreserving(model, 14, {...edited, enabled : 2}, old));
const mutable = seed(), withGetter = decodeValue(model, 14, mutable);
Object.defineProperty(withGetter, 'number', {
	enumerable : true,
	get() {
	    mutable.fill(0);
	    return 42;
	}
});
equal(encodeValuePreserving(model, 14, withGetter, mutable), expected);

function fixture()
{
	const clock = new FakeClock(), started = [];
	const client = new DynamicClient({
		clock,
		maxPending : 1,
		backend : {
			execute : (request, context) =>
			    new Promise(resolve => started.push({request, context, resolve}))
		}
	});
	equal(client.admit(model, descriptor.subarray(0, 64), 17), Status.Ok);
	const bound = client.bind('FieldWrite', 65536);
	equal(bound.status, Status.Ok);
	return {client, clock, started, endpoint : bound.endpoint};
}

{
	const f = fixture(), original = seed(), data = decodeValue(model, 14, original);
	data.number = 42;
	const pending = f.client.writeFieldPreserving(f.endpoint, data, original);
	equal(pending.requestId > 0, true);
	original.fill(0);
	await flush();
	equal(f.started.length, 1);
	equal(f.started[0].request.payload, expected);
	equal(f.started[0].context.handoff(true), true);
	f.started[0].resolve({dispatch : 0, endpointStatus : 0, payload : new Uint8Array()});
	equal((await pending).operation.succeeded, true);
	f.client.close();
}
{
	const f = fixture(), bad = seed();
	bad[4] = 2;
	const result = f.client.writeFieldPreserving(f.endpoint, edited, bad);
	equal(result.requestId, 0);
	equal((await result).operation.status, Status.CodecError);
	await flush();
	equal(f.started.length, 0);
	equal(f.client.pendingCount, 0);
	f.client.close();
}
for (const gate of ['closed', 'revoked', 'full']) {
	const f = fixture();
	let touches = 0;
	const value = {
		get number() {
		    ++touches;
		    throw Error('must not inspect refused DTO');
		}
	};
	const original = seed();
	Object.defineProperty(original, 'buffer', {
		get() {
		    ++touches;
		    throw Error('must not inspect refused seed');
		}
	});
	if (gate === 'closed')
		f.client.close();
	if (gate === 'revoked')
		f.client.invalidate();
	if (gate === 'full')
		f.client.writeField(f.endpoint, dto(1));
	const result = f.client.writeFieldPreserving(f.endpoint, value, original);
	equal(result.requestId, 0);
	equal((await result).operation.status, gate === 'closed'    ? Status.Closed
	                                       : gate === 'revoked' ? Status.NotAdmitted
	                                                            : Status.QueueFull);
	equal(touches, 0);
	f.client.close();
	await flush();
}
{
	const f = fixture(), original = seed(), value = decodeValue(model, 14, original);
	let drained, readmit;
	Object.defineProperty(value, 'number', {
		enumerable : true,
		get() {
		    f.client.invalidate();
		    drained = f.client.drained;
		    readmit = f.client.admit(model, descriptor.subarray(0, 64), 18);
		    return 42;
		}
	});
	const result = f.client.writeFieldPreserving(f.endpoint, value, original);
	equal(result.requestId, 0);
	equal((await result).operation.status, Status.NotAdmitted);
	equal(drained, false);
	equal(readmit, Status.Busy);
	await flush();
	equal(f.started.length, 0);
	f.client.close();
}
console.log('RuntimePreserving checks=' + checks + ' failures=0');
