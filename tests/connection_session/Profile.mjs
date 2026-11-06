// Named reference-profile bootstrap against controlled HTTP/WS surfaces; no UX runtime. MIT.
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {demoWebSocket, bindWebSocket} from 'telemetry-sdk/transports/websocket';
import {EncodedBackend} from 'telemetry-sdk';
import {FakeClock, flush, u32Bytes} from './Trace.mjs';
const descriptor = new Uint8Array(await readFile(new URL('./descriptor.bin', import.meta.url)));
let checks = 0, scenarios = 0;
const equal = (actual, expected, label) => {
	checks++;
	assert.deepEqual(actual, expected, label);
};
const truth = (actual, label) => {
	checks++;
	assert(actual, label);
};
const deferred = () => {
	let resolve, reject;
	const promise = new Promise((yes, no) => {
		resolve = yes;
		reject = no;
	});
	return {promise, resolve, reject};
};

async function failure(promise)
{
	let error;
	try {
		await promise;
	} catch (reason) {
		error = reason;
	}
	truth(error instanceof Error);
	return error;
}

async function test(body)
{
	await body();
	scenarios++;
}

function environment(settings = {})
{
	const clock = new FakeClock(), sockets = [], requests = [];

	class Socket {
		static CONNECTING = 0;
		static OPEN = 1;
		static CLOSING = 2;
		static CLOSED = 3;
		readyState = 0;
		protocol = '';
		binaryType = 'blob';
		bufferedAmount = 0;
		events = new Map();
		sent = [];
		closes = 0;

		constructor(url, protocol)
		{
			this.url = String(url);
			this.requestedProtocol = protocol;
			sockets.push(this);
		}

		addEventListener(type, callback, options = {})
		{
			const entries = this.events.get(type) ?? new Map();
			entries.set(callback, {callback, once : options?.once === true});
			this.events.set(type, entries);
		}

		removeEventListener(type, callback)
		{
			this.events.get(type)?.delete(callback);
		}

		emit(type, values = {})
		{
			const event = {type, target : this, ...values};
			for (const record of Array.from(this.events.get(type)?.values() ?? [])) {
				if (record.once)
					this.removeEventListener(type, record.callback);
				record.callback.call(this, event);
			}
		}

		greet(generation = 17, text = JSON.stringify({session : generation}),
		      protocol = this.requestedProtocol)
		{
			this.readyState = 1;
			this.protocol = protocol;
			this.emit('open');
			this.emit('message', {data : text});
		}

		send(bytes)
		{
			assert.equal(this.readyState, 1);
			this.sent.push(new Uint8Array(bytes));
		}

		close()
		{
			this.closes++;
			if (this.readyState === 3)
				return;
			this.readyState = 2;
			if (settings.autoClose !== false)
				queueMicrotask(() => this.finishClose());
		}

		finishClose(code = 1000)
		{
			this.readyState = 3;
			this.emit('close', {code, reason : '', wasClean : code === 1000});
		}

		listenerCount()
		{
			return [...this.events.values() ].reduce((sum, entries) => sum + entries.size, 0);
		}
	}

	const fetch = async (input, options = {}) => {
		const url = new URL(String(input));
		requests.push({url : url.href, path : url.pathname, options});
		if (options.signal?.aborted)
			throw new Error('Aborted fetch');
		if (settings.holdPath === url.pathname) {
			return new Promise((resolve, reject) => {
				settings.releaseFetch = resolve;
				options.signal?.addEventListener('abort', () => reject(new Error('Fetch aborted')),
				                                 {once : true});
			});
		}
		if (settings.responses?.[url.pathname])
			return settings.responses[url.pathname]();
		if (url.pathname === '/demo/transport')
			return new Response(JSON.stringify(settings.transport ?? {
				kind : 'websocket',
				url : 'ws://127.0.0.1:8081/encoded',
				protocol : 'telemetry.encoded.v1'
			}));
		if (url.pathname === '/schema-header.bin')
			return new Response(settings.header ?? descriptor.slice(0, 64));
		if (url.pathname === '/descriptor.bin')
			return new Response(settings.descriptor ?? descriptor);
		return new Response(null, {status : 404});
	};
	const profile =
	    demoWebSocket('http://127.0.0.1:8080/some/page',
	                  {fetch, WebSocket : Socket, clock, timeoutMs : 50, ...settings.options});
	const open = (mode = 'static', extra = {}) => {
		const abort = new AbortController();
		return {
			abort,
			promise : profile.open({mode, signal : abort.signal, onDisconnect() {}, ...extra})
		};
	};
	return {profile, clock, sockets, requests, open, settings};
}

