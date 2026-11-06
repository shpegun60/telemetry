// Packed SDK Session -> existing EncodedBackend -> unchanged native SDK Model. MIT.
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {readFile} from 'node:fs/promises';
import {connectStatic, connectDynamic, EncodedBackend, Status} from 'telemetry-sdk';
import * as sdk from './generated/js/device.js';

const [executable] = process.argv.slice(2);
const descriptor = new Uint8Array(await readFile(new URL('./descriptor.bin', import.meta.url)));
let checks = 0, packets = 0;
const equal = (actual, expected) => {
	checks++;
	assert.deepEqual(actual, expected);
};
const truth = value => {
	checks++;
	assert(value);
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
const outcomes = [];
for (const mode of ['static', 'dynamic']) {
	const child = spawn(executable, [], {stdio : [ 'pipe', 'pipe', 'pipe' ]});
	let pending, errors = '', active = 0, peak = 0;
	const exit = new Promise((resolve, reject) => {
		child.once('error', reject);
		child.once('exit', code => {
			if (pending)
				pending.reject(new Error('Native peer ended: ' + code + ' ' + errors));
			resolve(code);
		});
	});
	child.stderr.on('data', bytes => errors += bytes);
	createInterface({input : child.stdout}).on('line', line => {
		assert(pending, 'No unsolicited native response');
		const {resolve} = pending;
		pending = undefined;
		resolve(line);
	});
	const native = command => new Promise((resolve, reject) => {
		assert(!pending, 'Second physical exchange bypassed existing client FIFO');
		pending = {resolve, reject};
		child.stdin.write(command + '\n');
	});
	const backend = EncodedBackend.fromExchange(async packet => {
		active++;
		peak = Math.max(peak, active);
		packets++;
		try {
			return new Uint8Array(
			    Buffer.from(await native(Buffer.from(packet).toString('hex')), 'hex'));
		} finally {
			active--;
		}
	});
	const profile = {
		async open() {
		    return {
			    peer : 'native-fixture',
			    session : 17,
			    header : descriptor.slice(0, 64),
			    descriptor,
			    backend,
			    async close() {
			        child.stdin.end();
			        equal(await exit, 0);
			    }
		    };
		}
	};
	const session = mode === 'static'
	                    ? await connectStatic({sdk, transport : profile, timeoutMs : 5000})
	                    : await connectDynamic({transport : profile, timeoutMs : 5000});
	const endpoint = object => mode === 'static' ? object : object.id;
	const F = sdk.control.fields, C = sdk.control.commands, S = sdk.control.services;
	try {
		const results = [];
		results.push(await session.read(endpoint(F.Config)));
		equal(results[0].value.number, 42);
		equal(results[0].value.large, 0xffffffffffffffffn);
		equal(results[0].value.negative, -0x8000000000000000n);
		truth(Number.isNaN(results[0].value.f32));
		truth(Object.is(results[0].value.f64, -0));
		results.push(await session.write(endpoint(F.Config), dto(1111)));
		equal(results[1].succeeded, true);
		equal(results[1].valueStatus, 'NoValue');
		results.push(await session.read(endpoint(F.Config)));
		equal(results[2].value.number, 1000);
		results.push(await session.command(endpoint(C.Configure), dto(37)));
		equal(results[3].executed, true);
		results.push(await session.service(endpoint(S.Echo), dto(99)));
		equal(results[4].value.number, 99);
		results.push(await session.command(endpoint(C.Accepted), dto(10)));
		equal(results[5].accepted, true);
		equal(results[5].executed, false);
		results.push(await session.service(endpoint(S.Busy), dto(99)));
		equal(results[6].status, Status.Ok);
		equal(results[6].succeeded, false);
		equal(results[6].endpointStatus, 3);
		equal(results[6].valueStatus, 'NoValue');
		results.push(await session.service(endpoint(S.Empty), undefined));
		truth(results[7].value !== undefined);
		equal(Object.keys(results[7].value), []);
		equal(results[7].valueStatus, 'Ok');
		results.push(await session.service(endpoint(S.Ping), undefined));
		equal(results[8].value, undefined);
		equal(results[8].valueStatus, 'NoValue');
		results.push(await session.read(endpoint(F.Empty)));
		truth(results[9].value !== undefined);
		equal(Object.keys(results[9].value), []);
		equal(results[9].valueStatus, 'Ok');
		for (const [index, result] of results.entries()) {
			equal(result.requestId, index + 1);
			equal(result.session, 17);
			for (const key of ['status', 'operation', 'session', 'requestId', 'dispatch',
			                   'endpointStatus', 'succeeded', 'accepted', 'executed', 'payload'])
				equal(result[key], result.rawResult[key]);
		}
		equal(peak, 1);
		const state = JSON.parse(await native('state'));
		equal(state.reads, 3);
		equal(state.writes, 1);
		equal(state.commands, 2);
		equal(state.services, 4);
		equal(state.decoy, [ 0, 0, 0, 0 ]);
		outcomes.push(results.map(result => ({
			                          status : result.status,
			                          operation : result.operation,
			                          dispatch : result.dispatch,
			                          endpointStatus : result.endpointStatus,
			                          succeeded : result.succeeded,
			                          accepted : result.accepted,
			                          executed : result.executed,
			                          valueStatus : result.valueStatus,
			                          payload : Buffer.from(result.payload).toString('hex')
		                          })));
	} finally {
		await session.close();
	}
}
equal(outcomes[0], outcomes[1]);
console.log('connection-session-native checks=' + checks + ' packets=' + packets + ' failures=0');
