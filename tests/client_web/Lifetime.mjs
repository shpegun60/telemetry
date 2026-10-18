// Physical transport ownership is independent of logical request completion. MIT.
import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';
import {FakeClock, flush, bindings, u32Bytes} from './Trace.mjs';

const {DeviceClient, RpcBackend, EncodedBackend, Status} =
    await import(pathToFileURL(process.argv[2]).href);
let checks = 0;
const equal = (actual, expected) => {
	++checks;
	assert.deepEqual(actual, expected);
};
const truth = actual => {
	++checks;
	assert(actual);
};
const header = new Uint8Array(64), ops = bindings();
const data = bytes => new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);

function rpcAck(packet)
{
	const source = data(packet), cursor = source.getBigUint64(5, true);
	const transaction = cursor ? cursor >> 32n : BigInt(source.getUint32(24, true));
	const count = source.getUint16(14, true), reply = new Uint8Array(14), view = data(reply);
	view.setBigUint64(1, (transaction << 32n) | ((cursor & 0xffffffffn) + BigInt(count)), true);
	view.setUint32(9, count, true);
	reply[13] = packet[13];
	return reply;
}

function rpcReadReply(firstWrite, value)
{
	const request = data(firstWrite.subarray(16)), payload = u32Bytes(value);
	const body = new Uint8Array(32 + payload.length), view = data(body);
	view.setUint32(0, 0x53505254, true);
	view.setUint16(4, 1, true);
	view.setUint32(8, request.getUint32(8, true), true);
	view.setUint32(12, request.getUint32(12, true), true);
	view.setUint32(16, payload.length, true);
	body[20] = request.getUint8(20);
	body[21] = 4;
	view.setUint32(24, 24, true);
	view.setUint32(28, 24, true);
	body.set(payload, 32);
	const reply = new Uint8Array(12 + body.length), out = data(reply);
	out.setBigUint64(1, (BigInt(request.getUint32(8, true)) << 32n) | BigInt(body.length), true);
	reply[9] = 1;
	out.setUint16(10, body.length, true);
	reply.set(body, 12);
	return reply;
}

function encodedReply(packet, value)
{
	const payload = u32Bytes(value), reply = new Uint8Array(32 + payload.length);
	reply.set(packet.subarray(0, 32));
	reply[5] = 1;
	data(reply).setUint32(20, payload.length, true);
	reply.set(payload, 32);
	return reply;
}

function rpcControlled(options = {})
{
	const clock = new FakeClock(), calls = [];
	const backend = new RpcBackend(
	    (packet, context) => new Promise((resolve, reject) => {
		    calls.push({packet, context, resolve, reject});
	    }),
	    {fragmentBytes : 24, ackTimeoutMs : 10, pollIntervalMs : 0, maxRetries : 1, ...options});
	const client = new DeviceClient({backend, clock, matchesHeader : () => true, timeoutMs : 1000});
	truth(client.openSession(1, header));
	return {clock, calls, backend, client};
}

async function finishRead(calls, index, handle, value)
{
	equal(calls[index].packet[0], 4);
	calls[index].resolve(rpcAck(calls[index].packet));
	await flush();
	equal(calls.length, index + 2);
	equal(calls[index + 1].packet[0], 3);
	calls[index + 1].resolve(rpcReadReply(calls[index].packet, value));
	const result = await handle;
	equal(result.status, Status.Ok);
	equal(result.value, value);
	await flush();
}

