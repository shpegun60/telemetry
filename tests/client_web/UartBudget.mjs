// Loss-free UART timing model: handoff deadlines include full framed TX and RX. MIT.
import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';
import {FakeClock, flush} from './Trace.mjs';

const {DeviceClient, RetryingEncodedBackend, Status} =
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
const BAUD = 115200, BITS = 10, PAYLOAD = 2048, FRAME = 32 + PAYLOAD;
const TX = Math.ceil(FRAME * BITS * 1000 / BAUD), RX = TX, SERVER = 7;
const ACTUAL_JITTER = 11, JITTER_BUDGET = 20, REPLY = TX + SERVER + RX + ACTUAL_JITTER;
const BUDGET = TX + SERVER + RX + JITTER_BUDGET, HEADER = new Uint8Array(64);
equal(FRAME, 2080);
equal(TX, 181);
equal(REPLY, 380);
equal(BUDGET, 389);
const observed = [];
for (const [replyTimeoutMs, actualJitter] of [[ 100, 11 ], [ BUDGET, 11 ], [ 500, 11 ],
                                              [ 500, 20 ]]) {
	const clock = new FakeClock(), sent = [], received = [], handoffs = [];
	const replyAt = TX + SERVER + RX + actualJitter;
	let busy = 0, effects = 0, outcome, completedAt;
	const backend = new RetryingEncodedBackend((packet, context) => {
		equal(busy, 0);
		++busy;
		equal(packet.length, FRAME);
		equal(packet.subarray(32), new Uint8Array(PAYLOAD).fill(0x33));
		const frame = packet.slice();
		sent.push({at : clock.now(), frame, context});
		return new Promise(resolve => {
			// Abort cannot truncate this simulated in-flight UART frame. Every frame arrives.
			clock.setTimeout(() => {
				equal(busy, 1);
				--busy;
				++effects;
				const reply = frame.slice();
				reply[5] = 1;
				reply.fill(0x5a, 32);
				clock.setTimeout(
				    () => received.push({at : clock.now(), accepted : backend.receive(reply)}),
				    SERVER + RX + actualJitter);
				resolve(); // Physical TX no longer owns its bytes.
			}, TX);
		});
	}, {replyTimeoutMs, maxRetries : 1});
	const execute = backend.execute.bind(backend);
	backend.execute = (request, context) => execute(request, {
		...context,
		handoff : mayExecute => {
		    truth(mayExecute);
		    handoffs.push(clock.now());
		    return context.handoff(mayExecute);
		}
	});
	const client =
	    new DeviceClient({backend, clock, matchesHeader : () => true, timeoutMs : 30000});
	truth(client.openSession(1, HEADER));
	const binding = {
		fingerprint : 0n,
		operation : 'Service',
		id : 7,
		requestBytes : PAYLOAD,
		responseBytes : PAYLOAD,
		encode : value => value.slice(),
		decodeResponse : value => value.slice()
	};
	const input = new Uint8Array(PAYLOAD).fill(0x33), request = client.service(binding, input);
	input.fill(0xff);
	request.then(result => {
		outcome = result;
		completedAt = clock.now();
	});
	// Process timers in chronological order even when a caller jumps fake time far ahead.
	const advanceTo = async target => {
		for (let guard = 0; guard < 100; ++guard) {
			const next = Math.min(...[...clock.timers.values()].map(timer => timer.at));
			if (next > target) {
				clock.advance(target - clock.now());
				await flush();
				return;
			}
			clock.advance(next - clock.now());
			await flush();
		}
		throw Error('UART timer progress guard');
	};
	await flush();
	equal(sent.map(send => send.at), [ 0 ]);
	await advanceTo(100);
	equal(sent.length, 1);
	equal(busy, 1);
	equal(effects, 0);
	equal(sent[0].context.signal.aborted, replyTimeoutMs === 100);
	await advanceTo(TX);
	equal(effects, 1);
	equal(sent.map(send => send.at), replyTimeoutMs === 100 ? [ 0, 181 ] : [ 0 ]);
	equal(handoffs, sent.map(send => send.at));
	equal(busy, replyTimeoutMs === 100 ? 1 : 0);
	equal(outcome, undefined);
	if (replyTimeoutMs === 100) {
		equal(sent[0].frame, sent[1].frame);
		truth(sent[0].context.signal !== sent[1].context.signal);
		await advanceTo(281);
		equal(outcome, undefined);
		equal(busy, 1);
		await advanceTo(362);
		equal(outcome.status, Status.OutcomeUnknown);
		equal(client.recoveryRequired, true);
		equal(busy, 0);
	}
	await advanceTo(replyAt);
	const result = await request;
	equal(result.status, replyTimeoutMs === 100 ? Status.OutcomeUnknown : Status.Ok);
	equal(result.requestId, request.requestId);
	equal(result.session, 1);
	equal(received[0], {at : replyAt, accepted : replyTimeoutMs !== 100});
	equal(client.recoveryRequired, replyTimeoutMs === 100);
	if (replyTimeoutMs !== 100) {
		equal(completedAt, replyAt);
		truth(result.succeeded);
		if (actualJitter === JITTER_BUDGET)
			equal(replyTimeoutMs - replyAt, 111);
		equal(result.value, new Uint8Array(PAYLOAD).fill(0x5a));
	}
	await advanceTo(
	    600); // Both original and repeated replies arrived; no frame was discarded by the model.
	equal(sent.length, replyTimeoutMs === 100 ? 2 : 1);
	equal(effects, sent.length);
	equal(received.length, sent.length);
	if (sent.length === 2)
		equal(received[1], {at : 561, accepted : false});
	equal(client.pendingCount, 0);
	equal(client.snapshotBytes, 0);
	equal(busy, 0);
	equal(clock.timers.size, 0);
	client.close();
	truth(client.drained);
	observed.push({
		replyTimeoutMs,
		actualJitter,
		sends : sent.map(send => send.at),
		replies : received,
		status : result.status,
		completedAt,
		endpointInvocations : effects
	});
}
console.log('UART budget traces=' + JSON.stringify(observed));
console.log(`uart budget checks=${checks} failures=0`);
