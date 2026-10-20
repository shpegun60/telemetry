// Opt-in TEQ1 retransmission with controlled time and physical transport ownership. MIT.
import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';
import {FakeClock, flush, bindings, invoke, u32Bytes} from './Trace.mjs';

const {DeviceClient, RetryingEncodedBackend, EncodedBackend, Status} =
    await import(pathToFileURL(process.argv[2]).href);
let checks = 0;
const equal = (actual, expected) => {
	++checks;
	assert.deepEqual(actual, expected);
};
const truth = value => {
	++checks;
	assert(value);
};
const header = new Uint8Array(64), ops = bindings();
const data = bytes => new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
const deferred = () => {
	let resolve, reject;
	const promise = new Promise((yes, no) => {
		resolve = yes;
		reject = no;
	});
	return {promise, resolve, reject};
};

function reply(packet, endpointStatus = 0, dispatch = 0)
{
	const operation = packet[6],
	      payload = dispatch === 0 && endpointStatus === 0 && (operation === 3 || operation === 4)
	                    ? u32Bytes(91)
	                    : new Uint8Array();
	const result = new Uint8Array(32 + payload.length);
	result.set(packet.subarray(0, 32));
	result[5] = 1;
	result[7] = dispatch;
	result[28] = endpointStatus;
	data(result).setUint32(20, payload.length, true);
	result.set(payload, 32);
	return result;
}

function fixture(send, options = {}, timeoutMs = 1000)
{
	const clock = new FakeClock(), calls = [];
	const backend = new RetryingEncodedBackend((packet, context) => {
		const call = {packet, snapshot : packet.slice(), context, at : clock.now()};
		calls.push(call);
		return send?.(call, calls.length, backend);
	}, options);
	let handoffs = 0;
	const execute = backend.execute.bind(backend);
	backend.execute = (request, context) => execute(request, {
		...context,
		handoff : mayExecute => {
		    ++handoffs;
		    return context.handoff(mayExecute);
		}
	});
	const client = new DeviceClient({backend, clock, matchesHeader : () => true, timeoutMs});
	truth(client.openSession(1, header));
	return {clock, calls, backend, client, handoffs : () => handoffs};
}

async function tick(f, milliseconds)
{
	f.clock.advance(milliseconds);
	await flush();
}