// Ignoring abort cannot overlap an old non-final fragment with any new request.
for (const lateReject of [false, true]) {
	const {clock, calls, client} = rpcControlled();
	const first = client.command(ops[2], 11);
	let completions = 0;
	first.then(() => ++completions);
	await flush();
	equal(calls.length, 1);
	equal(calls[0].packet[13], 0);
	truth(first.cancel());
	equal((await first).status, Status.Cancelled);
	truth(calls[0].context.signal.aborted);
	equal(client.recoveryRequired, false);
	const timed = client.read(ops[4], 5);
	await flush();
	equal(calls.length, 1);
	clock.advance(5);
	equal((await timed).status, Status.Timeout);
	// Repeated enqueue/cancel never starts or accumulates another execute waiter.
	for (let index = 0; index < 16; ++index) {
		const waiting = client.read(ops[4]);
		await flush();
		truth(waiting.cancel());
		equal((await waiting).status, Status.Cancelled);
	}
	equal(calls.length, 1);
	equal(client.pendingCount, 0);
	equal(client.snapshotBytes, 0);
	const next = client.read(ops[4]);
	await flush();
	equal(calls.length, 1);
	if (lateReject)
		calls[0].reject(Error('old transport stopped'));
	else
		calls[0].resolve(rpcAck(calls[0].packet));
	await flush();
	equal(calls.length, 2);
	equal(data(calls[1].packet).getUint32(24, true), next.requestId);
	await finishRead(calls, 1, next, 73);
	equal(completions, 1);
	equal(client.recoveryRequired, false);
	truth(client.ready);
	client.close();
	await flush();
	truth(client.drained);
}

// An ACK timer may request abort, but retry must await the old exchange's settlement.
for (const lateReject of [false, true]) {
	const {clock, calls, client} = rpcControlled();
	const pending = client.command(ops[2], 11);
	await flush();
	const initial = calls[0].packet.slice();
	clock.advance(10);
	await flush();
	truth(calls[0].context.signal.aborted);
	equal(calls.length, 1);
	if (lateReject)
		calls[0].reject(Error('ACK aborted physically'));
	else
		calls[0].resolve(rpcAck(calls[0].packet));
	await flush();
	equal(calls.length, 2);
	equal(calls[1].packet, initial);
	truth(pending.cancel());
	equal((await pending).status, Status.Cancelled);
	const next = client.read(ops[4]);
	await flush();
	equal(calls.length, 2);
	calls[1].resolve(rpcAck(calls[1].packet));
	await flush();
	equal(calls.length, 3);
	await finishRead(calls, 2, next, 91);
	client.close();
	await flush();
	truth(client.drained);
}

// A forever-pending exchange does not delay logical deadlines or close results.
// Reopening requires actual external close/drain and an explicit confirmation.
for (const lateReject of [false, true]) {
	const {clock, calls, client, backend} = rpcControlled();
	const first = client.command(ops[2], 11, 20);
	await flush();
	clock.advance(10);
	await flush();
	truth(calls[0].context.signal.aborted);
	equal(calls.length, 1);
	clock.advance(10);
	equal((await first).status, Status.Timeout);
	await flush();
	equal(calls.length, 1);
	equal(client.recoveryRequired, false);
	equal(client.openSession(2, header), false);
	equal(client.confirmSessionClosed(1), false); // still admitted
	client.resetSession();
	equal(client.confirmSessionClosed(2), false);
	equal(backend.confirmSessionClosed(2), false);
	truth(client.confirmSessionClosed(1)); // test port has now closed/drained its old session
	equal(client.openSession(1, header), false);
	truth(client.openSession(2, header));
	const current = client.command(ops[2], 22);
	await flush();
	equal(calls.length, 2);
	const queued = client.read(ops[4]);
	if (lateReject)
		calls[0].reject(Error('closed old session'));
	else
		calls[0].resolve(rpcAck(calls[0].packet));
	await flush();
	equal(calls.length, 2); // old finally must not release the new invocation
	equal(client.recoveryRequired, false);
	truth(current.cancel());
	equal((await current).status, Status.Cancelled);
	await flush();
	equal(calls.length, 2);
	client.close();
	equal((await queued).status, Status.Cancelled);
	equal(client.drained, false);
	truth(client.confirmSessionClosed(2));
	truth(client.drained);
}

// A backend's refusal/exception cannot partially release the client ownership token.
for (const throws of [false, true]) {
	const clock = new FakeClock(), starts = [];
	let allow = false;
	const backend = {
		execute : (request, context) =>
		    new Promise(resolve => starts.push({request, context, resolve})),
		confirmSessionClosed : () => {
		    if (!allow && throws)
			    throw Error('not drained');
		    return allow;
		}
	};
	const client = new DeviceClient({backend, clock, matchesHeader : () => true});
	truth(client.openSession(1, header));
	const first = client.command(ops[2], 11);
	await flush();
	client.resetSession();
	equal((await first).status, Status.SessionLost);
	equal(client.confirmSessionClosed(1), false);
	equal(client.openSession(2, header), false);
	allow = true;
	truth(client.confirmSessionClosed(1));
	truth(client.openSession(2, header));
	client.close();
	truth(client.drained);
}

