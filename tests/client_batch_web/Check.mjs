// Deterministic Mixed Batch ownership, validation and FIFO lifecycle. MIT.
import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';
import {FakeClock, flush, u32Bytes, u32Value, bindings} from '../client_web/Trace.mjs';

const R = await import(pathToFileURL(process.argv[2]).href);
const {
	DeviceClient,
	EncodedBackend,
	RetryingEncodedBackend,
	RpcBackend,
	Status,
	Batch,
	BatchValueStatus : V
} = R;
let checks = 0;
const equal = (a, b) => {
	++checks;
	assert.deepEqual(a, b);
};
const truth = a => {
	++checks;
	assert(a);
};
const empty = new Uint8Array(0), header = new Uint8Array(64);
const cap = (session = 1, fingerprint = 0n) => {
	const output = new Uint8Array(24), dv = new DataView(output.buffer);
	output.set([ 84, 66, 77, 49, 1, 32, 1, 1 ]);
	dv.setUint16(8, 4096, true);
	dv.setUint16(10, 4096, true);
	dv.setUint32(12, session, true);
	dv.setBigUint64(16, fingerprint, true);
	return output;
};
const field = (id = 7, decode = u32Value) => ({...bindings(id)[4], typeId : 6, decode});
const write = (id = 7, encode = u32Bytes) => ({...bindings(id)[1], typeId : 6, encode});
const command = (id = 7) => ({...bindings(id)[2], requestTypeId : 6});
const service = (id = 7, decodeResponse = u32Value) =>
    ({...bindings(id)[3], requestTypeId : 6, responseTypeId : 6, decodeResponse});
const reply = items => {
	const bytes = new Uint8Array(2 + items.reduce((n, i) => n + 12 + (i[4]?.length ?? 0), 0));
	const dv = new DataView(bytes.buffer);
	dv.setUint16(0, items.length, true);
	let cursor = 2;
	for (const [id, op, dispatch = 0, endpoint = 0, payload = empty] of items) {
		dv.setUint32(cursor, id, true);
		bytes.set([ op, dispatch, endpoint, 0 ], cursor + 4);
		dv.setUint16(cursor + 8, payload.length, true);
		bytes.set(payload, cursor + 12);
		cursor += 12 + payload.length;
	}
	return bytes;
};
const frame = (sent, body) => {
	const bytes = new Uint8Array(32 + body.length);
	bytes.set(sent.subarray(0, 32));
	bytes[5] = 1;
	const dv = new DataView(bytes.buffer);
	dv.setUint32(20, body.length, true);
	bytes.set(body, 32);
	return bytes;
};

function fixture(options = {})
{
	const clock = new FakeClock(), started = [];
	let supported = true;
	const backend = {
		supports : () => supported,
		execute : (request, context) =>
		    new Promise(resolve => started.push({request, context, resolve}))
	};
	const client = new DeviceClient(
	    {backend, clock, matchesHeader : () => true, timeoutMs : 100, maxPending : 4, ...options});
	truth(client.openSession(1, header));
	return {client, clock, backend, started, disable : () => supported = false};
}

async function complete(f, body, prepare = true)
{
	await flush();
	const record = f.started.at(-1);
	truth(record);
	if (prepare)
		truth(record.context.handoff(true));
	record.resolve({dispatch : 0, endpointStatus : 0, payload : body});
	await flush();
}

const outcome = (result, status = Status.Ok, valueStatus = V.Ok) => {
	equal(result.status, status);
	equal(result.valueStatus, valueStatus);
	equal(result.value, undefined);
};
const read = () => Batch.items(Batch.read(field()));
const mixed = () => Batch.items(Batch.write(write(), 42), Batch.read(field()));

