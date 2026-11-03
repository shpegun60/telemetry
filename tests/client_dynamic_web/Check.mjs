// Runtime admission, opaque handles and all transfer-boundary invalidation checks. MIT.
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {pathToFileURL} from 'node:url';
import {FakeClock, flush, u32Bytes} from '../client_web/Trace.mjs';

const [dynamicPath, runtimePath, descriptorPath, trmPath, tbmPath] = process.argv.slice(2);
const {DynamicClient, ValueStatus : V} = await import(pathToFileURL(dynamicPath).href);
const {parseDescriptor, encodeValue, matchesDescriptorHeader} =
    await import(new URL('./telemetry.js', pathToFileURL(dynamicPath)));
const {EncodedBackend, RetryingEncodedBackend, RpcBackend, Status} =
    await import(pathToFileURL(runtimePath).href);
const descriptor = await readFile(descriptorPath), header = descriptor.subarray(0, 64);
const model = parseDescriptor(descriptor), trm = await readFile(trmPath),
      tbm = await readFile(tbmPath);
let checks = 0;
const equal = (a, b) => {
	++checks;
	assert.deepEqual(a, b);
};
const truth = a => {
	++checks;
	assert(a);
};
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
const encoded = number => encodeValue(model, 14, dto(number));
const proof = (session = 17) =>
    ({session, txReleased : true, oldFramesExcluded : true, remoteSessionClosed : true});
const bind = (client, operation = 'FieldRead', id = 65536) => {
	const b = client.bind(operation, id);
	equal(b.status, Status.Ok);
	return b.endpoint;
};

function fixture(options = {})
{
	const clock = new FakeClock(), started = [];
	const backend = {
		supports : () => true,
		execute : (request, context) =>
		    new Promise(resolve => started.push({request, context, resolve}))
	};
	const client = new DynamicClient({backend, clock, timeoutMs : 100, maxPending : 4, ...options});
	equal(client.admit(model, header, 17), Status.Ok);
	return {client, clock, backend, started};
}

async function complete(f, payload, endpointStatus = 0, prepare = true)
{
	await flush();
	const r = f.started.at(-1);
	truth(r);
	if (prepare)
		truth(r.context.handoff(true));
	r.resolve({dispatch : 0, endpointStatus, payload});
	await flush();
}

const reply = (sent, body) => {
	const bytes = new Uint8Array(32 + body.length);
	bytes.set(sent.subarray(0, 32));
	bytes[5] = 1;
	new DataView(bytes.buffer).setUint32(20, body.length, true);
	bytes.set(body, 32);
	return bytes;
};
const batchReply = items => {
	const bytes = new Uint8Array(2 + items.reduce((n, i) => n + 12 + (i[4]?.length ?? 0), 0));
	const dv = new DataView(bytes.buffer);
	dv.setUint16(0, items.length, true);
	let cursor = 2;
	for (const [id, operation, dispatch = 0, endpoint = 0, payload = new Uint8Array(0)] of items) {
		dv.setUint32(cursor, id, true);
		bytes.set([ operation, dispatch, endpoint, 0 ], cursor + 4);
		dv.setUint16(cursor + 8, payload.length, true);
		bytes.set(payload, cursor + 12);
		cursor += 12 + payload.length;
	}
	return bytes;
};
const readManyReply = (id, payload) => {
	const bytes = new Uint8Array(10 + payload.length), dv = new DataView(bytes.buffer);
	dv.setUint16(0, 1, true);
	dv.setUint32(2, id, true);
	dv.setUint16(8, payload.length, true);
	bytes.set(payload, 10);
	return bytes;
};

