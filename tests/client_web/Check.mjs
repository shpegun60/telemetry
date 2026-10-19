// Deterministic queue, wire correlation and lifetime checks for both emitted runtimes. MIT.
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {pathToFileURL} from 'node:url';
import {FakeClock, flush, bindings, u32Bytes, u32Value, invoke, runTrace} from './Trace.mjs';

const runtime = await import(pathToFileURL(process.argv[2]).href);
const {DeviceClient, EncodedBackend, Status} = runtime;
let checks = 0;
const equal = (actual, expected) => {
	++checks;
	assert.deepEqual(actual, expected);
};
const truth = actual => {
	++checks;
	assert(actual);
};
const header = new Uint8Array(64);

function controlled(options = {})
{
	const clock = new FakeClock(), starts = [];
	const backend = {
		execute : (request, context) => new Promise(resolve => {
		    // This fake owns no physical I/O; abort therefore makes it quiescent.
		    context.signal.addEventListener('abort', () => resolve({}), {once : true});
		    starts.push({request, context, resolve});
		})
	};
	const client = new DeviceClient({
		backend,
		clock,
		matchesHeader : input => input.byteLength === 64,
		maxPending : 3,
		maxPayloadBytes : 12,
		timeoutMs : 100,
		...options
	});
	return {clock, starts, client, operations : bindings()};
}

const reply = (record, payload = new Uint8Array(0), endpointStatus = 0, dispatch = 0) =>
    record.resolve({payload, endpointStatus, dispatch});
const tests = JSON.parse(await readFile(new URL('./traces.json', import.meta.url)));
for (const test of tests) {
	const events = await runTrace(runtime, test);
	truth(events.length >= 4);
	truth(events.some(line => line.startsWith('DONE ')));
}
equal(Object.values(Status), Array.from({length : 18}, (_, index) => index));
{
	const {client, starts, operations} = controlled();
	equal((await client.command(operations[2], 1)).status, Status.NotAdmitted);
	equal(client.openSession(1, new Uint8Array(63)), false);
	truth(client.openSession(1, header));
	equal(client.openSession(1, header), false);
	const a = client.command(operations[2], 11), b = client.command(operations[2], 22),
	      c = client.read(operations[4]);
	await flush();
	equal(starts.length, 1);
	equal(client.pendingCount, 3);
	equal(client.snapshotBytes, 8);
	let observed = false;
	a.then(() => {
		observed = true;
		equal(starts.length, 1);
	});
	starts[0].context.handoff(true);
	reply(starts[0]);
	await flush();
	truth(observed);
	equal(starts.length, 2);
	equal(u32Value(starts[1].request.payload), 22);
	starts[1].context.handoff(true);
	reply(starts[1], new Uint8Array(0), 1);
	await flush();
	equal(starts.length, 3);
	reply(starts[2], u32Bytes(44));
	const results = await Promise.all([ a, b, c ]);
	equal(results.map(result => result.succeeded), [ true, true, true ]);
	equal(results[1].accepted, true);
	equal(results[1].executed, false);
	equal(results[2].value, 44);
	equal(client.snapshotBytes, 0);
	equal(client.pendingCount, 0);
	equal(a.cancel(), false);
}
{
	const {client, starts, operations} = controlled({maxPending : 2, maxPayloadBytes : 4});
	truth(client.openSession(1, header));
	let reads = 0, state = 17;
	const source = {
		get value() {
		    ++reads;
		    return state;
		}
	};
	const binding = {...operations[2], encode : value => u32Bytes(value.value)};
	const a = client.command(binding, source);
	state = 99;
	equal((await client.command(operations[2], 2)).status, Status.PayloadFull);
	await flush();
	equal(reads, 1);
	equal(u32Value(starts[0].request.payload), 17);
	const b = client.read(operations[4]);
	equal((await client.read(operations[4])).status, Status.QueueFull);
	equal((await client.write({read : operations[4]}, 1)).status, Status.ReadOnly);
	equal(b.cancel(), true);
	equal((await b).status, Status.Cancelled);
	equal(a.cancel(), true);
	equal((await a).status, Status.Cancelled);
	equal(client.recoveryRequired, false);
	equal(client.snapshotBytes, 0);
}
{
	const {client, starts, operations, clock} = controlled();
	truth(client.openSession(1, header));
	const a = client.command(operations[2], 11);
	await flush();
	const b = client.command(operations[2], 22, 5);
	clock.advance(5);
	await flush();
	equal((await b).status, Status.Timeout);
	equal(starts.length, 1);
	equal(client.recoveryRequired, false);
	truth(starts[0].context.handoff(true));
	a.cancel();
	equal((await a).status, Status.OutcomeUnknown);
	truth(client.recoveryRequired);
	equal(client.recoveryGeneration, 1);
	equal(client.openSession(2, header), false);
	equal(client.confirmSessionClosed(2), false);
	truth(client.confirmSessionClosed(1));
	equal(client.ready, false);
	truth(client.openSession(2, header));
	const c = client.command(operations[2], 33);
	await flush();
	reply(starts[0]);
	await flush();
	equal(client.pendingCount, 1);
	reply(starts[1]);
	equal((await c).status, Status.Ok);
	equal(client.pendingCount, 0);
	equal(starts[0].context.handoff(true), false);
}
for (const operation of [1, 2, 3, 4]) {
	const {client, starts, operations, clock} = controlled();
	truth(client.openSession(1, header));
	const a = invoke(client, operations[operation], 44, 10);
	await flush();
	truth(starts[0].context.handoff(true));
	clock.advance(10);
	await flush();
	equal((await a).status, operation === 4 ? Status.Timeout : Status.OutcomeUnknown);
	truth(client.recoveryRequired);
	equal(client.ready, false);
	equal(starts[0].context.handoff(true), false);
	equal(a.cancel(), false);
}
{
	const {client, starts, operations} = controlled();
	truth(client.openSession(1, header));
	const a = client.command(operations[2], 1), b = client.read(operations[4]);
	await flush();
	client.close();
	equal((await a).status, Status.Cancelled);
	equal((await b).status, Status.Cancelled);
	equal((await client.command(operations[2], 3)).status, Status.Closed);
	reply(starts[0]);
	await flush();
	equal(client.pendingCount, 0);
	equal(client.openSession(2, header), false);
}
{
	const {client, operations} = controlled();
	truth(client.openSession(1, header));
	let called = 0;
	const reentrant = {
		...operations[2],
		encode : () => {
		    ++called;
		    client.resetSession();
		    truth(client.openSession(2, header));
		    return u32Bytes(55);
		}
	};
	equal((await client.command(reentrant, 55)).status, Status.NotAdmitted);
	equal(called, 1);
	equal(client.pendingCount, 0);
	const bad = {
		...operations[2],
		encode : () => {
		    throw Error('Invalid input');
		}
	};
	equal((await client.command(bad, 0)).status, Status.CodecError);
	equal(client.recoveryRequired, false);
}