for (const mode of ['static', 'dynamic'])
	await test(async () => {
		const e = environment(), pending = e.open(mode);
		await flush();
		equal(e.sockets.length, 1);
		const socket = e.sockets[0];
		equal(socket.url, 'ws://127.0.0.1:8081/encoded');
		equal(socket.requestedProtocol, 'telemetry.encoded.v1');
		socket.greet();
		const lease = await pending.promise;
		equal(lease.session, 17);
		equal(lease.header, descriptor.slice(0, 64));
		if (mode === 'dynamic')
			equal(lease.descriptor, descriptor);
		truth(typeof lease.peer === 'string' && lease.peer.length > 0);
		truth(lease.backend instanceof EncodedBackend);
		equal(lease.supersedes, undefined);
		equal(lease.backend.supports('ReadMany'), false);
		equal(lease.backend.supports('Batch'), false);
		equal(e.requests.map(row => row.path).sort(), [
			'/demo/transport', mode === 'static' ? '/schema-header.bin' : '/descriptor.bin',
			'/capabilities/read-many.bin', '/capabilities/batch.bin'
		].sort());
		truth(e.requests.every(row => row.options.signal instanceof AbortSignal));
		equal(e.clock.timers.size, 0);
		const command = lease.backend.execute({
			operation : 'FieldRead',
			endpointId : 65537,
			session : 17,
			requestId : 1,
			payload : new Uint8Array(),
			responseBytes : 4
		},
		                                      {
			                                      session : 17,
			                                      requestId : 1,
			                                      signal : new AbortController().signal,
			                                      clock : e.clock,
			                                      handoff : () => true
		                                      });
		await flush();
		equal(socket.sent.length, 1);
		const reply = new Uint8Array(36);
		reply.set(socket.sent[0].subarray(0, 32));
		reply[5] = 1;
		new DataView(reply.buffer).setUint32(20, 4, true);
		reply.set(u32Bytes(73), 32);
		socket.emit('message', {data : reply.buffer});
		equal((await command).payload, u32Bytes(73));
		await lease.close();
		equal(socket.readyState, 3);
		equal(socket.closes, 1);
		equal(socket.listenerCount(), 0);
		await lease.close();
		equal(socket.closes, 1);
	});