// Confirmation is an external callback: nested confirmation/admission must not
// replace the exact invocation that the outer call is about to detach.
{
	const starts = [];
	let client, nested = false, opening = false;
	const backend = {
		execute : (request, context) =>
		    new Promise(resolve => starts.push({request, context, resolve})),
		confirmSessionClosed : generation => {
		    nested = client.confirmSessionClosed(generation);
		    opening = client.openSession(generation + 1, header);
		    return true;
		}
	};
	client = new DeviceClient({backend, matchesHeader : () => true});
	truth(client.openSession(1, header));
	const old = client.command(ops[2], 11);
	await flush();
	client.resetSession();
	equal((await old).status, Status.SessionLost);
	truth(client.confirmSessionClosed(1));
	equal(nested, false);
	equal(opening, false);
	truth(client.openSession(2, header));
	const current = client.command(ops[2], 22);
	await flush();
	equal(starts.length, 2);
	current.cancel();
	equal((await current).status, Status.Cancelled);
	const queued = client.read(ops[4]);
	await flush();
	equal(starts.length, 2);
	client.close();
	equal((await queued).status, Status.Cancelled);
	equal(client.drained, false);
	truth(client.confirmSessionClosed(2));
	truth(client.drained);
}

// Truthy values and async hooks are not synchronous evidence of completed drain.
for (const invalid of [() => 1, () => ({}), () => Promise.resolve(true),
                      () => Promise.reject(Error('async drain failed'))]) {
	let starts = 0;
	const backend = {
		execute : () => {
		    ++starts;
		    return new Promise(() => {
		                       });
		},
		confirmSessionClosed : invalid
	};
	const client = new DeviceClient({backend, matchesHeader : () => true});
	truth(client.openSession(1, header));
	const old = client.command(ops[2], 11);
	await flush();
	client.resetSession();
	equal((await old).status, Status.SessionLost);
	equal(client.confirmSessionClosed(1), false);
	equal(client.openSession(2, header), false);
	await flush();
	equal(starts, 1);
	client.close();
	equal(client.drained, false);
}

// A physically completed normal result permits immediate awaited re-admission.
{
	const backend = {
		execute : async () => ({dispatch : 0, endpointStatus : 0, payload : u32Bytes(5)})
	};
	const client = new DeviceClient({backend, matchesHeader : () => true});
	truth(client.openSession(1, header));
	equal((await client.read(ops[4])).value, 5);
	truth(client.openSession(2, header));
	client.close();
	truth(client.drained);
}

// Encoded async send and fromExchange must also retain the actual transport lease.
for (const fromExchange of [false, true]) {
	const clock = new FakeClock(), calls = [];
	const send = (packet, context) =>
	    new Promise((resolve, reject) => calls.push({packet, context, resolve, reject}));
	const backend = fromExchange ? EncodedBackend.fromExchange(send) : new EncodedBackend(send);
	const client = new DeviceClient({backend, clock, matchesHeader : () => true});
	truth(client.openSession(1, header));
	const first = client.command(ops[2], 11);
	await flush();
	truth(first.cancel());
	equal((await first).status, Status.OutcomeUnknown);
	truth(client.recoveryRequired);
	equal(client.openSession(2, header), false);
	truth(client.confirmSessionClosed(1));
	truth(client.openSession(2, header));
	const current = client.command(ops[2], 22);
	await flush();
	equal(calls.length, 2);
	const waiting = client.read(ops[4]);
	if (fromExchange) {
		const old = calls[0].packet.slice();
		old[5] = 1;
		data(old).setUint32(20, 0, true);
		calls[0].resolve(old.subarray(0, 32));
	} else {
		calls[0].reject(Error('old send closed'));
	}
	await flush();
	equal(calls.length, 2);
	equal(client.pendingCount, 2);
	equal(client.recoveryRequired, false);
	client.close();
	equal((await current).status, Status.OutcomeUnknown);
	equal((await waiting).status, Status.Cancelled);
	equal(client.drained, false);
	calls[1].reject(Error('new transport stopped'));
	await flush();
	truth(client.drained);
}

