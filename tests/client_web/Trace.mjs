// Shared C++/JS logical traces, with caller-owned fake monotonic time. MIT.
import {readFile} from 'node:fs/promises';
import {pathToFileURL} from 'node:url';

export class FakeClock {
	value = 0;
	sequence = 0;
	timers = new Map();
	now = () => this.value;
	setTimeout = (callback, delay) => {
		const id = ++this.sequence;
		this.timers.set(id, {at : this.value + delay, callback});
		return id;
	};
	clearTimeout = id => this.timers.delete(id);

	advance(milliseconds)
	{
		this.value += milliseconds;
		for (let guard = 0;; ++guard) {
			if (guard > 10000)
				throw Error('Timer progress guard');
			const due = [...this.timers ].find(([, timer ]) => timer.at <= this.value);
			if (!due)
				break;
			this.timers.delete(due[0]);
			due[1].callback();
		}
	}
}

export const flush = async () => {
	for (let index = 0; index < 30; ++index)
		await Promise.resolve();
};
export const u32Bytes = value => {
	const result = new Uint8Array(4);
	new DataView(result.buffer).setUint32(0, value, true);
	return result;
};
export const u32Value = value =>
    new DataView(value.buffer, value.byteOffset, value.byteLength).getUint32(0, true);
export const hex = bytes => Buffer.from(bytes).toString('hex');
export const bindings = (id = 7) => ({
	1 : {
		fingerprint : 0n,
		operation : 'FieldWrite',
		id,
		requestBytes : 4,
		responseBytes : 0,
		encode : u32Bytes
	},
	2 : {
		fingerprint : 0n,
		operation : 'Command',
		id,
		requestBytes : 4,
		responseBytes : 0,
		encode : u32Bytes
	},
	3 : {
		fingerprint : 0n,
		operation : 'Service',
		id,
		requestBytes : 4,
		responseBytes : 4,
		encode : u32Bytes,
		decodeResponse : u32Value
	},
	4 : {
		fingerprint : 0n,
		operation : 'FieldRead',
		id,
		requestBytes : 0,
		responseBytes : 4,
		decode : u32Value
	},
});

export function invoke(client, binding, value, timeout)
{
	return ({
		FieldWrite : () => client.write(binding, value, timeout),
		Command : () => client.command(binding, value, timeout),
		Service : () => client.service(binding, value, timeout),
		FieldRead : () => client.read(binding, timeout)
	})[binding.operation]();
}

export async function runTrace(runtime, test)
{
	const clock = new FakeClock(), events = [], handles = {}, operations = bindings();
	let active;
	const backend = {
		execute : (request, context) => {
		    events.push(`START ${runtime.Operations[request.operation]} ${request.endpointId} ${
				hex(request.payload) || '-'}`);
		    return new Promise(resolve => {
			    active = {request, context, resolve};
		    });
		}
	};
	const client = new runtime.DeviceClient({
		backend,
		clock,
		matchesHeader : () => true,
		maxPending : 3,
		maxPayloadBytes : 12,
		timeoutMs : 100
	});
	for (const action of test.actions) {
		const [command, ...args] = action.split(' ');
		switch (command) {
			case 'OPEN':
				client.openSession(+args[0], new Uint8Array(64));
				break;
			case 'SUB': {
				const [tag, op, value, timeout] = args;
				const handle = handles[tag] = invoke(client, operations[+op], +value, +timeout);
				handle.then(result => events.push(`DONE ${tag} ${result.status} ${
							    result.dispatch} ${result.endpointStatus} ${result.value ?? '-'}`));
				break;
			}
			case 'HANDOFF':
				active.context.handoff(true);
				break;
			case 'REPLY':
				active.resolve({
					dispatch : +args[0],
					endpointStatus : +args[1],
					payload : args[2] === '-' ? new Uint8Array(0) : u32Bytes(+args[2])
				});
				break;
			case 'ADVANCE':
				clock.advance(+args[0]);
				client.process();
				break;
			case 'CANCEL':
				handles[args[0]].cancel();
				break;
			case 'RESET':
				client.resetSession();
				break;
			case 'CONFIRM':
				client.confirmSessionClosed(+args[0]);
				break;
			case 'PUMP':
				client.process();
				break;
			default:
				throw Error('Unknown shared action ' + action);
		}
		await flush();
	}
	if (client.pendingCount !== 0)
		throw Error('Trace leaves accepted requests unresolved: ' + test.name);
	return events;
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
	const runtime = await import(pathToFileURL(process.argv[2]).href);
	const tests = JSON.parse(await readFile(new URL('./traces.json', import.meta.url)));
	for (const test of tests)
		console.log(JSON.stringify({name : test.name, events : await runTrace(runtime, test)}));
}