for (const invalid of [0, -1, NaN, Infinity, 0.5, 0x80000000]) {
	++checks;
	assert.throws(() => new RetryingEncodedBackend(() => {
	                                               }, {replyTimeoutMs : invalid}), RangeError);
}
for (const invalid of [-1, NaN, Infinity, 0.5, 0x100000000]) {
	++checks;
	assert.throws(() => new RetryingEncodedBackend(() => {
	                                               }, {maxRetries : invalid}), RangeError);
}
// Defaults are initial + three retries, with one handoff and identical bytes per send.
for (const operation of [1, 2, 3, 4]) {
	const f = fixture();
	const result = invoke(f.client, ops[operation], 42);
	await flush();
	for (let attempt = 1; attempt <= 4; ++attempt) {
		equal(f.calls.length, attempt);
		equal(f.handoffs(), attempt);
		equal(f.calls[attempt - 1].snapshot, f.calls[0].snapshot);
		equal(f.calls[attempt - 1].at, (attempt - 1) * 100);
		equal(data(f.calls[attempt - 1].snapshot).getUint32(12, true), result.requestId);
		await tick(f, 100);
	}
	equal((await result).status, operation === 4 ? Status.Timeout : Status.OutcomeUnknown);
	truth(f.client.recoveryRequired);
	equal(f.client.pendingCount, 0);
	equal(f.clock.timers.size, 0);
	f.client.close();
	truth(f.client.drained);
}
// Retries preserve canonical bytes even when the port changes its attempt's own copy.
for (const operation of [1, 2, 3, 4]) {
	const f = fixture((call, attempt, backend) => {
		if (attempt === 1)
			call.packet.fill(0xff);
		else
			backend.receive(reply(call.snapshot));
	}, {replyTimeoutMs : 10, maxRetries : 1});
	const result = invoke(f.client, ops[operation], 42);
	await flush();
	await tick(f, 10);
	const value = await result;
	equal(value.status, Status.Ok);
	truth(value.succeeded);
	equal(f.calls.length, 2);
	equal(f.handoffs(), 2);
	equal(f.calls[1].snapshot, f.calls[0].snapshot);
	equal(value.value, operation === 3 || operation === 4 ? 91 : undefined);
	await tick(f, 100);
	equal(f.calls.length, 2);
	equal(f.clock.timers.size, 0);
	f.client.close();
	truth(f.client.drained);
}
// Timeout does not permit an overlapping send. Only physical release enables retry.
{
	const first = deferred();
	const f = fixture((call, attempt, backend) =>
	                      attempt === 1 ? first.promise : backend.receive(reply(call.snapshot)),
	                  {replyTimeoutMs : 10, maxRetries : 1});
	const result = f.client.read(ops[4]);
	await flush();
	await tick(f, 40);
	equal(f.calls.length, 1);
	equal(f.handoffs(), 1);
	first.resolve();
	await flush();
	equal(f.calls.length, 2);
	equal(f.calls[1].at, 40);
	truth((await result).succeeded);
	equal(f.clock.timers.size, 0);
	f.client.close();
	truth(f.client.drained);
}
// A late but matching checked reply wins while TX is still held, for resolve and reject.
for (const reject of [false, true]) {
	const first = deferred();
	const f = fixture((call, attempt, backend) =>
	                      attempt === 1 ? first.promise : backend.receive(reply(call.snapshot)),
	                  {replyTimeoutMs : 10});
	const result = f.client.command(ops[2], 1), next = f.client.read(ops[4]);
	await flush();
	await tick(f, 10);
	truth(f.backend.receive(reply(f.calls[0].snapshot)));
	truth((await result).executed);
	equal(f.calls.length, 1);
	await tick(f, 100);
	equal(f.calls.length, 1);
	if (reject)
		first.reject(Error('late TX rejection'));
	else
		first.resolve();
	await flush();
	equal(f.calls.length, 2);
	truth((await next).succeeded);
	equal(f.clock.timers.size, 0);
	f.client.close();
	truth(f.client.drained);
}
// Overall enqueue deadlines dominate the retry budget and stop all future handoffs.
{
	const f = fixture(undefined, {replyTimeoutMs : 10, maxRetries : 3}, 25);
	const result = f.client.command(ops[2], 1);
	await flush();
	await tick(f, 10);
	await tick(f, 10);
	equal(f.calls.length, 3);
	await tick(f, 5);
	equal((await result).status, Status.OutcomeUnknown);
	await tick(f, 100);
	equal(f.calls.length, 3);
	equal(f.clock.timers.size, 0);
	f.client.close();
	truth(f.client.drained);
}
// A stuck TX still allows timely logical deadline/cancel and queued deadline/cancel.
{
	const stuck = deferred();
	const f = fixture(() => stuck.promise, {replyTimeoutMs : 10});
	const first = f.client.command(ops[2], 1, 50), expired = f.client.read(ops[4], 15),
	      cancelled = f.client.read(ops[4]);
	await flush();
	truth(cancelled.cancel());
	equal((await cancelled).status, Status.Cancelled);
	await tick(f, 15);
	equal((await expired).status, Status.Timeout);
	equal(f.calls.length, 1);
	await tick(f, 35);
	equal((await first).status, Status.OutcomeUnknown);
	truth(f.client.recoveryRequired);
	equal(f.calls.length, 1);
	equal(f.clock.timers.size, 0);
	f.client.close();
	equal(f.client.drained, false);
	stuck.resolve();
	await flush();
	truth(f.client.drained);
}
// Explicit old-session physical close permits a newer lease; late old settlement cannot clear it.
{
	const old = deferred(), current = deferred();
	const f =
	    fixture((call, attempt, backend) => attempt === 1   ? old.promise
	                                        : attempt === 2 ? current.promise
	                                                        : backend.receive(reply(call.snapshot)),
	            {replyTimeoutMs : 10});
	const first = f.client.command(ops[2], 1);
	await flush();
	truth(first.cancel());
	equal((await first).status, Status.OutcomeUnknown);
	f.client.resetSession();
	truth(f.client.confirmSessionClosed(1));
	truth(f.client.openSession(2, header));
	const second = f.client.command(ops[2], 2);
	await flush();
	equal(f.calls.length, 2);
	old.reject(Error('old transport stopped'));
	await flush();
	equal(f.calls.length, 2);
	truth(second.cancel());
	equal((await second).status, Status.OutcomeUnknown);
	f.client.close();
	equal(f.client.drained, false);
	current.resolve();
	await flush();
	truth(f.client.drained);
	equal(f.clock.timers.size, 0);
}
// Stale correlation never settles/retries early. A matching malformed result is terminal.
for (const field of [6, 8, 12, 16]) {
	const f = fixture(undefined, {replyTimeoutMs : 10});
	const result = f.client.read(ops[4]);
	await flush();
	const stale = reply(f.calls[0].snapshot);
	stale[field] ^= 1;
	equal(f.backend.receive(stale), false);
	equal(f.calls.length, 1);
	truth(f.backend.receive(reply(f.calls[0].snapshot)));
	truth((await result).succeeded);
	await tick(f, 20);
	equal(f.calls.length, 1);
	equal(f.clock.timers.size, 0);
	f.client.close();
}
{
	const f = fixture(undefined, {replyTimeoutMs : 10});
	const result = f.client.read(ops[4]);
	await flush();
	const malformed = reply(f.calls[0].snapshot);
	malformed[31] = 1;
	equal(f.backend.receive(malformed), false);
	equal((await result).status, Status.MalformedReply);
	truth(f.client.recoveryRequired);
	await tick(f, 20);
	equal(f.calls.length, 1);
	equal(f.clock.timers.size, 0);
	f.client.close();
}
// Successful refusal statuses are not timeout retries.
for (const [operation, endpointStatus] of [[ 1, 2 ], [ 2, 1 ], [ 3, 3 ]]) {
	const f =
	    fixture((call, attempt, backend) => backend.receive(reply(call.snapshot, endpointStatus)),
	            {replyTimeoutMs : 10});
	const result = await invoke(f.client, ops[operation], 5);
	equal(result.status, Status.Ok);
	equal(result.endpointStatus, endpointStatus);
	await tick(f, 20);
	equal(f.calls.length, 1);
	equal(f.clock.timers.size, 0);
	f.client.close();
}
// Exceptions/rejections clean timers; fromExchange waits for its actual exchange release.
for (const reject of [false, true]) {
	const f = fixture(() => {
		if (reject)
			return Promise.reject(Error('send'));
		throw Error('send');
	}, {replyTimeoutMs : 10});
	equal((await f.client.read(ops[4])).status, Status.TransportError);
	await tick(f, 20);
	equal(f.calls.length, 1);
	equal(f.clock.timers.size, 0);
	f.client.close();
}
{
	const clock = new FakeClock(), delayed = deferred();
	let calls = 0, packet;
	const backend = RetryingEncodedBackend.fromExchange(bytes => {
		++calls;
		packet = bytes;
		return delayed.promise;
	}, {replyTimeoutMs : 10});
	const client = new DeviceClient({backend, clock, matchesHeader : () => true});
	truth(client.openSession(1, header));
	const result = client.read(ops[4]);
	await flush();
	clock.advance(20);
	await flush();
	equal(calls, 1);
	delayed.resolve(reply(packet));
	truth((await result).succeeded);
	await flush();
	equal(calls, 1);
	equal(clock.timers.size, 0);
	client.close();
	truth(client.drained);
}
// Abort-aware exchange releases a missing reply on each attempt timeout.
{
	const clock = new FakeClock(), packets = [], signals = [];
	const backend = RetryingEncodedBackend.fromExchange((packet, context) => {
		packets.push(packet.slice());
		signals.push(context.signal);
		if (packets.length === 1)
			return new Promise((resolve, reject) => {
				context.signal.addEventListener('abort', () => reject(Error('omitted reply')),
				                                {once : true});
			});
		return reply(packet);
	}, {replyTimeoutMs : 10, maxRetries : 1});
	const client = new DeviceClient({backend, clock, matchesHeader : () => true});
	truth(client.openSession(1, header));
	const result = client.command(ops[2], 42);
	await flush();
	clock.advance(10);
	await flush();
	truth((await result).executed);
	equal(packets.length, 2);
	equal(packets[0], packets[1]);
	truth(signals[0].aborted);
	truth(signals[0] !== signals[1]);
	await flush();
	equal(clock.timers.size, 0);
	client.close();
	truth(client.drained);
}
// Every exhausted exchange abort/rejection is a release, not an unrelated transport error.
{
	const clock = new FakeClock();
	let calls = 0;
	const backend = RetryingEncodedBackend.fromExchange((packet, context) => {
		++calls;
		return new Promise((resolve, reject) => {
			context.signal.addEventListener('abort', () => reject(Error('reply timeout')),
			                                {once : true});
		});
	}, {replyTimeoutMs : 10, maxRetries : 1});
	const client = new DeviceClient({backend, clock, matchesHeader : () => true});
	truth(client.openSession(1, header));
	const result = client.read(ops[4]);
	await flush();
	clock.advance(10);
	await flush();
	equal(calls, 2);
	clock.advance(10);
	await flush();
	equal((await result).status, Status.Timeout);
	truth(client.recoveryRequired);
	equal(clock.timers.size, 0);
	client.close();
	truth(client.drained);
}
// Zero retries means exactly one send, independently of the default budget.
{
	const f = fixture(undefined, {replyTimeoutMs : 10, maxRetries : 0});
	const result = f.client.read(ops[4]);
	await flush();
	await tick(f, 10);
	equal((await result).status, Status.Timeout);
	equal(f.calls.length, 1);
	equal(f.handoffs(), 1);
	equal(f.clock.timers.size, 0);
	f.client.close();
	truth(f.client.drained);
}
// Cancelling or closing a released-but-unanswered send removes its retry timer.
for (const close of [false, true]) {
	const f = fixture(undefined, {replyTimeoutMs : 10});
	const result = f.client.command(ops[2], 4);
	await flush();
	if (close)
		f.client.close();
	else
		truth(result.cancel());
	equal((await result).status, Status.OutcomeUnknown);
	await tick(f, 1000);
	equal(f.calls.length, 1);
	equal(f.clock.timers.size, 0);
	f.client.close();
	truth(f.client.drained);
}
// The options are captured once; the overall deadline also bounds the largest retry count.
{
	const options = {replyTimeoutMs : 10, maxRetries : 0};
	const f = fixture(undefined, options);
	options.replyTimeoutMs = 1000;
	options.maxRetries = 1000;
	const result = f.client.read(ops[4]);
	await flush();
	await tick(f, 10);
	equal((await result).status, Status.Timeout);
	equal(f.calls.length, 1);
	f.client.close();
}
{
	const f = fixture(undefined, {replyTimeoutMs : 10, maxRetries : 0xffffffff}, 15);
	const result = f.client.read(ops[4]);
	await flush();
	await tick(f, 10);
	await tick(f, 5);
	equal((await result).status, Status.Timeout);
	equal(f.calls.length, 2);
	await tick(f, 100);
	equal(f.calls.length, 2);
	equal(f.clock.timers.size, 0);
	f.client.close();
}
// Logical cancellation before the backend-start microtask never permits a handoff.
for (const action of ['cancel', 'reset', 'close']) {
	const f = fixture(undefined, {replyTimeoutMs : 10});
	const result = f.client.command(ops[2], 1);
	if (action === 'cancel')
		truth(result.cancel());
	else if (action === 'reset')
		f.client.resetSession();
	else
		f.client.close();
	equal((await result).status, Status.Cancelled);
	await tick(f, 100);
	equal(f.calls.length, 0);
	equal(f.handoffs(), 0);
	equal(f.clock.timers.size, 0);
	f.client.close();
	truth(f.client.drained);
}
// A physically closed old exchange cannot parse a malformed/detached reply into a newer invocation.
for (const variant of ['detached', 'malformed', 'matching-new']) {
	const clock = new FakeClock(), old = deferred(), current = deferred(), calls = [];
	const backend = RetryingEncodedBackend.fromExchange((packet, context) => {
		calls.push({packet : packet.slice(), context});
		return calls.length === 1 ? old.promise : current.promise;
	}, {replyTimeoutMs : 10});
	const client = new DeviceClient({backend, clock, matchesHeader : () => true});
	truth(client.openSession(1, header));
	const first = client.command(ops[2], 1);
	await flush();
	truth(first.cancel());
	equal((await first).status, Status.OutcomeUnknown);
	client.resetSession();
	truth(client.confirmSessionClosed(1));
	truth(client.openSession(2, header));
	const second = client.read(ops[4]);
	await flush();
	equal(calls.length, 2);
	let stale = reply(calls[1].packet);
	if (variant === 'detached') {
		const buffer = new ArrayBuffer(32);
		structuredClone(buffer, {transfer : [ buffer ]});
		stale = buffer;
	} else if (variant === 'malformed') {
		stale[31] = 1;
	}
	old.resolve(stale);
	await flush();
	const valid = reply(calls[1].packet);
	data(valid).setUint32(32, 77, true);
	current.resolve(valid);
	const outcome = await second;
	equal(outcome.status, Status.Ok);
	equal(outcome.value, 77);
	equal(client.recoveryRequired, false);
	await flush();
	equal(clock.timers.size, 0);
	client.close();
	truth(client.drained);
}
// Existing ordinary backend has no retry timer or automatic retransmission.
{
	const clock = new FakeClock();
	let calls = 0;
	const backend = new EncodedBackend(() => {
		++calls;
	});
	const client = new DeviceClient({backend, clock, matchesHeader : () => true});
	truth(client.openSession(1, header));
	const result = client.read(ops[4], 500);
	await flush();
	clock.advance(400);
	await flush();
	equal(calls, 1);
	truth(result.cancel());
	equal((await result).status, Status.Cancelled);
	await flush();
	equal(clock.timers.size, 0);
	client.close();
	truth(client.drained);
}
console.log(`retrying encoded checks=${checks} failures=0`);
