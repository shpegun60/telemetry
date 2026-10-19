// Deterministic RPC first-read latency, status polling and retry lifetimes. MIT.
import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';
import {FakeClock, flush, bindings, u32Bytes} from './Trace.mjs';

const {DeviceClient, RpcBackend, Status} = await import(pathToFileURL(process.argv[2]).href);
const operations = bindings(), header = new Uint8Array(64), timelines = [];
let checks = 0;
const equal = (actual, expected) => {
	++checks;
	assert.deepEqual(actual, expected);
};
const truth = value => {
	++checks;
	assert(value);
};
const data = bytes => new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);

function writeAck(packet)
{
	const input = data(packet), offset = input.getBigUint64(5, true);
	const transaction = offset ? offset >> 32n : BigInt(input.getUint32(24, true));
	const count = input.getUint16(14, true), reply = new Uint8Array(14), output = data(reply);
	output.setBigUint64(1, (transaction << 32n) | ((offset & 0xffffffffn) + BigInt(count)), true);
	output.setUint32(9, count, true);
	reply[13] = packet[13];
	return reply;
}

function resourceReply(bytes, next, eof)
{
	const reply = new Uint8Array(12 + bytes.length), output = data(reply);
	output.setBigUint64(1, next, true);
	reply[9] = eof ? 1 : 0;
	output.setUint16(10, bytes.length, true);
	reply.set(bytes, 12);
	return reply;
}

function statusReply(request, state, continuation)
{
	const input = data(request), body = new Uint8Array(32), output = data(body);
	output.setUint32(0, 0x53505254, true);
	output.setUint16(4, 1, true);
	output.setUint32(8, input.getUint32(8, true), true);
	output.setUint32(12, input.getUint32(12, true), true);
	output.setUint32(16, state === 4 && continuation ? 4 : 0, true);
	body[20] = request[20];
	body[21] = state;
	output.setUint32(24, request.length, true);
	output.setUint32(28, request.length, true);
	const next = state === 4 ? (BigInt(input.getUint32(8, true)) << 32n) | 32n : 0n;
	return resourceReply(body, next, state !== 4 || !continuation);
}

function harness(name,
                 {states = [ 4 ], continuation = false, dropWrite = false, dropRead = false} = {})
{
	const clock = new FakeClock(), calls = [], trace = [];
	let request, nextState = 0, reads = 0, droppedWrite, droppedRead;
	const backend = new RpcBackend((packet, context) => {
		const kind = packet[0] === 4 ? 'write' : 'read';
		const cursor = data(packet).getBigUint64(5, true);
		calls.push({packet : packet.slice(), context});
		trace.push({kind, at : clock.now(), cursor : cursor.toString()});
		const omit = () => new Promise((resolve, reject) => {
			context.signal.addEventListener('abort', () => reject(Error('ACK omitted')),
			                                {once : true});
		});
		if (kind === 'write') {
			equal(packet[13], 1); // This fixture fits one final WRITE.
			request ??= packet.slice(16);
			if (dropWrite && droppedWrite === undefined) {
				droppedWrite = calls.length - 1;
				return omit();
			}
			if (droppedWrite !== undefined)
				equal(packet, calls[droppedWrite].packet);
			return writeAck(packet);
		}
		if (cursor !== 0n) {
			const transaction = BigInt(data(request).getUint32(8, true));
			equal(cursor, (transaction << 32n) | 32n);
			return resourceReply(u32Bytes(77), (transaction << 32n) | 36n, true);
		}
		++reads;
		if (dropRead && reads === 2) {
			droppedRead = calls.length - 1;
			return omit();
		}
		if (droppedRead !== undefined && reads === 3)
			equal(packet, calls[droppedRead].packet);
		return statusReply(request, states[nextState++], continuation);
	}, {fragmentBytes : 64, ackTimeoutMs : 5, maxRetries : 1});
	const client = new DeviceClient({backend, clock, matchesHeader : () => true, timeoutMs : 200});
	truth(client.openSession(1, header));
	const timeline = {name, trace};
	timelines.push(timeline);
	return {clock, calls, client, timeline};
}

async function advanceUntil(clock, predicate)
{
	await flush();
	for (let elapsed = 0; !predicate(); ++elapsed) {
		assert(elapsed < 200, 'Deterministic polling progress bound');
		clock.advance(1);
		await flush();
	}
}

async function completed(name, options = {})
{
	const h = harness(name, options);
	const pending = options.continuation ? h.client.service(operations[3], 11)
	                                     : h.client.command(operations[2], 11);
	let result;
	pending.then(value => {
		result = value;
	});
	await advanceUntil(h.clock, () => result !== undefined);
	equal(result.status, Status.Ok);
	truth(result.succeeded);
	if (options.continuation)
		equal(result.value, 77);
	h.timeline.completedAt = h.clock.now();
	h.timeline.packets = h.calls.length;
	for (const call of h.calls) {
		equal(call.context.session, 1);
		equal(call.context.requestId, pending.requestId);
	}
	h.client.close();
	await flush();
	truth(h.client.drained);
	equal(h.clock.timers.size, 0);
	return h.timeline;
}

const ready = await completed('ready');
const polling = await completed('pending-running', {states : [ 2, 3, 4 ]});
const continuation = await completed('continuation', {continuation : true});
const writeRetry = await completed('write-retry', {dropWrite : true});
const readRetry = await completed('read-retry', {states : [ 2, 3, 4 ], dropRead : true});

// Cancel while waiting for the next Pending poll: no new exchange or live timer.
const cancelled = harness('cancel-polling', {states : [ 2 ]});
const pending = cancelled.client.read(operations[4]);
await advanceUntil(cancelled.clock, () => cancelled.calls.some(c => c.packet[0] === 3));
await flush();
truth(pending.cancel());
equal((await pending).status, Status.Cancelled);
await flush();
truth(cancelled.client.recoveryRequired); // Final WRITE may retain a server result.
equal(cancelled.calls.length, 2);
cancelled.clock.advance(100);
await flush();
equal(cancelled.calls.length, 2);
cancelled.timeline.cancelledAt = cancelled.clock.now() - 100;
cancelled.timeline.packets = cancelled.calls.length;
cancelled.client.close();
await flush();
truth(cancelled.client.drained);
equal(cancelled.clock.timers.size, 0);

// Emit before timing assertions so a baseline failure retains its actual timeline.
console.log('rpc polling timelines=' + JSON.stringify(timelines));
equal(ready.trace.map(call => call.at), [ 0, 0 ]);
equal(ready.completedAt, 0);
equal(ready.packets, 2);
equal(polling.trace.map(call => call.at), [ 0, 0, 10, 20 ]);
equal(polling.packets, 4);
equal(continuation.trace.map(call => call.at), [ 0, 0, 0 ]);
equal(continuation.packets, 3);
equal(writeRetry.trace.map(call => call.at), [ 0, 5, 5 ]);
equal(writeRetry.packets, 3);
equal(readRetry.trace.map(call => call.at), [ 0, 0, 10, 15, 25 ]);
equal(readRetry.packets, 5);
equal(cancelled.timeline.trace.map(call => call.at), [ 0, 0 ]);
equal(cancelled.timeline.cancelledAt, 0);
console.log(`rpc polling checks=${checks} failures=0`);