equal(R.Operations.Batch, 6);
for (const timeout of [0, -1, NaN, Infinity, 0x80000000]) {
	const f = fixture();
	let encodes = 0;
	const p = f.client.batch(cap(), Batch.items(Batch.write(write(7, x => {
		                                                        ++encodes;
		                                                        return u32Bytes(x);
	                                                        }), 9)), timeout);
	equal((await p).status, Status.InvalidArgument);
	equal(encodes, 0);
	equal(f.started.length, 0);
	equal(f.client.pendingCount, 0);
}
for (const count of [0, 33]) {
	const f = fixture();
	const p = f.client.batch(
	    cap(), Batch.items(...Array.from({length : count}, () => Batch.read(field()))));
	equal((await p).status, Status.InvalidArgument);
	equal(f.started.length, 0);
}
{
	const f = fixture(), items = mixed(), p = f.client.batch(cap(), items);
	await flush();
	equal(f.started.length, 1);
	const request = f.started[0].request;
	equal(request.operation, 'Batch');
	equal(request.endpointId, 0);
	equal([...request.payload ],
	      [ 2, 0, 7, 0, 0, 0, 1, 0, 4, 0, 42, 0, 0, 0, 7, 0, 0, 0, 4, 0, 0, 0 ]);
	await complete(f, reply([ [ 7, 1 ], [ 7, 4, 0, 0, u32Bytes(42) ] ]));
	const result = await p;
	outcome(result);
	equal(result.items.length, 2);
	truth(result.items[0].succeeded);
	equal(result.items[0].valueStatus, V.NoValue);
	equal(result.items[1].value, 42);
	equal(f.client.pendingCount, 0);
}
{
	const f = fixture(), storage = new Uint8Array(40);
	storage.set(cap(), 5);
	const p = f.client.batch(new DataView(storage.buffer, 5, 24), read());
	await complete(f, reply([ [ 7, 4, 0, 0, u32Bytes(18) ] ]));
	equal((await p).items[0].value, 18);
}
// One captured call owns canonical argument bytes and copied endpoint/decode metadata.
{
	const f = fixture();
	const target = field(), source = {n : 41};
	const setter = write(7, value => u32Bytes(value.n));
	const recipe = Batch.items(Batch.write(setter, source), Batch.read(target));
	const capability = cap(), p = f.client.batch(capability, recipe);
	source.n = 99;
	setter.id = 100;
	target.id = 100;
	setter.encode = () => {
		throw Error('mutated encoder');
	};
	target.decode = () => {
		throw Error('mutated decoder');
	};
	capability.fill(255);
	await flush();
	equal(u32Value(f.started[0].request.payload.subarray(10, 14)), 41);
	const body = reply([ [ 7, 1 ], [ 7, 4, 0, 0, u32Bytes(41) ] ]), original = body.slice();
	await complete(f, body);
	const result = await p;
	outcome(result);
	equal(result.items[1].value, 41);
	body.fill(255);
	equal(result.payload, original);
	equal(u32Value(result.items[1].payload), 41);
	result.items[1].payload.fill(0);
	equal(result.payload, original);
}
// Encoders may return shared reusable storage; a later encoder must not change an earlier item.
{
	const f = fixture(), scratch = u32Bytes(41);
	const p = f.client.batch(cap(), Batch.items(Batch.write(write(7, () => scratch), 41),
	                                            Batch.write(write(8, () => {
		                                                        scratch.set(u32Bytes(99));
		                                                        return scratch;
	                                                        }), 99)));
	await flush();
	equal(u32Value(f.started[0].request.payload.subarray(10, 14)), 41);
	equal(u32Value(f.started[0].request.payload.subarray(22, 26)), 99);
	await complete(f, reply([ [ 7, 1 ], [ 8, 1 ] ]));
	truth((await p).succeeded);
}

// Public encode():Uint8Array includes Node Buffer and typed-array subclasses.
class HostileBytes extends Uint8Array {
	get length()
	{
		throw Error('overridden length');
	}

	slice()
	{
		throw Error('overridden slice');
	}

	subarray()
	{
		throw Error('overridden subarray');
	}

	[Symbol.iterator]()
	{
		throw Error('overridden iterator');
	}
}