await test(async () => {
	const e = environment({autoClose : false}), opening = e.open();
	await flush();
	e.sockets[0].greet();
	const lease = await opening.promise;
	let done = false;
	const closing = lease.close().then(() => done = true);
	await flush();
	equal(done, false);
	equal(e.sockets[0].readyState, 2);
	const second = e.open();
	const failed = failure(second.promise);
	await flush();
	equal(e.sockets.length, 1);
	await failed;
	e.sockets[0].finishClose();
	await closing;
	equal(done, true);
	equal(e.sockets[0].listenerCount(), 0);
});
await test(async () => {
	const e = environment(), first = e.open();
	await flush();
	e.sockets[0].greet(17);
	const prior = await first.promise;
	await prior.close();
	const previous = {peer : prior.peer, session : 17}, second = e.open('static', {previous});
	await flush();
	e.sockets[1].greet(18);
	const lease = await second.promise;
	equal(lease.peer, prior.peer);
	equal(lease.session, 18);
	equal(lease.supersedes, previous);
	await lease.close();
	equal(e.sockets[1].listenerCount(), 0);
});
await test(async () => {
	const settings = {}, e = environment(settings), first = e.open();
	await flush();
	e.sockets[0].greet(17);
	const prior = await first.promise;
	await prior.close();
	const previous = {peer : prior.peer, session : 17};
	settings.header = new Uint8Array(63);
	const rejected = e.open('static', {previous}), failed = failure(rejected.promise);
	await flush();
	e.sockets[1].greet(18);
	await failed;
	equal(e.sockets[1].readyState, 3);
	settings.header = descriptor.slice(0, 64);
	const third = e.open('static', {previous});
	await flush();
	e.sockets[2].greet(19);
	const lease = await third.promise;
	equal(lease.session, 19);
	equal(lease.supersedes, previous);
	equal(lease.peer, prior.peer);
	await lease.close();
	equal(e.sockets[2].listenerCount(), 0);
});
await test(async () => {
	const e = environment(), schedule = e.clock.setTimeout;
	let once = true;
	e.clock.setTimeout = (...arguments_) => {
		if (once) {
			once = false;
			throw new Error('Injected scheduler refused setup');
		}
		return schedule(...arguments_);
	};
	await failure(e.open().promise);
	equal(e.sockets.length, 0);
	const second = e.open();
	await flush();
	equal(e.sockets.length, 1);
	e.sockets[0].greet();
	const lease = await second.promise;
	equal(lease.session, 17);
	await lease.close();
	equal(e.clock.timers.size, 0);
});
await test(async () => {
	const late = deferred();
	let hold = true, cancels = 0;
	const e = environment({
		responses : {
			'/schema-header.bin' : () => hold ? late.promise : new Response(descriptor.slice(0, 64))
		}
	}),
	      opening = e.open();
	const failed = failure(opening.promise);
	await flush();
	e.sockets[0].greet(17);
	await flush();
	truth(e.requests.some(row => row.path === '/schema-header.bin'));
	opening.abort.abort();
	await failed;
	equal(e.sockets[0].readyState, 3);
	hold = false;
	late.resolve({
		status : 200,
		ok : true,
		body : {
			cancel() {
		        cancels++;
		        return Promise.resolve();
			}
		}
	});
	await flush();
	equal(cancels, 1);
	equal(e.sockets[0].listenerCount(), 0);
	const second = e.open();
	await flush();
	e.sockets[1].greet(18);
	const lease = await second.promise;
	equal(lease.session, 18);
	await lease.close();
});
await test(async () => {
	const e = environment(), first = e.open(), failed = failure(first.promise);
	await flush();
	e.sockets[0].greet(17);
	first.abort.abort();
	await failed;
	equal(e.sockets[0].readyState, 3);
	const second = e.open(), old = failure(second.promise);
	await flush();
	e.sockets[1].greet(17);
	await old;
	equal(e.sockets[1].readyState, 3);
	equal(e.sockets[1].sent.length, 0);
	const third = e.open();
	await flush();
	e.sockets[2].greet(18);
	const lease = await third.promise;
	equal(lease.session, 18);
	await lease.close();
	equal(e.sockets[2].listenerCount(), 0);
});
await test(async () => {
	let cancels = 0;
	const missing = () => ({
		status : 404,
		ok : false,
		body : {
			cancel() {
		        cancels++;
		        return new Promise(() => {
		                           });
			}
		}
	});
	const e = environment({
		responses : {'/capabilities/read-many.bin' : missing, '/capabilities/batch.bin' : missing}
	});
	const pending = e.open();
	await flush();
	e.sockets[0].greet();
	const lease = await pending.promise;
	equal(cancels, 2);
	equal(lease.readMany, undefined);
	equal(lease.batch, undefined);
	await lease.close();
	equal(e.sockets[0].readyState, 3);
	equal(e.clock.timers.size, 0);
});
for (const changedPeer of [false, true])
	await test(async () => {
		const settings = {}, e = environment(settings), first = e.open();
		await flush();
		e.sockets[0].greet(17);
		const prior = await first.promise;
		await prior.close();
		if (changedPeer)
			settings.transport = {
				kind : 'websocket',
				url : 'ws://127.0.0.1:8099/encoded',
				protocol : 'telemetry.encoded.v1'
			};
		const second = e.open('static', {previous : {peer : prior.peer, session : 17}});
		const failed = failure(second.promise);
		await flush();
		if (changedPeer) {
			equal(e.sockets.length, 1);
			await failed;
		} else {
			e.sockets[1].greet(17);
			await failed;
			equal(e.sockets[1].readyState, 3);
			equal(e.sockets[1].sent.length, 0);
			equal(e.sockets[1].listenerCount(), 0);
		}
	});
