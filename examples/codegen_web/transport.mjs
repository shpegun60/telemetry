/* Optional application adapter: complete Resource v1 packets around RPC v1.
 * The caller supplies framing, timeout, admission, identity agreement and
 * strictly increasing transaction IDs. Serialize calls for one mailbox.
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
 */

export class RpcError extends Error {
	constructor(message, details = {})
	{
		super(message);
		this.name = 'RpcError';
		Object.assign(this, details);
	}
}

const codes = Object.freeze({FieldWrite : 1, Command : 2, Service : 3, FieldRead : 4});
const ensure = (condition, message) => {
	if (!condition)
		throw new RpcError(message);
};
const cursor = (transactionId, offset) => (BigInt(transactionId) << 32n) | BigInt(offset);
const integer = (value, minimum, maximum, name) =>
    ensure(Number.isSafeInteger(value) && value >= minimum && value <= maximum, name);
const view = bytes => new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);

function resourceRead(fileIndex, offset)
{
	const packet = new Uint8Array(13), data = view(packet);
	data.setUint8(0, 3);
	data.setUint32(1, fileIndex, true);
	data.setBigUint64(5, offset, true);
	return packet;
}

function resourceWrite(fileIndex, offset, fragment, final)
{
	const packet = new Uint8Array(16 + fragment.length), data = view(packet);
	data.setUint8(0, 4);
	data.setUint32(1, fileIndex, true);
	data.setBigUint64(5, offset, true);
	data.setUint8(13, final ? 1 : 0);
	data.setUint16(14, fragment.length, true);
	packet.set(fragment, 16);
	return packet;
}

function parseResource(reply, bytes)
{
	ensure(reply instanceof Uint8Array, 'Exchange must return Uint8Array');
	ensure(reply.length >= bytes, 'Truncated Resource reply');
	ensure(reply[0] <= 8, 'Unknown Resource status');
	if (reply[0] !== 0)
		throw new RpcError('Resource refused packet', {resourceStatus : reply[0]});
	return view(reply);
}

function readChunk(reply)
{
	const data = parseResource(reply, 12), count = data.getUint16(10, true);
	ensure(reply.length === 12 + count, 'Resource READ length');
	ensure(reply[9] <= 1, 'Resource READ eof');
	return {next : data.getBigUint64(1, true), eof : reply[9] === 1, bytes : reply.subarray(12)};
}