for (const batch of [false, true])
	for (const subclass of [false, true]) {
		const f = fixture(),
		      source = subclass ? new HostileBytes([ 41, 0, 0, 0 ]) : Buffer.from([ 41, 0, 0, 0 ]);
		const encoded = write(7, () => source);
		const p = batch ? f.client.batch(cap(), Batch.items(Batch.write(encoded, 41)))
		                : f.client.write(encoded, 41);
		source.fill(99);
		await flush();
		const owned = f.started[0].request.payload;
		equal(owned.constructor, Uint8Array);
		equal(u32Value(batch ? owned.subarray(10, 14) : owned), 41);
		await complete(f, batch ? reply([ [ 7, 1 ] ]) : empty);
		truth((await p).succeeded);
	}
{
	const f = fixture(), p = f.client.read(field());
	const original = Buffer.from(u32Bytes(41));
	await complete(f, original);
	const result = await p;
	original.fill(0);
	equal(result.value, 41);
	equal(u32Value(result.payload), 41);
	equal(result.payload.constructor, Uint8Array);
}
// Existing FIFO count/payload refusal happens before application encoders run.
for (const reason of ['count', 'payload']) {
	const f = fixture(reason === 'count' ? {maxPending : 1} : {maxPayloadBytes : 20});
	let encodes = 0;
	const first = f.client.read(field());
	const p = f.client.batch(cap(), Batch.items(Batch.write(write(7, v => {
		                                                        ++encodes;
		                                                        return u32Bytes(v);
	                                                        }), 9), Batch.read(field())));
	equal((await p).status, reason === 'count' ? Status.QueueFull : Status.PayloadFull);
	equal(encodes, 0);
	equal(f.client.pendingCount, 1);
	truth(first.cancel());
	await first;
}
// A user support hook can fill the FIFO only after encoding: late refusal keeps Batch shape.
{
	const f = fixture({maxPending : 1});
	let armed = false, ordinary;
	f.backend.supports = () => {
		if (armed && !ordinary)
			ordinary = f.client.read(field(9));
		return true;
	};
	const p = f.client.batch(cap(), Batch.items(Batch.write(write(7, value => {
		                                                        armed = true;
		                                                        return u32Bytes(value);
	                                                        }), 41)));
	const result = await p;
	outcome(result, Status.QueueFull, V.NoValue);
	equal(result.items, undefined);
	truth(ordinary);
	truth(f.started.every(item => item.request.operation === 'FieldRead'));
	truth(ordinary.cancel());
	await ordinary;
	await flush();
	for (const record of f.started)
		record.resolve({dispatch : 0, endpointStatus : 0, payload : u32Bytes(9)});
	await flush();
}
// Each item's endpoint status is independent, and errors do not stop later items.
for (const endpoint of [1, 2, 3, 4]) {
	const f = fixture();
	const p = f.client.batch(cap(), Batch.items(Batch.service(service(), 3), Batch.read(field(8))));
	await complete(f, reply([ [ 7, 3, 0, endpoint ], [ 8, 4, 0, 0, u32Bytes(18) ] ]));
	const result = await p;
	outcome(result);
	equal(result.items[0].endpointStatus, endpoint);
	equal(result.items[0].accepted, false);
	equal(result.items[0].succeeded, false);
	equal(result.items[0].value, undefined);
	equal(result.items[1].value, 18);
}
for (const endpoint of [0, 1, 2, 3, 4, 5, 6, 7]) {
	const f = fixture(), p = f.client.batch(cap(), Batch.items(Batch.command(command(), 3)));
	await complete(f, reply([ [ 7, 2, 0, endpoint ] ]));
	const result = await p;
	outcome(result);
	equal(result.items[0].endpointStatus, endpoint);
	equal(result.items[0].accepted, endpoint === 1);
	equal(result.items[0].executed, endpoint === 0);
	equal(result.items[0].succeeded, endpoint <= 1);
}
// Structural errors preserve a checked outer outcome, without inventing item successes.
for (const mutate of [b => b.subarray(0, 1),
                     b => {
	                     b[0] = 1;
	                     return b;
                     },
                     b => {
	                     b[2] = 8;
	                     return b;
                     },
                     b => {
	                     b[6] = 2;
	                     return b;
                     },
                     b => {
	                     b[9] = 1;
	                     return b;
                     },
                     b => {
	                     b[12] = 1;
	                     return b;
                     },
                     b => {
	                     b[10] = 3;
	                     return b;
                     },
                     b => b.subarray(0, b.length - 1),
                     b => {
	                     const x = new Uint8Array(b.length + 1);
	                     x.set(b);
	                     return x;
                     }]) {
	const f = fixture();
	let decodes = 0;
	const p = f.client.batch(cap(), Batch.items(Batch.write(write(), 9), Batch.read(field(8, x => {
		++decodes;
		return u32Value(x);
	}))));
	const body = mutate(reply([ [ 7, 1 ], [ 8, 4, 0, 0, u32Bytes(9) ] ]));
	await complete(f, body);
	const result = await p;
	// Below the 14-byte Encoded Batch minimum, this is an envelope failure.
	if (body.length < 14 || body.length > 30) {
		equal(result.status, Status.OutcomeUnknown);
	} else {
		outcome(result, Status.Ok, V.InvalidPayload);
		truth(result.succeeded);
		truth(!f.client.recoveryRequired);
	}
	equal(result.items, undefined);
	equal(decodes, 0);
}
// A later throwing DTO decoder cannot erase a preceding confirmed mutation.
{
	const f = fixture();
	let earlier = 0;
	const p = f.client.batch(cap(), Batch.items(Batch.write(write(), 42), Batch.read(field(8, x => {
		++earlier;
		return u32Value(x);
	})), Batch.service(service(9, () => {
		                   throw Error('invalid bool');
	                   }), 3)));
	await complete(f,
	               reply([ [ 7, 1 ], [ 8, 4, 0, 0, u32Bytes(42) ], [ 9, 3, 0, 0, u32Bytes(2) ] ]));
	const result = await p;
	outcome(result, Status.Ok, V.InvalidPayload);
	equal(earlier, 1);
	truth(result.items[0].succeeded);
	equal(result.items.map(i => i.value), [ undefined, undefined, undefined ]);
	equal(result.items[1].valueStatus, V.InvalidPayload);
	truth(!f.client.recoveryRequired);
}
// Closing, resetting or readmitting during a codec preserves checked statuses, not values.
for (const action of ['close', 'reset', 'readmit']) {
	const f = fixture();
	let later = 0, body;
	const decoder = () => {
		body.fill(255);
		if (action === 'close')
			f.client.close();
		else {
			f.client.resetSession();
			if (action === 'readmit') {
				truth(f.client.confirmSessionClosed(1));
				truth(f.client.openSession(2, header));
			}
		}
		return 42;
	};
	const p =
	    f.client.batch(cap(), Batch.items(Batch.write(write(), 42), Batch.read(field(8, decoder)),
	                                      Batch.service(service(9, () => ++later), 3)));
	let completions = 0;
	p.then(() => ++completions);
	body = reply([ [ 7, 1 ], [ 8, 4, 0, 0, u32Bytes(42) ], [ 9, 3, 0, 0, u32Bytes(9) ] ]);
	const original = body.slice();
	await complete(f, body);
	const result = await p;
	outcome(result, Status.Ok, V.Stale);
	truth(result.items[0].succeeded);
	equal(result.items[0].endpointStatus, 0);
	equal(result.items.map(i => i.value), [ undefined, undefined, undefined ]);
	equal(result.payload, original);
	equal(later, 0);
	equal(completions, 1);
	equal(result.session, 1);
	truth(!f.client.recoveryRequired);
	equal(f.client.pendingCount, 0);
	equal(f.client.ready, action === 'readmit');
}
// Canonical request failures and synchronous encoder reentry never publish a request.
for (const action of ['throw', 'wrong-length', 'close', 'reset']) {
	const f = fixture();
	let encodes = 0;
	const encoder = () => {
		++encodes;
		if (action === 'throw')
			throw Error('argument rejected');
		if (action === 'close')
			f.client.close();
		if (action === 'reset')
			f.client.resetSession();
		return action === 'wrong-length' ? new Uint8Array(3) : u32Bytes(7);
	};
	const p = f.client.batch(cap(), Batch.items(Batch.write(write(7, encoder), 9)));
	const result = await p;
	await flush();
	truth([ Status.CodecError, Status.Closed, Status.NotAdmitted, Status.SessionLost ].includes(
	    result.status));
	equal(result.items, undefined);
	equal(encodes, 1);
	equal(f.started.length, 0);
	equal(f.client.pendingCount, 0);
}
// Deterministic boundary states; no event-loop speed or sleep assumptions.
for (const cancel of [false, true])
	for (const phase of ['queued', 'offered', 'prepared']) {
		const f = fixture(), p = f.client.batch(cap(), mixed());
		let completed = 0;
		p.then(() => ++completed);
		if (phase !== 'queued')
			await flush();
		if (phase === 'prepared')
			truth(f.started[0].context.handoff(true));
		if (cancel)
			truth(p.cancel());
		else
			f.clock.advance(100);
		await flush();
		const result = await p;
		equal(result.status, phase === 'prepared' ? Status.OutcomeUnknown
		                     : cancel             ? Status.Cancelled
		                                          : Status.Timeout);
		equal(result.items, undefined);
		equal(f.client.recoveryRequired, phase === 'prepared');
		equal(completed, 1);
		equal(p.cancel(), false);
		if (f.started.length) {
			equal(f.started[0].context.handoff(true), false);
			f.started[0].resolve({
				dispatch : 0,
				endpointStatus : 0,
				payload : reply([ [ 7, 1 ], [ 7, 4, 0, 0, u32Bytes(42) ] ])
			});
			await flush();
			equal(completed, 1);
		}
		if (phase === 'prepared') {
			equal((await f.client.batch(cap(), mixed())).status, Status.RecoveryRequired);
			f.client.close();
			equal((await f.client.batch(cap(), mixed())).status, Status.Closed);
		}
	}