await test(async () => {
	const readMany = new Uint8Array(24).fill(1), batch = new Uint8Array(24).fill(2);
	const e = environment({
		responses : {
			'/capabilities/read-many.bin' : () => new Response(readMany),
			'/capabilities/batch.bin' : () => new Response(batch)
		}
	}),
	      pending = e.open();
	await flush();
	e.sockets[0].greet();
	const lease = await pending.promise;
	equal(lease.readMany, readMany);
	equal(lease.batch, batch);
	readMany[0] = 7;
	batch[0] = 7;
	equal(lease.readMany[0], 1);
	equal(lease.batch[0], 2);
	equal(lease.backend.supports('ReadMany'), false);
	equal(lease.backend.supports('Batch'), false);
	await lease.close();
	equal(e.sockets[0].listenerCount(), 0);
});
for (const greeting of ['{}', '{"session":0}', '{"session":4294967296}', '{"session":1.5}',
                        '{"session":17,"extra":true}', 'x'.repeat(257), new ArrayBuffer(8)]) {
	await test(async () => {
		const e = environment(), opening = e.open();
		const failed = failure(opening.promise);
		await flush();
		e.sockets[0].greet(17, greeting);
		await failed;
		equal(e.sockets[0].readyState, 3);
		equal(e.sockets[0].listenerCount(), 0);
		equal(e.sockets[0].sent.length, 0);
		equal(e.clock.timers.size, 0);
	});
}
await test(async () => {
	const e = environment(), opening = e.open();
	const failed = failure(opening.promise);
	await flush();
	e.sockets[0].greet(17, '{"session":17}', 'wrong.protocol');
	await failed;
	equal(e.sockets[0].readyState, 3);
	equal(e.sockets[0].sent.length, 0);
});
for (const transport of
         [{kind : 'http'},
          {kind : 'websocket', url : 'http://localhost/encoded', protocol : 'telemetry.encoded.v1'},
          {kind : 'websocket', url : 'ws://localhost/encoded', protocol : 'unknown'}]) {
	await test(async () => {
		const e = environment({transport}), opening = e.open();
		await failure(opening.promise);
		equal(e.sockets.length, 0);
		equal(e.clock.timers.size, 0);
	});
}
for (const mode of ['static', 'dynamic'])
	await test(async () => {
		const e = environment(mode === 'static' ? {header : new Uint8Array(63)}
		                                        : {descriptor : new Uint8Array([ 1, 2 ])});
		const opening = e.open(mode), failed = failure(opening.promise);
		await flush();
		e.sockets[0].greet();
		await failed;
		equal(e.sockets[0].readyState, 3);
		equal(e.sockets[0].sent.length, 0);
		equal(e.sockets[0].listenerCount(), 0);
	});
for (const path of ['/capabilities/read-many.bin', '/capabilities/batch.bin'])
	await test(async () => {
		const e = environment({responses : {[path] : () => new Response(null, {status : 503})}});
		const opening = e.open(), failed = failure(opening.promise);
		await flush();
		e.sockets[0].greet();
		await failed;
		equal(e.sockets[0].readyState, 3);
		equal(e.sockets[0].sent.length, 0);
	});