// Return checked transport outcomes; this function never decodes value payloads.
export async function rpcExchange(exchange, operation, id, payload, options)
{
	ensure(typeof exchange === 'function', 'Exchange callback required');
	ensure(Object.hasOwn(codes, operation), 'Explicit RPC operation required');
	ensure(payload instanceof Uint8Array, 'RPC payload requires Uint8Array');
	ensure(options !== null && typeof options === 'object',
	       'RPC options with transaction ID required');
	const {
		transactionId,
		fileIndex = 0,
		fragmentBytes = 48,
		maxPackets = 256,
		maxReplyBytes = 1048576,
		maxRequestBytes = 1048576
	} = options;
	integer(transactionId, 1, 0xffffffff, 'Transaction ID must be a nonzero u32');
	integer(fileIndex, 0, 0xffffffff, 'File index must be u32');
	integer(id, 0, 0xffffffff, 'Endpoint ID must be u32');
	integer(fragmentBytes, 24, 65535, 'First WRITE must fit the RPC header');
	integer(maxPackets, 1, 1000000, 'Packet budget');
	integer(maxReplyBytes, 32, 0xffffffff, 'Reply byte budget');
	integer(maxRequestBytes, 24, 0xffffffff, 'Request byte budget');
	ensure(payload.length <= maxRequestBytes - 24, 'Request exceeds byte budget');
	ensure(operation !== 'FieldRead' || payload.length === 0, 'FieldRead payload must be empty');
	let packets = 0;
	const send = async packet => {
		ensure(++packets <= maxPackets, 'Packet budget exhausted');
		return await exchange(packet);
	};
	const request = new Uint8Array(24 + payload.length), header = view(request);
	header.setUint32(0, 0x43505254, true); // TRPC
	header.setUint16(4, 1, true);
	header.setUint16(6, 0, true);
	header.setUint32(8, transactionId, true);
	header.setUint32(12, id, true);
	header.setUint32(16, payload.length, true);
	header.setUint8(20, codes[operation]);
	request.set(payload, 24);
	let offset = 0;
	while (offset < request.length) {
		const fragment = request.subarray(offset, Math.min(offset + fragmentBytes, request.length));
		const final = offset + fragment.length === request.length;
		const reply = await send(
		    resourceWrite(fileIndex, offset ? cursor(transactionId, offset) : 0n, fragment, final));
		const data = parseResource(reply, 14);
		ensure(reply.length === 14 && reply[13] <= 1, 'Resource WRITE envelope');
		ensure(data.getUint32(9, true) === fragment.length, 'Resource WRITE consumed count');
		ensure(data.getBigUint64(1, true) === cursor(transactionId, offset + fragment.length),
		       'Resource WRITE cursor identity');
		ensure((reply[13] === 1) === final, 'Resource WRITE completion');
		offset += fragment.length;
	}
	// WRITE next belongs to the request stream. Every status poll starts at zero.
	let first, resultHeader;
	for (;;) {
		first = readChunk(await send(resourceRead(fileIndex, 0n)));
		ensure(first.bytes.length >= 32, 'RPC header cannot be fragmented');
		const data = view(first.bytes);
		ensure(data.getUint32(0, true) === 0x53505254 && data.getUint16(4, true) === 1 &&
		           data.getUint16(6, true) === 0,
		       'RPC reply magic/version');
		ensure(data.getUint32(8, true) === transactionId && data.getUint32(12, true) === id &&
		           data.getUint8(20) === codes[operation],
		       'RPC reply transaction/endpoint/operation identity');
		const state = data.getUint8(21), payloadBytes = data.getUint32(16, true);
		ensure(data.getUint32(24, true) === request.length &&
		           data.getUint32(28, true) === request.length,
		       'RPC accepted request byte counts');
		if (state === 4) {
			resultHeader = {
				payloadBytes,
				dispatchStatus : data.getUint8(22),
				endpointStatus : data.getUint8(23)
			};
			break;
		}
		ensure(state === 2 || state === 3, 'RPC must be Pending, Running or Ready');
		ensure(payloadBytes === 0 && data.getUint8(22) === 0 && data.getUint8(23) === 0 &&
		           first.bytes.length === 32 && first.eof && first.next === 0n,
		       'RPC pending snapshot');
	}
	const total = 32 + resultHeader.payloadBytes;
	ensure(total <= maxReplyBytes, 'RPC result exceeds byte budget');
	ensure(first.bytes.length <= total &&
	           first.next === cursor(transactionId, first.bytes.length) &&
	           first.eof === (first.bytes.length === total),
	       'RPC first READ cursor/length/eof');
	const record = new Uint8Array(total);
	record.set(first.bytes);
	offset = first.bytes.length;
	while (offset < total) {
		const chunk = readChunk(await send(resourceRead(fileIndex, cursor(transactionId, offset))));
		ensure(chunk.bytes.length > 0 && chunk.bytes.length <= total - offset,
		       'RPC READ progress/bounds');
		ensure(chunk.next === cursor(transactionId, offset + chunk.bytes.length) &&
		           chunk.eof === (offset + chunk.bytes.length === total),
		       'RPC continuation cursor/length/eof');
		record.set(chunk.bytes, offset);
		offset += chunk.bytes.length;
	}
	const {dispatchStatus, endpointStatus} = resultHeader;
	ensure([ 0, 4, 5, 6, 7, 8, 9 ].includes(dispatchStatus), 'Unknown dispatch status');
	if (dispatchStatus !== 0) {
		ensure(endpointStatus === 0 && resultHeader.payloadBytes === 0,
		       'Failed dispatch carried endpoint result');
		throw new RpcError('Encoded dispatch refused operation', {dispatchStatus, endpointStatus});
	}
	const maximumStatus = {FieldRead : 0, FieldWrite : 5, Command : 7, Service : 4}[operation];
	ensure(endpointStatus <= maximumStatus, 'Unknown endpoint status');
	const successful = endpointStatus === 0 || (operation === 'Command' && endpointStatus === 1);
	const hasValue = operation === 'FieldRead' || operation === 'Service';
	ensure((successful && hasValue) || resultHeader.payloadBytes === 0,
	       'Operation/status must have no payload');
	if (!successful)
		throw new RpcError('Endpoint refused operation', {dispatchStatus, endpointStatus});
	return Object.freeze({
		operation,
		id,
		transactionId,
		dispatchStatus,
		endpointStatus,
		accepted : operation === 'Command' && endpointStatus === 1,
		payload : record.subarray(32)
	});
}

// Generated metadata supplies the operation and codec. Status checks above run
// before any decode, including zero-byte Empty/Void service response codecs.
export async function rpcInvoke(exchange, binding, value, options)
{
	ensure(binding !== null && typeof binding === 'object',
	       'Generated operation metadata required');
	const payload = binding.operation === 'FieldRead' ? new Uint8Array(0) : binding.encode(value);
	const outcome = await rpcExchange(exchange, binding.operation, binding.id, payload, options);
	const decoded = binding.operation === 'FieldRead' ? binding.decode(outcome.payload)
	                : binding.operation === 'Service' ? binding.decodeResponse(outcome.payload)
	                                                  : undefined;
	return Object.freeze({...outcome, value : decoded});
}