// A checked reply is already known even if the asynchronous send still owns I/O.
// Neither a later deadline nor send failure may turn that result into Unknown.
for (const rejectTransfer of [false, true]) {
	const clock = new FakeClock(), calls = [];
	const backend = new EncodedBackend((packet, context) => new Promise((resolve, reject) => {
		                                   calls.push({packet, context, resolve, reject});
	                                   }));
	const client = new DeviceClient({backend, clock, matchesHeader : () => true});
	truth(client.openSession(1, header));
	const first = client.read(ops[4], 10), expired = client.read(ops[4], 5);
	let completed = 0;
	first.then(() => ++completed);
	await flush();
	truth(backend.receive(encodedReply(calls[0].packet, 71)));
	const known = await first;
	equal(known.status, Status.Ok);
	equal(known.value, 71);
	await flush();
	equal(completed, 1);
	equal(calls.length, 1);
	clock.advance(10);
	equal((await expired).status, Status.Timeout);
	equal((await first).status, Status.Ok);
	equal(client.recoveryRequired, false);
	const next = client.read(ops[4], 100);
	await flush();
	equal(calls.length, 1);
	if (rejectTransfer)
		calls[0].reject(Error('send ended after checked reply'));
	else
		calls[0].resolve();
	await flush();
	equal(calls.length, 2); // backend token cleanup must precede the next execute
	truth(backend.receive(encodedReply(calls[1].packet, 72)));
	equal((await next).value, 72);
	client.close();
	equal(client.drained, false);
	calls[1].resolve();
	await flush();
	truth(client.drained);
	equal(completed, 1);
}

// Late release of an already-completed old result cannot detach a fresh owner.
{
	const calls = [];
	const backend = new EncodedBackend((packet, context) => new Promise((resolve, reject) => {
		                                   calls.push({packet, context, resolve, reject});
	                                   }));
	const client = new DeviceClient({backend, matchesHeader : () => true});
	truth(client.openSession(1, header));
	const old = client.read(ops[4]);
	await flush();
	truth(backend.receive(encodedReply(calls[0].packet, 1)));
	equal((await old).status, Status.Ok);
	client.resetSession();
	truth(client.confirmSessionClosed(1));
	truth(client.openSession(2, header));
	const current = client.read(ops[4]);
	await flush();
	const queued = client.read(ops[4]);
	calls[0].reject(Error('old checked-result send closed'));
	await flush();
	equal(calls.length, 2);
	equal(client.pendingCount, 2);
	truth(backend.receive(encodedReply(calls[1].packet, 2)));
	equal((await current).value, 2);
	await flush();
	equal(calls.length, 2);
	calls[1].resolve();
	await flush();
	equal(calls.length, 3);
	truth(backend.receive(encodedReply(calls[2].packet, 3)));
	equal((await queued).value, 3);
	calls[2].resolve();
	await flush();
	client.close();
	truth(client.drained);
}

// Both settlement paths of a custom backend's release fence prove physical drain.
for (const rejectRelease of [false, true]) {
	const fences = [];
	const backend = {
		execute : async () => ({
			dispatch : 0,
			endpointStatus : 0,
			payload : u32Bytes(8),
			released : new Promise((resolve, reject) => fences.push({resolve, reject}))
		})
	};
	const client = new DeviceClient({backend, matchesHeader : () => true});
	truth(client.openSession(1, header));
	equal((await client.read(ops[4])).status, Status.Ok);
	const next = client.read(ops[4]);
	await flush();
	equal(fences.length, 1);
	if (rejectRelease)
		fences[0].reject(Error('physical release with transport failure'));
	else
		fences[0].resolve();
	equal((await next).status, Status.Ok);
	equal(fences.length, 2);
	equal(client.recoveryRequired, false);
	client.close();
	equal(client.drained, false);
	fences[1].resolve();
	await flush();
	truth(client.drained);
}

console.log(`async web lifetime checks=${checks} failures=0`);