for (const path of ['/capabilities/read-many.bin', '/capabilities/batch.bin'])
	await test(async () => {
		const e = environment({responses : {[path] : () => new Response(new Uint8Array(25))}});
		const opening = e.open(), failed = failure(opening.promise);
		await flush();
		e.sockets[0].greet();
		await failed;
		equal(e.sockets[0].readyState, 3);
		equal(e.sockets[0].sent.length, 0);
	});
await test(async () => {
	const e = environment({options : {maxDescriptorBytes : 64}}), pending = e.open('dynamic');
	const failed = failure(pending.promise);
	await flush();
	e.sockets[0].greet();
	await failed;
	equal(e.sockets[0].readyState, 3);
	equal(e.sockets[0].listenerCount(), 0);
});
await test(async () => {
	const e = environment(), opening = e.open();
	const failed = failure(opening.promise);
	await flush();
	equal(e.sockets.length, 1);
	e.clock.advance(50);
	await failed;
	equal(e.sockets[0].readyState, 3);
	equal(e.sockets[0].sent.length, 0);
	equal(e.clock.timers.size, 0);
});
await test(async () => {
	const settings = {holdPath : '/schema-header.bin'}, e = environment(settings),
	      opening = e.open();
	const failed = failure(opening.promise);
	await flush();
	e.sockets[0].greet();
	await flush();
	truth(e.requests.some(row => row.path === '/schema-header.bin'));
	e.clock.advance(50);
	await failed;
	equal(e.sockets[0].readyState, 3);
	equal(e.clock.timers.size, 0);
	settings.releaseFetch?.(new Response(descriptor.slice(0, 64)));
	await flush();
	equal(e.sockets[0].sent.length, 0);
});
await test(async () => {
	const e = environment({autoClose : false}), opening = e.open();
	let settled = false;
	const failed = failure(opening.promise).then(() => settled = true);
	await flush();
	opening.abort.abort();
	await flush();
	equal(e.sockets[0].readyState, 2);
	equal(settled, false);
	e.sockets[0].finishClose();
	await failed;
	equal(settled, true);
	equal(e.sockets[0].listenerCount(), 0);
});
await test(async () => {
	const e = environment(), opening = e.open(), failed = failure(opening.promise);
	await flush();
	e.sockets[0].finishClose(1013);
	await failed;
	equal(e.sockets[0].sent.length, 0);
	equal(e.sockets[0].listenerCount(), 0);
});
await test(async () => {
	const e = environment(), opening = e.open();
	let notifications = 0;
	// A separate carrier remains borrowed: dispose must not call its owner's close().
	await flush();
	const socket = e.sockets[0];
	opening.abort.abort();
	await failure(opening.promise);
	const borrowed = new (socket.constructor)('ws://fixture/encoded', 'telemetry.encoded.v1');
	borrowed.greet();
	const carrier = bindWebSocket(borrowed, {
		onDisconnect() {
		    notifications++;
		}
	});
	carrier.dispose();
	carrier.dispose();
	equal(notifications, 1);
	equal(borrowed.closes, 0);
	equal(borrowed.listenerCount(), 0);
	equal(borrowed.readyState, 1);
});
for (const [enableReadMany, enableBatch] of [[ true, false ], [ false, true ], [ true, true ]])
	await test(async () => {
		const e = environment({options : {enableReadMany, enableBatch}}), opening = e.open();
		await flush();
		e.sockets[0].greet();
		const lease = await opening.promise;
		equal(lease.backend.supports('ReadMany'), enableReadMany);
		equal(lease.backend.supports('Batch'), enableBatch);
		const closing = lease.close();
		e.sockets[0].finishClose();
		await closing;
		equal(e.sockets[0].listenerCount(), 0);
	});
console.log('connection-profile checks=' + checks + ' cases=' + scenarios + ' failures=0');