// Identity checks cannot roll back an epoch admitted by a reentrant callback.
{
	let client, inspecting = false;
	({client} = controlled({
		 matchesHeader : () => {
		     if (!inspecting) {
			     inspecting = true;
			     truth(client.openSession(2, header));
		     }
		     return true;
		 }
	 }));
	equal(client.openSession(1, header), false);
	equal(client.session, 2);
	truth(client.ready);
	equal(client.openSession(2, header), false);
	client.close();
}
{
	let client;
	({client} = controlled({
		 matchesHeader : () => {
		     client.close();
		     return true;
		 }
	 }));
	equal(client.openSession(1, header), false);
	equal(client.session, 0);
	equal(client.ready, false);
}

function response(request, payload = new Uint8Array(0), dispatch = 0, endpoint = 0)
{
	const frame = new Uint8Array(32 + payload.length);
	frame.set(request.subarray(0, 32));
	const view = new DataView(frame.buffer);
	frame[5] = 1;
	frame[7] = dispatch;
	frame[28] = endpoint;
	view.setUint32(20, payload.length, true);
	frame.set(payload, 32);
	return frame;
}

for (const operation of [1, 2, 3, 4]) {
	const packets = [], backend = new EncodedBackend(packet => packets.push(packet));
	const client = new DeviceClient({backend, matchesHeader : () => true, timeoutMs : 1000});
	truth(client.openSession(17, header));
	const promise = invoke(client, bindings()[operation], 0x01020304);
	await flush();
	equal(packets.length, 1);
	const packet = packets[0], view = new DataView(packet.buffer);
	equal(view.getUint32(0, true), 0x31514554);
	equal(packet[4], 1);
	equal(packet[5], 0);
	equal(packet[6], operation);
	equal(view.getUint32(8, true), 17);
	equal(view.getUint32(12, true), 1);
	equal(view.getUint32(16, true), 7);
	equal(view.getUint32(24, true), operation >= 3 ? 4 : 0);
	const payload = operation >= 3 ? u32Bytes(77) : new Uint8Array(0),
	      good = response(packet, payload);
	for (const offset of [6, 8, 12, 16]) {
		const late = good.slice();
		late[offset] ^= 1;
		equal(backend.receive(late), false);
	}
	equal(client.pendingCount, 1);
	equal(backend.receive(new Uint8Array(7)), false);
	const padded = new Uint8Array(good.length + 9);
	padded.set(good, 5);
	truth(backend.receive(new DataView(padded.buffer, 5, good.length)));
	const result = await promise;
	truth(result.succeeded);
	equal(result.value, operation >= 3 ? 77 : undefined);
	equal(backend.receive(good), false);
	equal(client.recoveryRequired, false);
}
for (const operation of [2, 4]) {
	for (const offset of [0, 4, 5, 7, 20, 24, 28, 29]) {
		const packets = [], backend = new EncodedBackend(packet => packets.push(packet));
		const client = new DeviceClient({backend, matchesHeader : () => true, timeoutMs : 1000});
		truth(client.openSession(17, header));
		const promise = invoke(client, bindings()[operation], 11);
		await flush();
		const bad = response(packets[0], operation === 4 ? u32Bytes(3) : new Uint8Array(0));
		bad[offset] = offset === 28 ? 255 : bad[offset] ^ 0x80;
		equal(backend.receive(bad), false);
		equal((await promise).status,
		      operation === 4 ? Status.MalformedReply : Status.OutcomeUnknown);
		truth(client.recoveryRequired);
	}
}
for (const [operation, dispatch, endpoint] of [[ 2, 0, 1 ], [ 2, 0, 7 ], [ 3, 0, 3 ],
                                               [ 4, 9, 0 ]]) {
	const packets = [], backend = new EncodedBackend(packet => packets.push(packet));
	const client = new DeviceClient({backend, matchesHeader : () => true});
	truth(client.openSession(17, header));
	const promise = invoke(client, bindings()[operation], 1);
	await flush();
	truth(backend.receive(response(packets[0], new Uint8Array(0), dispatch, endpoint)));
	const result = await promise;
	equal(result.status, Status.Ok);
	equal(result.dispatch, dispatch);
	equal(result.endpointStatus, endpoint);
	equal(result.accepted, operation === 2 && endpoint === 1);
	equal(client.recoveryRequired, false);
}
for (const [operation, fragmentBytes, expected] of [[ 2, 24, Status.Cancelled ],
                                                    [ 2, 48, Status.OutcomeUnknown ],
                                                    [ 4, 48, Status.Timeout ]]) {
	const clock = new FakeClock(), packets = [];
	const backend = new runtime.RpcBackend(packet => {
		packets.push(packet);
		return new Promise(() => {
		                   });
	}, {fragmentBytes, ackTimeoutMs : 100, pollIntervalMs : 0});
	const client = new DeviceClient({backend, clock, matchesHeader : () => true, timeoutMs : 10});
	truth(client.openSession(1, header));
	const promise = invoke(client, bindings()[operation], 77);
	await flush();
	equal(packets.length, 1);
	if (operation === 4)
		clock.advance(10);
	else
		truth(promise.cancel());
	equal((await promise).status, expected);
	equal(client.recoveryRequired, fragmentBytes !== 24);
	equal(client.pendingCount, 0);
	equal(client.snapshotBytes, 0);
	await flush();
	if (client.recoveryRequired) {
		truth(client.confirmSessionClosed(1));
		truth(client.openSession(2, header));
	} else {
		truth(client.ready);
		equal(client.session, 1);
		client.resetSession();
		truth(client.confirmSessionClosed(1));
		truth(client.openSession(2, header));
	}
	// Only the explicit assertion that the old physical transport closed/drained
	// permits a fresh session while its obsolete Promise remains unresolved.
	const second = client.read(bindings()[4]);
	await flush();
	equal(packets.length, 2);
	second.cancel();
	await second;
	client.close();
}
{
	let calls = 0;
	const backend = new runtime.RpcBackend(() => {
		++calls;
		return new Uint8Array(0);
	}, {maxRequestBytes : 24});
	const client = new DeviceClient({backend, matchesHeader : () => true});
	truth(client.openSession(1, header));
	equal((await client.command(bindings()[2], 33)).status, Status.BufferTooSmall);
	equal(calls, 0);
	equal(client.recoveryRequired, false);
	client.close();
}
{
	const {client, starts, operations} = controlled();
	truth(client.openSession(0xffffffff, header));
	client.nextId = 0xffffffff;
	const last = client.command(operations[2], 1);
	await flush();
	equal(last.requestId, 0xffffffff);
	equal((await client.command(operations[2], 2)).status, Status.IdsExhausted);
	reply(starts[0]);
	await last;
	equal(client.openSession(0x100000000, header), false);
}
{
	const {client, starts, operations} = controlled();
	truth(client.openSession(1, header));
	let called = 0;
	const otherSchema = {
		...operations[2],
		fingerprint : 1n,
		encode : value => {
		    ++called;
		    return u32Bytes(value);
		}
	};
	equal((await client.command(otherSchema, 1)).status, Status.NotAdmitted);
	equal(called, 0);
	equal(starts.length, 0);
	client.nextId = 0xffffffff;
	let inner;
	const reentrant = {
		...operations[2],
		encode : value => {
		    inner = client.command(operations[2], 22);
		    return u32Bytes(value);
		}
	};
	equal((await client.command(reentrant, 11)).status, Status.IdsExhausted);
	await flush();
	equal(starts.length, 1);
	equal(inner.requestId, 0xffffffff);
	equal(u32Value(starts[0].request.payload), 22);
	reply(starts[0]);
	equal((await inner).status, Status.Ok);
}
{
	const {client, starts, operations} = controlled();
	truth(client.openSession(1, header));
	const a = client.command(operations[2], 11), b = client.command(operations[2], 22);
	await flush();
	starts[0].context.signal.addEventListener('abort', () => client.process());
	let observed = false;
	a.then(() => {
		observed = true;
		equal(starts.length, 1);
	});
	truth(a.cancel());
	await flush();
	truth(observed);
	equal(starts.length, 2);
	reply(starts[1]);
	equal((await b).status, Status.Ok);
}
console.log(`async web checks=${checks} failures=0`);