// Exact parser brand/header, not only the fingerprint, defines a runtime admission.
{
	const f = fixture();
	f.client.invalidate();
	truth(f.client.confirmQuiescence(proof()));
	const changed = Uint8Array.from(header);
	changed[63] ^= 1;
	equal(new DataView(changed.buffer, changed.byteOffset).getBigUint64(16, true),
	      model.fingerprint);
	truth(!matchesDescriptorHeader(model, changed));
	equal(f.client.admit(model, changed, 18), Status.InvalidArgument);
	equal(f.client.admit({...model}, header, 18), Status.InvalidArgument);
	equal(f.started.length, 0);
	equal(f.client.pendingCount, 0);
	equal(f.client.admit(model, header, 18), Status.Ok);
}
{
	const source = Uint8Array.from(descriptor), schema = parseDescriptor(source);
	source.fill(255);
	truth(matchesDescriptorHeader(schema, header));
	const f = fixture();
	equal(f.client.bind('FieldWrite', 65537).status, Status.ReadOnly);
	equal(f.client.bind('FieldRead', 0xffffffff).status, Status.InvalidArgument);
	for (const name
	         of ['core', 'client', 'submitEncoded', 'read', 'write', 'openSession', 'resetSession'])
		equal(f.client[name], undefined);
	equal(Object.keys(f.client), []);
	const read = bind(f.client);
	equal((await f.client.readField({...read})).operation.status, Status.NotAdmitted);
	equal((await f.client.command(read)).operation.status, Status.InvalidArgument);
	const other = fixture();
	equal((await other.client.readField(read)).operation.status, Status.NotAdmitted);
	f.client.invalidate();
	equal(f.client.schema, undefined);
	truth(!f.client.ready);
	equal((await f.client.readField(read)).operation.status, Status.NotAdmitted);
	for (const property of ['txReleased', 'oldFramesExcluded', 'remoteSessionClosed'])
		truth(!f.client.confirmQuiescence({...proof(), [property] : false}));
	truth(!f.client.confirmQuiescence(proof(18)));
	truth(f.client.confirmQuiescence(proof()));
	equal(f.client.admit(parseDescriptor(descriptor), header, 18), Status.Ok);
	equal((await f.client.readField(read)).operation.status, Status.NotAdmitted);
	equal(f.started.length, 0);
}
// Dynamic canonical decode failure cannot change a checked endpoint operation result.
{
	const f = fixture(), read = bind(f.client, 'FieldRead', 65540), p = f.client.readField(read);
	await complete(f, new Uint8Array([ 2 ]));
	const result = await p;
	equal(result.operation.status, Status.Ok);
	truth(result.operation.succeeded);
	equal(result.valueStatus, V.InvalidPayload);
	equal(result.value, undefined);
	truth(!f.client.recoveryRequired);
}
// A DTO getter cannot retire/admit a new owner session inside an unfinished old producer.
{
	const f = fixture(), write = bind(f.client, 'FieldWrite');
	let verified, admitted, getters = 0;
	const value = dto(7);
	Object.defineProperty(value, 'number', {
		enumerable : true,
		get() {
		    ++getters;
		    f.client.invalidate();
		    verified = f.client.confirmQuiescence(proof());
		    admitted = f.client.admit(model, header, 18);
		    return 7;
		}
	});
	const result = await f.client.writeField(write, value);
	await flush();
	equal(verified, false);
	equal(admitted, Status.Busy);
	equal(getters, 1);
	equal(result.operation.status, Status.NotAdmitted);
	equal(f.started.length, 0);
	truth(f.client.confirmQuiescence(proof()));
	equal(f.client.admit(model, header, 18), Status.Ok);
}
// Admission revocation before physical handoff cancels; after handoff it waits passively.
{
	const f = fixture(), write = bind(f.client, 'FieldWrite');
	let drainedInside;
	const value = dto(7);
	Object.defineProperty(value, 'number', {
		enumerable : true,
		get() {
		    f.client.close();
		    drainedInside = f.client.drained;
		    return 7;
		}
	});
	const result = await f.client.writeField(write, value);
	equal(drainedInside, false);
	equal(result.operation.status, Status.Closed);
	truth(f.client.drained);
	equal(f.started.length, 0);
}
{
	const client = new DynamicClient({
		backend : {
			execute : () => {
		        throw Error('Unexpected transport');
			}
		}
	});
	const buffer = Uint8Array.from(header).buffer, observed = new DataView(buffer);
	let drainedInside;
	Object.defineProperty(observed, 'buffer', {
		get() {
		    client.close();
		    drainedInside = client.drained;
		    return buffer;
		}
	});
	equal(client.admit(model, observed, 17), Status.Closed);
	equal(drainedInside, false);
	truth(client.drained);
}
for (const phase of ['queued', 'offered', 'prepared']) {
	const f = fixture(), read = bind(f.client), p = f.client.readField(read);
	let completed = 0;
	p.then(() => ++completed);
	if (phase !== 'queued')
		await flush();
	if (phase === 'prepared')
		truth(f.started[0].context.handoff(true));
	f.client.invalidate();
	await flush();
	if (phase === 'prepared') {
		equal(completed, 0);
		truth(!f.started[0].context.handoff(true));
		f.started[0].resolve({dispatch : 0, endpointStatus : 0, payload : encoded(42)});
		await flush();
		const result = await p;
		equal(result.operation.status, Status.Ok);
		truth(result.operation.succeeded);
		equal(result.valueStatus, V.Stale);
		equal(result.value, undefined);
		truth(!f.client.recoveryRequired);
	} else {
		equal((await p).operation.status, Status.Cancelled);
		if (f.started.length) {
			truth(!f.started[0].context.handoff(true));
			f.started[0].resolve({dispatch : 0, endpointStatus : 0, payload : encoded(42)});
			await flush();
		}
	}
	equal(completed, 1);
	equal(f.client.pendingCount, 0);
	truth(f.client.confirmQuiescence(proof()));
	equal(f.client.admit(model, header, 18), Status.Ok);
	equal((await f.client.readField(read)).operation.status, Status.NotAdmitted);
}
// A checked passive completion preserves operation facts even when values have been retired.
for (const operation of ['FieldWrite', 'Command', 'Service', 'ReadMany', 'Batch']) {
	const f = fixture(), write = bind(f.client, 'FieldWrite'),
	      accepted = bind(f.client, 'Command', 65538);
	const busy = bind(f.client, 'Service', 65539), counter = bind(f.client, 'FieldRead', 65537);
	const watch = f.client.bindReadMany(trm, [ counter ]);
	equal(watch.status, Status.Ok);
	const p = operation === 'FieldWrite' ? f.client.writeField(write, dto(7))
	          : operation === 'Command'  ? f.client.command(accepted, dto(7))
	          : operation === 'Service'  ? f.client.service(busy, dto(7))
	          : operation === 'ReadMany'
	              ? f.client.readMany(watch.binding)
	              : f.client.batch(tbm, [ {endpoint : write, value : dto(7)} ]);
	let completed = 0;
	p.then(() => ++completed);
	await flush();
	truth(f.started[0].context.handoff(true));
	f.client.invalidate();
	await flush();
	equal(completed, 0);
	const payload = operation === 'ReadMany' ? readManyReply(65537, u32Bytes(42))
	                : operation === 'Batch'  ? batchReply([ [ 65536, 1 ] ])
	                                         : new Uint8Array(0);
	const endpointStatus = operation === 'Command' ? 1 : operation === 'Service' ? 3 : 0;
	await complete(f, payload, endpointStatus, false);
	const result = await p;
	equal(result.operation.status, Status.Ok);
	equal(result.operation.endpointStatus, endpointStatus);
	equal(result.valueStatus, V.Stale);
	equal(result.value, undefined);
	equal(completed, 1);
	if (operation === 'Command') {
		truth(result.operation.accepted);
		truth(!result.operation.executed);
	}
	if (operation === 'Service') {
		truth(!result.operation.succeeded);
		truth(!result.operation.accepted);
	}
	if (operation === 'Batch') {
		truth(result.items[0].succeeded);
		equal(result.items[0].value, undefined);
	}
	truth(!f.client.recoveryRequired);
}
// Every opcode shares the same deadline and revocation guards; mutation evidence survives.
for (const operation of ['FieldRead', 'FieldWrite', 'Command', 'Service', 'ReadMany', 'Batch']) {
	const f = fixture(), read = bind(f.client), write = bind(f.client, 'FieldWrite'),
	      command = bind(f.client, 'Command');
	const service = bind(f.client, 'Service'), counter = bind(f.client, 'FieldRead', 65537);
	const watch = f.client.bindReadMany(trm, [ counter ]);
	equal(watch.status, Status.Ok);
	const p = operation === 'FieldRead'    ? f.client.readField(read)
	          : operation === 'FieldWrite' ? f.client.writeField(write, dto(8))
	          : operation === 'Command'    ? f.client.command(command, dto(8))
	          : operation === 'Service'    ? f.client.service(service, dto(8))
	          : operation === 'ReadMany'
	              ? f.client.readMany(watch.binding)
	              : f.client.batch(tbm, [ {endpoint : write, value : dto(8)}, {endpoint : read} ]);
	await flush();
	truth(f.started[0].context.handoff(true));
	f.client.invalidate();
	await flush();
	truth(!f.client.confirmQuiescence(proof()));
	f.clock.advance(100);
	await flush();
	const result = await p;
	equal(result.operation.status,
	      [ 'FieldRead', 'ReadMany' ].includes(operation) ? Status.Timeout : Status.OutcomeUnknown);
	truth(f.client.recoveryRequired);
	equal(result.value, undefined);
	equal(result.items, undefined);
	equal(f.client.admit(model, header, 18), Status.RecoveryRequired);
	f.client.close();
	equal(f.client.admit(model, header, 18), Status.Closed);
	f.started[0].resolve({dispatch : 0, endpointStatus : 0, payload : new Uint8Array(0)});
	await flush();
}
// A malformed later batch value suppresses every value, but keeps the preceding Write outcome.
{
	const f = fixture(), write = bind(f.client, 'FieldWrite'), read = bind(f.client),
	      flag = bind(f.client, 'Service', 65542);
	const p = f.client.batch(
	    tbm, [ {endpoint : write, value : dto(7)}, {endpoint : read}, {endpoint : flag} ]);
	await complete(
	    f, batchReply([
		    [ 65536, 1 ], [ 65536, 4, 0, 0, encoded(7) ], [ 65542, 3, 0, 0, new Uint8Array([ 2 ]) ]
	    ]));
	const result = await p;
	equal(result.operation.status, Status.Ok);
	equal(result.valueStatus, V.InvalidPayload);
	truth(result.items[0].succeeded);
	truth(result.items.every(item => item.value === undefined));
	truth(!f.client.recoveryRequired);
}
// ReadMany rejects aggregates without a hidden fan-out request.
{
	const f = fixture(), aggregate = bind(f.client), counter = bind(f.client, 'FieldRead', 65537);
	equal(f.client.bindReadMany(trm, [ aggregate ]).status, Status.Unsupported);
	const selected = f.client.bindReadMany(trm, [ counter ]);
	equal(selected.status, Status.Ok);
	const p = f.client.readMany(selected.binding);
	await complete(f, readManyReply(65537, u32Bytes(42)));
	const result = await p;
	equal(result.operation.status, Status.Ok);
	equal(result.items[0].value, 42);
	f.client.invalidate();
	equal((await f.client.readMany(selected.binding)).operation.status, Status.NotAdmitted);
}
// No retransmission after explicit invalidation; the already-correlated reply can still settle.
{
	const clock = new FakeClock(), sent = [];
	const backend = new RetryingEncodedBackend(packet => sent.push(packet.slice()),
	                                           {replyTimeoutMs : 10, maxRetries : 3});
	const f = fixture({backend, clock}), read = bind(f.client), p = f.client.readField(read);
	let completed = 0;
	p.then(() => ++completed);
	await flush();
	equal(sent.length, 1);
	f.client.invalidate();
	clock.advance(10);
	await flush();
	equal(sent.length, 1);
	equal(completed, 0);
	clock.advance(10);
	truth(backend.receive(reply(sent[0], encoded(42))));
	await flush();
	equal((await p).operation.status, Status.Ok);
	equal((await p).valueStatus, V.Stale);
	equal(completed, 1);
	clock.advance(200);
	await flush();
	equal(sent.length, 1);
	truth(!f.client.recoveryRequired);
}
// A checked response does not release a send buffer or permit the next FIFO transfer.
{
	const clock = new FakeClock(), sent = [];
	let release;
	const backend = new EncodedBackend(packet => {
		sent.push(packet.slice());
		if (sent.length === 1)
			return new Promise(resolve => release = resolve);
	});
	const f = fixture({backend, clock}), read = bind(f.client),
	      counter = bind(f.client, 'FieldRead', 65537);
	const first = f.client.readField(read), second = f.client.readField(counter, 200);
	await flush();
	truth(backend.receive(reply(sent[0], encoded(7))));
	await flush();
	equal((await first).value.number, 7);
	equal(sent.length, 1);
	equal(f.client.pendingCount, 1);
	clock.advance(110);
	await flush();
	equal(sent.length, 1);
	release();
	await flush();
	equal(sent.length, 2);
	truth(backend.receive(reply(sent[1], u32Bytes(99))));
	await flush();
	equal((await second).value, 99);
	f.client.close();
}
// Invalidation between checked transport response and projection keeps outcome, retires value.
{
	const clock = new FakeClock(), sent = [];
	let release;
	const backend = new EncodedBackend(packet => {
		sent.push(packet.slice());
		return new Promise(resolve => release = resolve);
	});
	const f = fixture({backend, clock}), read = bind(f.client), p = f.client.readField(read);
	await flush();
	truth(backend.receive(reply(sent[0], encoded(7))));
	f.client.invalidate();
	await flush();
	const result = await p;
	equal(result.operation.status, Status.Ok);
	equal(result.valueStatus, V.Stale);
	equal(result.value, undefined);
	truth(!f.client.confirmQuiescence({...proof(), txReleased : false}));
	release();
	await flush();
	truth(f.client.confirmQuiescence(proof()));
	equal(f.client.admit(model, header, 18), Status.Ok);
	equal(sent.length, 1);
	f.client.close();
}
// The normal transport remains one-shot after a timeout: reconnect never replays a Write.
{
	const clock = new FakeClock(), sent = [];
	const backend = new EncodedBackend(packet => sent.push(packet.slice()));
	const f = fixture({backend, clock}), write = bind(f.client, 'FieldWrite'),
	      p = f.client.writeField(write, dto(7));
	await flush();
	equal(sent.length, 1);
	clock.advance(100);
	await flush();
	equal((await p).operation.status, Status.OutcomeUnknown);
	f.client.invalidate();
	truth(f.client.confirmQuiescence(proof()));
	equal(f.client.admit(model, header, 18), Status.Ok);
	clock.advance(1000);
	await flush();
	equal(sent.length, 1);
	f.client.close();
}
// Final RPC WRITE ACK is accepted, but invalidation prevents the next Resource READ handoff.
{
	const clock = new FakeClock(), sent = [];
	let acknowledge;
	const backend = new RpcBackend(packet => {
		sent.push(packet.slice());
		return new Promise(resolve => acknowledge = resolve);
	}, {fragmentBytes : 128, ackTimeoutMs : 50, pollIntervalMs : 0});
	const f = fixture({backend, clock}), command = bind(f.client, 'Command');
	const p = f.client.command(command, dto(7));
	let completed = 0;
	p.then(() => ++completed);
	await flush();
	equal(sent.length, 1);
	equal(sent[0][0], 4);
	equal(sent[0][13], 1);
	const packet = new DataView(sent[0].buffer), count = packet.getUint16(14, true),
	      id = packet.getUint32(24, true);
	f.client.invalidate();
	const ack = new Uint8Array(14), view = new DataView(ack.buffer);
	view.setBigUint64(1, (BigInt(id) << 32n) | BigInt(count), true);
	view.setUint32(9, count, true);
	ack[13] = 1;
	acknowledge(ack);
	await flush();
	equal(sent.length, 1);
	equal(completed, 0);
	clock.advance(99);
	await flush();
	equal(sent.length, 1);
	equal(completed, 0);
	clock.advance(1);
	await flush();
	equal((await p).operation.status, Status.OutcomeUnknown);
	equal(completed, 1);
	equal(sent.length, 1);
	truth(f.client.recoveryRequired);
	truth(f.client.confirmQuiescence(proof()));
	f.client.close();
}
// RPC resource fragments are physical sends, but only the final WRITE can execute a Command.
for (const fragmentBytes of [24, 128]) {
	const clock = new FakeClock(), sent = [];
	const backend = new RpcBackend(packet => {
		sent.push(packet.slice());
		return new Promise(() => {
		                   });
	}, {fragmentBytes, ackTimeoutMs : 50, pollIntervalMs : 0});
	const f = fixture({backend, clock}), command = bind(f.client, 'Command');
	const p = f.client.command(command, dto(7));
	await flush();
	equal(sent.length, 1);
	f.client.invalidate();
	clock.advance(100);
	await flush();
	const result = await p;
	equal(result.operation.status, fragmentBytes === 24 ? Status.Cancelled : Status.OutcomeUnknown);
	equal(result.value, undefined);
	equal(sent.length, 1);
	truth(!f.client.confirmQuiescence({...proof(), remoteSessionClosed : false}));
	truth(f.client.confirmQuiescence(proof()));
	equal(f.client.admit(model, header, 18), Status.Ok);
	clock.advance(1000);
	await flush();
	equal(sent.length, 1);
	f.client.close();
}
console.log(`Dynamic deterministic checks=${checks} failures=0`);
