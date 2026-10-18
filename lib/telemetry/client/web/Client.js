/* One-owner asynchronous FIFO client. Authors: Ruslan Kovtun (shpegun60), codexAi. MIT. */
export const Status = Object.freeze({
    Ok: 0,
    QueueFull: 1,
    PayloadFull: 2,
    NotAdmitted: 3,
    ReadOnly: 4,
    Closed: 5,
    Cancelled: 6,
    Timeout: 7,
    SessionLost: 8,
    OutcomeUnknown: 9,
    MalformedReply: 10,
    TransportError: 11,
    CodecError: 12,
    BufferTooSmall: 13,
    InvalidArgument: 14,
    Busy: 15,
    RecoveryRequired: 16,
    IdsExhausted: 17,
});
export const Operations = Object.freeze({ FieldWrite: 1, Command: 2, Service: 3, FieldRead: 4 });
const systemClock = {
    now: () => performance.now(),
    setTimeout: (callback, milliseconds) => setTimeout(callback, milliseconds),
    clearTimeout: handle => clearTimeout(handle),
};
const bytes = (input) => input instanceof ArrayBuffer ? new Uint8Array(input)
    : new Uint8Array(input.buffer, input.byteOffset, input.byteLength);
const u32 = (n) => Number.isInteger(n) && n >= 0 && n <= 0xffffffff;
const validOperation = (operation) => Object.hasOwn(Operations, operation);
class BackendFailure extends Error {
    status;
    constructor(status, message) {
        super(message);
        this.status = status;
    }
}
function checkWireResult(request, result) {
    const { dispatch, endpointStatus, payload } = result;
    if (![0, 4, 5, 6, 7, 8, 9].includes(dispatch) || !u32(endpointStatus) ||
        (dispatch !== 0 && (endpointStatus !== 0 || payload.length !== 0)))
        throw new BackendFailure(Status.MalformedReply, 'Invalid dispatch/status envelope');
    if (dispatch !== 0)
        return;
    const maximum = { FieldRead: 0, FieldWrite: 5, Command: 7, Service: 4 }[request.operation];
    const expected = endpointStatus === 0 &&
        (request.operation === 'FieldRead' || request.operation === 'Service')
        ? request.responseBytes
        : 0;
    if (endpointStatus > maximum || payload.length !== expected)
        throw new BackendFailure(Status.MalformedReply, 'Invalid endpoint status or payload length');
}
export class DeviceClient {
    backend;
    identity;
    clock;
    limit;
    byteLimit;
    timeout;
    pending = [];
    active;
    epoch = 0;
    fingerprint = 0n;
    lastEpoch = 0;
    nextId = 1;
    payloadBytes = 0;
    recoveryEpoch = 0;
    stopped = false;
    admitted = false;
    timer;
    scheduled = false;
    pumping = false;
    constructor(options) {
        this.backend = options.backend;
        this.identity = options.matchesHeader;
        this.clock = options.clock ?? systemClock;
        this.limit = options.maxPending ?? 64;
        this.byteLimit = options.maxPayloadBytes ?? 1048576;
        this.timeout = options.timeoutMs ?? 5000;
        if (!Number.isSafeInteger(this.limit) || this.limit < 1 ||
            !Number.isSafeInteger(this.byteLimit) || this.byteLimit < 0 ||
            !Number.isFinite(this.timeout) || this.timeout <= 0 || this.timeout > 0x7fffffff)
            throw new RangeError('Client capacity/deadline');
    }
    get session() {
        return this.epoch;
    }
    get ready() {
        return this.admitted && !this.stopped && this.recoveryEpoch === 0;
    }
    get recoveryRequired() {
        return this.recoveryEpoch !== 0;
    }
    get recoveryGeneration() {
        return this.recoveryEpoch;
    }
    get pendingCount() {
        return this.pending.length;
    }
    get snapshotBytes() {
        return this.payloadBytes;
    }
    // Caller opens a fresh physical session and supplies its admitted descriptor header.
    openSession(generation, header) {
        if (this.stopped || this.recoveryRequired || this.pending.length || !u32(generation) ||
            generation === 0 || generation <= this.lastEpoch)
            return false;
        let matches = false;
        let declared;
        try {
            declared = bytes(header).slice();
            matches = declared.length === 64 && this.identity(declared);
        }
        catch {
            return false;
        }
        if (!matches)
            return false;
        this.fingerprint = new DataView(declared.buffer).getBigUint64(16, true);
        this.epoch = this.lastEpoch = generation;
        this.nextId = 1;
        this.admitted = true;
        return true;
    }
    // Assertion by the application: old transport and remote retained session are closed/drained.
    // Re-admitting a header alone never makes an uncertain old session safe.
    confirmSessionClosed(generation) {
        if (!this.recoveryRequired || generation !== this.recoveryEpoch || this.pending.length)
            return false;
        this.recoveryEpoch = 0;
        return true;
    }
    resetSession() {
        this.admitted = false;
        this.cancelAll(Status.SessionLost);
    }
    close() {
        this.stopped = true;
        this.admitted = false;
        this.cancelAll(Status.Cancelled);
    }
    read(field, timeoutMs) {
        const binding = 'read' in field ? field.read : field;
        return this.enqueue(binding, undefined, binding.decode, timeoutMs);
    }
    write(field, value, timeoutMs) {
        const binding = 'read' in field ? field.write : field;
        if (!binding)
            return this.refused('FieldWrite', Status.ReadOnly);
        return this.enqueue(binding, value, undefined, timeoutMs);
    }
    command(binding, value, timeoutMs) {
        return this.enqueue(binding, value, undefined, timeoutMs);
    }
    service(binding, value, timeoutMs) {
        return this.enqueue(binding, value, binding.decodeResponse, timeoutMs);
    }
    refused(operation, status) {
        const result = this.makeResult(operation, this.epoch, 0, status);
        return Object.assign(Promise.resolve(result), {
            requestId: 0,
            session: this.epoch,
            cancel: () => false,
        });
    }
    enqueue(binding, value, decode, timeoutMs) {
        const operation = binding.operation;
        if (this.stopped)
            return this.refused(operation, Status.Closed);
        if (this.recoveryRequired)
            return this.refused(operation, Status.RecoveryRequired);
        if (!this.ready || binding.fingerprint !== this.fingerprint)
            return this.refused(operation, Status.NotAdmitted);
        if (this.pending.length >= this.limit)
            return this.refused(operation, Status.QueueFull);
        const timeout = timeoutMs ?? this.timeout, now = this.clock.now(), generation = this.epoch;
        if (!validOperation(operation) || !u32(binding.id) || !u32(binding.requestBytes) ||
            !u32(binding.responseBytes) || !Number.isFinite(timeout) || timeout <= 0 ||
            timeout > 0x7fffffff)
            return this.refused(operation, Status.InvalidArgument);
        if (this.nextId > 0xffffffff)
            return this.refused(operation, Status.IdsExhausted);
        if (binding.requestBytes > this.byteLimit - this.payloadBytes)
            return this.refused(operation, Status.PayloadFull);
        let payload;
        try {
            payload = operation === 'FieldRead' ? new Uint8Array(0)
                : binding.encode(value).slice();
            if (!(payload instanceof Uint8Array) || payload.length !== binding.requestBytes)
                return this.refused(operation, Status.CodecError);
        }
        catch {
            return this.refused(operation, Status.CodecError);
        }
        // Re-entrant getters/codec callbacks may have changed session or capacity.
        if (this.stopped)
            return this.refused(operation, Status.Closed);
        if (!this.ready || generation !== this.epoch || binding.fingerprint !== this.fingerprint)
            return this.refused(operation, this.recoveryRequired ? Status.RecoveryRequired
                : Status.NotAdmitted);
        if (this.pending.length >= this.limit)
            return this.refused(operation, Status.QueueFull);
        if (this.nextId > 0xffffffff)
            return this.refused(operation, Status.IdsExhausted);
        if (payload.length > this.byteLimit - this.payloadBytes)
            return this.refused(operation, Status.PayloadFull);
        const request = Object.freeze({
            operation,
            endpointId: binding.id,
            session: this.epoch,
            requestId: this.nextId++,
            payload,
            responseBytes: binding.responseBytes
        });
        let resolve;
        const promise = new Promise(done => {
            resolve = done;
        });
        const pending = {
            request,
            resolve,
            decode,
            deadline: now + timeout,
            controller: new AbortController(),
            possibleExecution: false,
            done: false
        };
        this.pending.push(pending);
        this.payloadBytes += payload.length;
        this.schedule();
        this.armTimer();
        return Object.assign(promise, {
            requestId: request.requestId,
            session: request.session,
            cancel: () => this.cancel(pending)
        });
    }
    makeResult(operation, session, requestId, status, wire, value) {
        const dispatch = wire?.dispatch ?? 0, endpointStatus = wire?.endpointStatus ?? 0;
        const succeeded = status === Status.Ok && dispatch === 0 &&
            (endpointStatus === 0 || (operation === 'Command' && endpointStatus === 1));
        return Object.freeze({
            status,
            operation,
            session,
            requestId,
            dispatch,
            endpointStatus,
            succeeded,
            accepted: succeeded && operation === 'Command' && endpointStatus === 1,
            executed: succeeded && operation === 'Command' && endpointStatus === 0,
            payload: wire?.payload.slice() ?? new Uint8Array(0),
            value
        });
    }
    schedule() {
        if (this.scheduled)
            return;
        this.scheduled = true;
        queueMicrotask(() => {
            this.scheduled = false;
            this.process();
        });
    }
    process() {
        if (this.pumping)
            return;
        this.pumping = true;
        try {
            const now = this.clock.now();
            for (const pending of this.pending.slice()) {
                if (!pending.done && now >= pending.deadline)
                    this.finish(pending, Status.Timeout);
            }
            if (!this.active && this.ready && this.pending.length) {
                const pending = this.active = this.pending[0];
                const context = {
                    session: pending.request.session,
                    requestId: pending.request.requestId,
                    signal: pending.controller.signal,
                    clock: this.clock,
                    handoff: mayExecute => {
                        if (pending.done || pending !== this.active || !this.ready ||
                            this.epoch !== pending.request.session)
                            return false;
                        if (this.clock.now() >= pending.deadline) {
                            this.finish(pending, Status.Timeout);
                            return false;
                        }
                        pending.possibleExecution ||= mayExecute;
                        return true;
                    }
                };
                Promise.resolve()
                    .then(() => {
                    if (pending.done)
                        throw new BackendFailure(Status.Cancelled, 'Cancelled before start');
                    return this.backend.execute(pending.request, context);
                })
                    .then(wire => {
                    if (pending.done)
                        return;
                    if (this.clock.now() >= pending.deadline) {
                        this.finish(pending, Status.Timeout);
                        return;
                    }
                    try {
                        checkWireResult(pending.request, wire);
                        let value;
                        if (wire.dispatch === 0 && wire.endpointStatus === 0 &&
                            pending.decode) {
                            try {
                                value = pending.decode(wire.payload);
                            }
                            catch {
                                this.finish(pending, Status.CodecError, wire);
                                return;
                            }
                        }
                        this.finish(pending, Status.Ok, wire, value);
                    }
                    catch {
                        this.finish(pending, Status.MalformedReply);
                    }
                }, error => {
                    if (!pending.done)
                        this.finish(pending, error instanceof BackendFailure
                            ? error.status
                            : Status.TransportError);
                });
            }
        }
        finally {
            this.pumping = false;
            this.armTimer();
        }
    }
    cancel(pending) {
        if (pending.done)
            return false;
        this.finish(pending, Status.Cancelled);
        return true;
    }
    cancelAll(reason) {
        for (const pending of this.pending.slice())
            this.finish(pending, pending === this.active ? reason : Status.Cancelled);
    }
    finish(pending, status, wire, value) {
        if (pending.done)
            return;
        // A checked reply proves an endpoint outcome. A failed transaction after
        // possible execution requires external session recovery, including reads.
        const uncertain = pending === this.active && pending.possibleExecution &&
            status !== Status.Ok && status !== Status.CodecError;
        if (uncertain) {
            this.recoveryEpoch = pending.request.session;
            this.admitted = false;
            if (pending.possibleExecution && pending.request.operation !== 'FieldRead')
                status = Status.OutcomeUnknown;
        }
        pending.done = true;
        this.pending.splice(this.pending.indexOf(pending), 1);
        this.payloadBytes -= pending.request.payload.length;
        if (this.active === pending)
            this.active = undefined;
        const request = pending.request;
        const result = this.makeResult(request.operation, request.session, request.requestId, status, wire, value);
        pending.resolve(result);
        pending.controller.abort();
        if (uncertain)
            for (const queued of this.pending.slice())
                this.finish(queued, Status.Cancelled);
        // Resolution queues Promise reactions before the next FIFO request starts.
        this.schedule();
        this.armTimer();
    }
    armTimer() {
        if (this.timer !== undefined) {
            this.clock.clearTimeout(this.timer);
            this.timer = undefined;
        }
        if (this.pending.length) {
            let deadline = Infinity;
            for (const pending of this.pending)
                deadline = Math.min(deadline, pending.deadline);
            this.timer = this.clock.setTimeout(() => {
                this.timer = undefined;
                this.process();
            }, Math.max(0, deadline - this.clock.now()));
        }
    }
}
// Complete frames only. The port owns framing and exposes received frames via receive().
export class EncodedBackend {
    send;
    waiting;
    constructor(send) {
        this.send = send;
    }
    static fromExchange(exchange) {
        const backend = new EncodedBackend(async (packet, context) => {
            const reply = await exchange(packet, context);
            if (!context.signal.aborted)
                backend.receive(reply);
        });
        return backend;
    }
    execute(request, context) {
        if (this.waiting)
            return Promise.reject(new BackendFailure(Status.Busy, 'Encoded backend already active'));
        const packet = new Uint8Array(32 + request.payload.length), data = new DataView(packet.buffer);
        data.setUint32(0, 0x31514554, true);
        data.setUint8(4, 1);
        data.setUint8(6, Operations[request.operation]);
        data.setUint32(8, request.session, true);
        data.setUint32(12, request.requestId, true);
        data.setUint32(16, request.endpointId, true);
        data.setUint32(20, request.payload.length, true);
        data.setUint32(24, request.responseBytes, true);
        packet.set(request.payload, 32);
        return new Promise((resolve, reject) => {
            const waiting = { request, context, resolve, reject };
            this.waiting = waiting;
            context.signal.addEventListener('abort', () => {
                if (this.waiting === waiting) {
                    this.waiting = undefined;
                    reject(new BackendFailure(Status.Cancelled, 'Aborted'));
                }
            }, { once: true });
            if (!context.handoff(true)) {
                if (this.waiting === waiting)
                    this.waiting = undefined;
                reject(new BackendFailure(Status.Cancelled, 'Handoff refused'));
                return;
            }
            try {
                Promise.resolve(this.send(packet, context)).catch(error => {
                    if (this.waiting === waiting) {
                        this.waiting = undefined;
                        reject(error);
                    }
                });
            }
            catch (error) {
                this.waiting = undefined;
                reject(error);
            }
        });
    }
    receive(input) {
        const waiting = this.waiting;
        if (!waiting || waiting.context.signal.aborted)
            return false;
        const malformed = () => {
            this.waiting = undefined;
            waiting.reject(new BackendFailure(Status.MalformedReply, 'Malformed TEQ1 reply'));
            return false;
        };
        let frame;
        try {
            frame = bytes(input);
        }
        catch {
            return malformed();
        }
        const request = waiting.request;
        if (frame.length < 20)
            return false;
        const data = new DataView(frame.buffer, frame.byteOffset, frame.byteLength);
        // Old or unrelated complete frames never complete the live request.
        if (data.getUint32(8, true) !== request.session ||
            data.getUint32(12, true) !== request.requestId ||
            data.getUint32(16, true) !== request.endpointId ||
            data.getUint8(6) !== Operations[request.operation])
            return false;
        if (frame.length < 32)
            return malformed();
        if (data.getUint32(0, true) !== 0x31514554 || data.getUint8(4) !== 1 ||
            data.getUint8(5) !== 1 || data.getUint32(20, true) !== frame.length - 32 ||
            data.getUint32(24, true) !== request.responseBytes || frame[29] !== 0 ||
            frame[30] !== 0 || frame[31] !== 0)
            return malformed();
        const result = {
            dispatch: frame[7],
            endpointStatus: frame[28],
            payload: frame.subarray(32)
        };
        try {
            checkWireResult(request, result);
        }
        catch {
            return malformed();
        }
        this.waiting = undefined;
        waiting.resolve({ ...result, payload: result.payload.slice() });
        return true;
    }
}
export class RpcError extends Error {
    clientStatus;
    resourceStatus;
    dispatchStatus;
    endpointStatus;
    constructor(message, details = {}) {
        super(message);
        this.name = 'RpcError';
        Object.assign(this, details);
    }
}
const codes = Operations;
const ensure = (condition, message) => {
    if (!condition)
        throw new RpcError(message);
};
const cursor = (transactionId, offset) => (BigInt(transactionId) << 32n) | BigInt(offset);
const integer = (value, minimum, maximum, name) => ensure(Number.isSafeInteger(value) && value >= minimum && value <= maximum, name);
const view = (bytes) => new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
function resourceRead(fileIndex, offset) {
    const packet = new Uint8Array(13), data = view(packet);
    data.setUint8(0, 3);
    data.setUint32(1, fileIndex, true);
    data.setBigUint64(5, offset, true);
    return packet;
}
function resourceWrite(fileIndex, offset, fragment, final) {
    const packet = new Uint8Array(16 + fragment.length), data = view(packet);
    data.setUint8(0, 4);
    data.setUint32(1, fileIndex, true);
    data.setBigUint64(5, offset, true);
    data.setUint8(13, final ? 1 : 0);
    data.setUint16(14, fragment.length, true);
    packet.set(fragment, 16);
    return packet;
}
function parseResource(reply, bytes) {
    ensure(reply instanceof Uint8Array, 'Exchange must return Uint8Array');
    if (reply.length === 1 && reply[0] === 7)
        throw new RpcError('Resource refused malformed request', { resourceStatus: 7, clientStatus: Status.TransportError });
    ensure(reply.length >= bytes, 'Truncated Resource reply');
    ensure(reply[0] <= 8, 'Unknown Resource status');
    if (reply[0] !== 0) {
        const data = view(reply);
        ensure(reply.length === bytes &&
            (bytes === 14 ? data.getUint32(9, true) === 0 && reply[13] === 0
                : data.getUint16(10, true) === 0 && reply[9] === 0), 'Malformed Resource refusal');
        throw new RpcError('Resource refused packet', { resourceStatus: reply[0], clientStatus: Status.TransportError });
    }
    return view(reply);
}
function readChunk(reply) {
    const data = parseResource(reply, 12), count = data.getUint16(10, true);
    ensure(reply.length === 12 + count, 'Resource READ length');
    ensure(reply[9] <= 1, 'Resource READ eof');
    return { next: data.getBigUint64(1, true), eof: reply[9] === 1, bytes: reply.subarray(12) };
}
// Return checked transport outcomes; this function never decodes value payloads.
async function rpcPacketExchange(exchange, operation, id, payload, options, preserveRefusals) {
    ensure(typeof exchange === 'function', 'Exchange callback required');
    ensure(Object.hasOwn(codes, operation), 'Explicit RPC operation required');
    ensure(payload instanceof Uint8Array, 'RPC payload requires Uint8Array');
    ensure(options !== null && typeof options === 'object', 'RPC options with transaction ID required');
    const { transactionId, fileIndex = 0, fragmentBytes = 48, maxPackets = 256, maxReplyBytes = 1048576, maxRequestBytes = 1048576 } = options;
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
    const send = async (packet) => {
        if (++packets > maxPackets)
            throw new RpcError('Packet budget exhausted', { clientStatus: Status.Timeout });
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
        const reply = await send(resourceWrite(fileIndex, offset ? cursor(transactionId, offset) : 0n, fragment, final));
        const data = parseResource(reply, 14);
        ensure(reply.length === 14 && reply[13] <= 1, 'Resource WRITE envelope');
        ensure(data.getUint32(9, true) === fragment.length, 'Resource WRITE consumed count');
        ensure(data.getBigUint64(1, true) === cursor(transactionId, offset + fragment.length), 'Resource WRITE cursor identity');
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
            data.getUint16(6, true) === 0, 'RPC reply magic/version');
        ensure(data.getUint32(8, true) === transactionId && data.getUint32(12, true) === id &&
            data.getUint8(20) === codes[operation], 'RPC reply transaction/endpoint/operation identity');
        const state = data.getUint8(21), payloadBytes = data.getUint32(16, true);
        ensure(data.getUint32(24, true) === request.length &&
            data.getUint32(28, true) === request.length, 'RPC accepted request byte counts');
        if (state === 4) {
            resultHeader = {
                payloadBytes,
                dispatchStatus: data.getUint8(22),
                endpointStatus: data.getUint8(23)
            };
            break;
        }
        ensure(state === 2 || state === 3, 'RPC must be Pending, Running or Ready');
        ensure(payloadBytes === 0 && data.getUint8(22) === 0 && data.getUint8(23) === 0 &&
            first.bytes.length === 32 && first.eof && first.next === 0n, 'RPC pending snapshot');
    }
    const total = 32 + resultHeader.payloadBytes;
    ensure(total <= maxReplyBytes, 'RPC result exceeds byte budget');
    ensure(first.bytes.length <= total &&
        first.next === cursor(transactionId, first.bytes.length) &&
        first.eof === (first.bytes.length === total), 'RPC first READ cursor/length/eof');
    const record = new Uint8Array(total);
    record.set(first.bytes);
    offset = first.bytes.length;
    while (offset < total) {
        const chunk = readChunk(await send(resourceRead(fileIndex, cursor(transactionId, offset))));
        ensure(chunk.bytes.length > 0 && chunk.bytes.length <= total - offset, 'RPC READ progress/bounds');
        ensure(chunk.next === cursor(transactionId, offset + chunk.bytes.length) &&
            chunk.eof === (offset + chunk.bytes.length === total), 'RPC continuation cursor/length/eof');
        record.set(chunk.bytes, offset);
        offset += chunk.bytes.length;
    }
    const { dispatchStatus, endpointStatus } = resultHeader;
    ensure([0, 4, 5, 6, 7, 8, 9].includes(dispatchStatus), 'Unknown dispatch status');
    if (dispatchStatus !== 0) {
        ensure(endpointStatus === 0 && resultHeader.payloadBytes === 0, 'Failed dispatch carried endpoint result');
        if (!preserveRefusals)
            throw new RpcError('Encoded dispatch refused operation', { dispatchStatus, endpointStatus });
    }
    const maximumStatus = { FieldRead: 0, FieldWrite: 5, Command: 7, Service: 4 }[operation];
    ensure(endpointStatus <= maximumStatus, 'Unknown endpoint status');
    const successful = endpointStatus === 0 || (operation === 'Command' && endpointStatus === 1);
    const hasValue = operation === 'FieldRead' || operation === 'Service';
    ensure((successful && hasValue) || resultHeader.payloadBytes === 0, 'Operation/status must have no payload');
    if (!successful && !preserveRefusals)
        throw new RpcError('Endpoint refused operation', { dispatchStatus, endpointStatus });
    return Object.freeze({
        operation,
        id,
        transactionId,
        dispatchStatus,
        endpointStatus,
        accepted: operation === 'Command' && endpointStatus === 1,
        payload: record.subarray(32)
    });
}
export function rpcExchange(exchange, operation, id, payload, options) {
    return rpcPacketExchange(exchange, operation, id, payload, options, false);
}
export async function rpcInvoke(exchange, binding, value, options) {
    ensure(binding !== null && typeof binding === 'object', 'Generated operation metadata required');
    const payload = binding.operation === 'FieldRead' ? new Uint8Array(0) : binding.encode(value);
    const outcome = await rpcExchange(exchange, binding.operation, binding.id, payload, options);
    if (outcome.dispatchStatus !== 0 || (outcome.endpointStatus !== 0 && !outcome.accepted))
        throw new RpcError('Endpoint refused operation', outcome);
    const decoded = binding.operation === 'FieldRead' ? binding.decode(outcome.payload)
        : binding.operation === 'Service' ? binding.decodeResponse(outcome.payload)
            : undefined;
    return Object.freeze({ ...outcome, value: decoded });
}
// RPC retries repeat only the same active Resource packet/transaction. An uncertain
// terminal outcome is quarantined by DeviceClient; a fresh request is never a retry.
export class RpcBackend {
    exchange;
    options;
    executing;
    constructor(exchange, options = {}) {
        this.exchange = exchange;
        this.options = Object.freeze({ ...options });
        const ack = options.ackTimeoutMs ?? 1000, poll = options.pollIntervalMs ?? 10, retries = options.maxRetries ?? 2;
        if (!Number.isFinite(ack) || ack <= 0 || ack > 0x7fffffff || !Number.isFinite(poll) ||
            poll < 0 || poll > 0x7fffffff || !Number.isSafeInteger(retries) || retries < 0 ||
            retries > 1000)
            throw new RangeError('RPC retry/poll policy');
        integer(options.fileIndex ?? 0, 0, 0xffffffff, 'File index');
        integer(options.fragmentBytes ?? 48, 24, 65535, 'Fragment capacity');
        integer(options.maxPackets ?? 256, 1, 1000000, 'Packet budget');
        integer(options.maxRequestBytes ?? 1048576, 24, 0xffffffff, 'Request capacity');
        integer(options.maxReplyBytes ?? 1048576, 32, 0xffffffff, 'Reply capacity');
    }
    async execute(request, context) {
        if (this.executing)
            throw new BackendFailure(Status.Busy, 'RPC backend already active');
        if (request.payload.length > (this.options.maxRequestBytes ?? 1048576) - 24 ||
            request.responseBytes > (this.options.maxReplyBytes ?? 1048576) - 32)
            throw new BackendFailure(Status.BufferTooSmall, 'RPC request/result capacity');
        const execution = {};
        this.executing = execution;
        const release = () => {
            if (this.executing === execution)
                this.executing = undefined;
        };
        context.signal.addEventListener('abort', release, { once: true });
        try {
            return await this.run(request, context);
        }
        finally {
            context.signal.removeEventListener('abort', release);
            release();
        }
    }
    async run(request, context) {
        const exchange = async (packet) => {
            if (context.signal.aborted)
                throw new BackendFailure(Status.Cancelled, 'RPC cancelled');
            const write = packet[0] === 4, final = write && packet[13] === 1;
            if (!write && view(packet).getBigUint64(5, true) === 0n &&
                (this.options.pollIntervalMs ?? 10) > 0) {
                await this.delay(this.options.pollIntervalMs ?? 10, context);
            }
            const maximum = this.options.maxRetries ?? 2;
            for (let attempt = 0;; ++attempt) {
                try {
                    const reply = await this.attempt(packet, context, final);
                    try {
                        return bytes(reply).slice();
                    }
                    catch {
                        throw new BackendFailure(Status.MalformedReply, 'Resource reply requires bytes');
                    }
                }
                catch (error) {
                    if (context.signal.aborted)
                        throw new BackendFailure(Status.Cancelled, 'RPC cancelled');
                    if (!(error instanceof BackendFailure) || error.status !== Status.Timeout ||
                        attempt >= maximum)
                        throw error;
                }
            }
        };
        let outcome;
        try {
            outcome = await rpcPacketExchange(exchange, request.operation, request.endpointId, request.payload, { ...this.options, transactionId: request.requestId }, true);
        }
        catch (error) {
            if (error instanceof BackendFailure)
                throw error;
            if (error instanceof RpcError)
                throw new BackendFailure(error.clientStatus ?? Status.MalformedReply, error.message);
            throw error;
        }
        return {
            dispatch: outcome.dispatchStatus,
            endpointStatus: outcome.endpointStatus,
            payload: outcome.payload
        };
    }
    delay(milliseconds, context) {
        return new Promise((resolve, reject) => {
            if (context.signal.aborted) {
                reject(new BackendFailure(Status.Cancelled, 'RPC cancelled'));
                return;
            }
            const abort = () => {
                context.clock.clearTimeout(timer);
                reject(new BackendFailure(Status.Cancelled, 'RPC cancelled'));
            };
            const timer = context.clock.setTimeout(() => {
                context.signal.removeEventListener('abort', abort);
                resolve();
            }, milliseconds);
            context.signal.addEventListener('abort', abort, { once: true });
        });
    }
    attempt(packet, context, mayExecute) {
        return new Promise((resolve, reject) => {
            const ownedPacket = packet.slice();
            const controller = new AbortController();
            let done = false;
            const settle = (error, reply, failed = true) => {
                if (done)
                    return;
                done = true;
                context.clock.clearTimeout(timer);
                context.signal.removeEventListener('abort', abort);
                controller.abort();
                if (failed)
                    reject(error);
                else
                    resolve(reply);
            };
            const abort = () => settle(new BackendFailure(Status.Cancelled, 'RPC cancelled'));
            const timer = context.clock.setTimeout(() => settle(new BackendFailure(Status.Timeout, 'RPC ACK timeout')), this.options.ackTimeoutMs ?? 1000);
            context.signal.addEventListener('abort', abort, { once: true });
            if (context.signal.aborted) {
                abort();
                return;
            }
            if (!context.handoff(mayExecute)) {
                settle(new BackendFailure(Status.Cancelled, 'RPC handoff refused'));
                return;
            }
            // Port owns this copy until its Promise settles, including timed-out late replies.
            try {
                Promise
                    .resolve(this.exchange(ownedPacket, {
                    session: context.session,
                    requestId: context.requestId,
                    signal: controller.signal
                }))
                    .then(reply => settle(undefined, reply, false), error => settle(error));
            }
            catch (error) {
                settle(error);
            }
        });
    }
}