// Outer deadline remains absolute; a successful response at 99ms works, at100ms is late.
for (const elapsed of [99, 100, 101]) {
	const f = fixture(), p = f.client.batch(cap(), mixed());
	await flush();
	truth(f.started[0].context.handoff(true));
	f.clock.advance(elapsed);
	f.started[0].resolve({
		dispatch : 0,
		endpointStatus : 0,
		payload : reply([ [ 7, 1 ], [ 7, 4, 0, 0, u32Bytes(42) ] ])
	});
	await flush();
	const result = await p;
	equal(result.status, elapsed === 99 ? Status.Ok : Status.OutcomeUnknown);
	equal(result.items?.[1].value, elapsed === 99 ? 42 : undefined);
}
// Opcode6 remains conservatively a mutation even if this particular recipe contains only reads.
{
	const f = fixture(), p = f.client.batch(cap(), read());
	await flush();
	truth(f.started[0].context.handoff(true));
	f.clock.advance(100);
	await flush();
	equal((await p).status, Status.OutcomeUnknown);
	truth(f.client.recoveryRequired);
	f.started[0].resolve(
	    {dispatch : 0, endpointStatus : 0, payload : reply([ [ 7, 4, 0, 0, u32Bytes(42) ] ])});
	await flush();
}
// Revocation inside the activation support hook must not resurrect an already cancelled request.
{
	const f = fixture();
	let armed = false, supports = 0;
	f.backend.supports = () => {
		++supports;
		if (armed)
			f.client.close();
		return true;
	};
	const p = f.client.batch(cap(), mixed());
	armed = true;
	await flush();
	equal((await p).status, Status.Cancelled);
	truth(supports >= 2);
	equal(f.started.length, 0);
	equal(f.client.pendingCount, 0);
}
// Capability/backend opt-in and full request/reply limits precede any encoder.
for (const which of ['unsupported', 'rpc', 'count', 'request-limit', 'reply-limit', 'session',
                     'fingerprint', 'magic']) {
	const f = fixture();
	let encodes = 0;
	const capability = cap(), dv = new DataView(capability.buffer);
	if (which === 'unsupported')
		f.disable();
	if (which === 'rpc')
		f.backend.supports = op => op !== 'Batch';
	if (which === 'count')
		capability[5] = 1;
	if (which === 'request-limit')
		dv.setUint16(8, 10, true);
	if (which === 'reply-limit')
		dv.setUint16(10, 14, true);
	if (which === 'session')
		dv.setUint32(12, 2, true);
	if (which === 'fingerprint')
		dv.setBigUint64(16, 1n, true);
	if (which === 'magic')
		capability[0] = 0;
	const result =
	    await f.client.batch(capability, Batch.items(Batch.write(write(7, x => {
		                                                             ++encodes;
		                                                             return u32Bytes(x);
	                                                             }), 3), Batch.read(field())));
	truth(result.status !== Status.Ok);
	equal(encodes, 0);
	equal(f.started.length, 0);
}
// Capturing an indexed recipe never trusts a user iterator or an unstable unbounded length.
{
	const f = fixture(), recipe = [...read() ];
	let iterators = 0;
	recipe[Symbol.iterator] = function*() {
		++iterators;
	};
	const p = f.client.batch(cap(), recipe);
	await complete(f, reply([ [ 7, 4, 0, 0, u32Bytes(42) ] ]));
	equal((await p).items[0].value, 42);
	equal(iterators, 0);
}
{
	const f = fixture(), recipe = [...read() ];
	const original = recipe[0];
	Object.defineProperty(recipe, '0', {
		get() {
		    recipe.length = 1000000;
		    return original;
		}
	});
	const p = f.client.batch(cap(), recipe);
	await complete(f, reply([ [ 7, 4, 0, 0, u32Bytes(42) ] ]));
	equal((await p).items[0].value, 42);
	equal(f.started.length, 1);
}
// A checked result does not let the next FIFO request reuse still-owned physical I/O.
{
	const clock = new FakeClock(), sent = [];
	let release;
	const backend = new EncodedBackend(packet => {
		sent.push(packet.slice());
		if (sent.length === 1)
			return new Promise(resolve => release = resolve);
	}, {batch : true});
	const client = new DeviceClient({backend, clock, matchesHeader : () => true, timeoutMs : 100});
	truth(client.openSession(1, header));
	const first = client.batch(cap(), mixed()), second = client.read(field(), 200);
	await flush();
	truth(backend.receive(frame(sent[0], reply([ [ 7, 1 ], [ 7, 4, 0, 0, u32Bytes(42) ] ]))));
	await flush();
	equal((await first).items[1].value, 42);
	equal(sent.length, 1);
	clock.advance(110);
	await flush();
	equal(sent.length, 1);
	release();
	await flush();
	equal(sent.length, 2);
	truth(backend.receive(frame(sent[1], u32Bytes(99))));
	await flush();
	equal((await second).value, 99);
	client.close();
}
// A normal EncodedBackend hands off once. Timeout never causes a hidden batch replay.
{
	const clock = new FakeClock(), sent = [];
	const backend = new EncodedBackend(packet => {
		sent.push(packet.slice());
	}, {batch : true});
	const client = new DeviceClient({backend, clock, matchesHeader : () => true, timeoutMs : 100});
	truth(client.openSession(1, header));
	const p = client.batch(cap(), mixed());
	await flush();
	equal(sent.length, 1);
	equal(sent[0][6], 6);
	clock.advance(100);
	await flush();
	equal((await p).status, Status.OutcomeUnknown);
	clock.advance(10000);
	await flush();
	equal(sent.length, 1);
	truth(client.recoveryRequired);
	truth(backend.receive(frame(sent[0], reply([ [ 7, 1 ], [ 7, 4, 0, 0, u32Bytes(42) ] ]))) ===
	      false);
	truth(client.confirmSessionClosed(1));
	truth(client.openSession(2, header));
	equal(sent.length, 1);
	client.close();
}
// Explicit retrying policy may repeat a mutation; plain backend never acquires this policy.
{
	const clock = new FakeClock(), sent = [];
	const backend = new RetryingEncodedBackend(packet => {
		sent.push(packet.slice());
	}, {batch : true, replyTimeoutMs : 10, maxRetries : 1});
	const client = new DeviceClient({backend, clock, matchesHeader : () => true, timeoutMs : 100});
	truth(client.openSession(1, header));
	const p = client.batch(cap(), mixed());
	await flush();
	equal(sent.length, 1);
	clock.advance(10);
	await flush();
	equal(sent.length, 2);
	equal(sent[1], sent[0]);
	clock.advance(10);
	await flush();
	equal((await p).status, Status.OutcomeUnknown);
	equal(sent.length, 2);
}
// The legacy RPC backend must explicitly refuse operation6 rather than misserialize it.
{
	const backend = new RpcBackend(() => {
	                               }),
	      client = new DeviceClient({backend, matchesHeader : () => true});
	truth(client.openSession(1, header));
	equal((await client.batch(cap(), read())).status, Status.Unsupported);
	client.close();
}
console.log(`Batch deterministic checks=${checks} failures=0`);
