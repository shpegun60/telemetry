/* One-owner asynchronous FIFO client. Authors: Ruslan Kovtun (shpegun60), codexAi. MIT. */
export const Status = Object.freeze({
	Ok : 0,
	QueueFull : 1,
	PayloadFull : 2,
	NotAdmitted : 3,
	ReadOnly : 4,
	Closed : 5,
	Cancelled : 6,
	Timeout : 7,
	SessionLost : 8,
	OutcomeUnknown : 9,
	MalformedReply : 10,
	TransportError : 11,
	CodecError : 12,
	BufferTooSmall : 13,
	InvalidArgument : 14,
	Busy : 15,
	RecoveryRequired : 16,
	IdsExhausted : 17,
} as const);
export type ClientStatus = typeof Status[keyof typeof Status];
export type Operation = 'FieldWrite'|'Command'|'Service'|'FieldRead';
export const Operations =
    Object.freeze({FieldWrite : 1, Command : 2, Service : 3, FieldRead : 4} as const);
export type ByteInput = ArrayBuffer|ArrayBufferView;
export interface ReadBinding<T> {
	readonly fingerprint: bigint;
	readonly operation: 'FieldRead';
	readonly id: number;
	readonly requestBytes: number;
	readonly responseBytes: number;
	decode(input: ByteInput): T;
}
export interface WriteBinding<T> {
	readonly fingerprint: bigint;
	readonly operation: 'FieldWrite';
	readonly id: number;
	readonly requestBytes: number;
	readonly responseBytes: number;
	encode(value: T): Uint8Array;
}
export interface CommandBinding<T> {
	readonly fingerprint: bigint;
	readonly operation: 'Command';
	readonly id: number;
	readonly requestBytes: number;
	readonly responseBytes: number;
	encode(value: T): Uint8Array;
}
export interface ServiceBinding<Q, R> {
	readonly fingerprint: bigint;
	readonly operation: 'Service';
	readonly id: number;
	readonly requestBytes: number;
	readonly responseBytes: number;
	encode(value: Q): Uint8Array;
	decodeResponse(input: ByteInput): R;
}
export interface Field<T> {
	readonly read: ReadBinding<T>;
	readonly write?: WriteBinding<T>;
}
export interface Result<T = undefined> {
	readonly status: ClientStatus;
	readonly operation: Operation;
	readonly session: number;
	readonly requestId: number;
	readonly dispatch: number;
	readonly endpointStatus: number;
	readonly succeeded: boolean;
	readonly accepted: boolean;
	readonly executed: boolean;
	readonly payload: Uint8Array;
	readonly value: T|undefined;
}
// Normal refusals and operational failures resolve a Result; they do not reject.
export interface RequestPromise<T> extends Promise<Result<T>> {
	readonly requestId: number;
	readonly session: number;
	cancel(): boolean;
}
export interface Clock {
	now(): number;
	setTimeout(callback: () => void, milliseconds: number): unknown;
	clearTimeout(handle: unknown): void;
}
const systemClock: Clock = {
	now : () => performance.now(),
	setTimeout : (callback, milliseconds) => setTimeout(callback, milliseconds),
	clearTimeout : handle => clearTimeout(handle as ReturnType<typeof setTimeout>),
};
const bytes = (input: ByteInput): Uint8Array =>
    input instanceof ArrayBuffer ? new Uint8Array(input)
                                 : new Uint8Array(input.buffer, input.byteOffset, input.byteLength);
const u32 = (n: number): boolean => Number.isInteger(n) && n >= 0 && n <= 0xffffffff;
const validOperation = (operation: string): operation is Operation =>
    Object.hasOwn(Operations, operation);
export interface WireRequest {
	readonly operation: Operation;
	readonly endpointId: number;
	readonly session: number;
	readonly requestId: number;
	readonly payload: Uint8Array;
	readonly responseBytes: number;
}
export interface WireResult {
	readonly dispatch: number;
	readonly endpointStatus: number;
	readonly payload: Uint8Array;
	// A checked outcome can precede physical release. Resolve/reject both mean quiescent,
	// without changing the known endpoint outcome. Backend cleanup must precede settlement.
	readonly released?: Promise<void>;
}
export interface ExchangeContext {
	readonly session: number;
	readonly requestId: number;
	readonly signal: AbortSignal;
}
export type Exchange = (packet: Uint8Array, context?: ExchangeContext) =>
    ByteInput|Promise<ByteInput>;
export interface BackendContext extends ExchangeContext {
	readonly clock: Clock;
	// Immediately before the first possible external transfer. False means do not send.
	handoff(mayExecute: boolean): boolean;
}
export interface Backend {
	// Settlement releases physical I/O, unless a checked result supplies released.
	execute(request: WireRequest, context: BackendContext): Promise<WireResult>;
	// Application asserts this aborted session's transport has actually closed/drained.
	confirmSessionClosed?(generation: number): boolean;
}

class BackendFailure extends Error {
	constructor(readonly status: ClientStatus, message: string)
	{
		super(message);
	}
}

function checkWireResult(request: WireRequest, result: WireResult): void
{
	const {dispatch, endpointStatus, payload} = result;
	if (![0, 4, 5, 6, 7, 8, 9].includes(dispatch) || !u32(endpointStatus) ||
	    (dispatch !== 0 && (endpointStatus !== 0 || payload.length !== 0)))
		throw new BackendFailure(Status.MalformedReply, 'Invalid dispatch/status envelope');
	if (dispatch !== 0)
		return;
	const maximum = {FieldRead : 0, FieldWrite : 5, Command : 7, Service : 4}[request.operation];
	const expected = endpointStatus === 0 &&
	                         (request.operation === 'FieldRead' || request.operation === 'Service')
	                     ? request.responseBytes
	                     : 0;
	if (endpointStatus > maximum || payload.length !== expected)
		throw new BackendFailure(Status.MalformedReply,
		                         'Invalid endpoint status or payload length');
}

export interface ClientOptions {
	readonly backend: Backend;
	readonly matchesHeader: (header: ByteInput) => boolean;
	readonly maxPending?: number;
	readonly maxPayloadBytes?: number;
	readonly timeoutMs?: number;
	readonly clock?: Clock;
}
interface Pending {
	request: WireRequest;
	deadline: number;
	resolve: (result: Result<unknown>) => void;
	decode?: (input: ByteInput) => unknown;
	controller: AbortController;
	possibleExecution: boolean;
	done: boolean;
}

export class DeviceClient {
	private readonly backend: Backend;
	private readonly identity: (header: ByteInput) => boolean;
	private readonly clock: Clock;
	private readonly limit: number;
	private readonly byteLimit: number;
	private readonly timeout: number;
	private pending: Pending[] = [];
	private active?: Pending;
	private invocation?: Pending;
	private epoch = 0;
	private fingerprint = 0n;
	private lastEpoch = 0;
	private nextId = 1;
	private payloadBytes = 0;
	private recoveryEpoch = 0;
	private stopped = false;
	private admitted = false;
	private timer?: unknown;
	private scheduled = false;
	private pumping = false;
	private confirming = false;

	constructor(options: ClientOptions)
	{
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

	get session(): number
	{
		return this.epoch;
	}

	get ready(): boolean
	{
		return this.admitted && !this.stopped && this.recoveryEpoch === 0;
	}

	get recoveryRequired(): boolean
	{
		return this.recoveryEpoch !== 0;
	}

	get recoveryGeneration(): number
	{
		return this.recoveryEpoch;
	}

	get pendingCount(): number
	{
		return this.pending.length;
	}

	get snapshotBytes(): number
	{
		return this.payloadBytes;
	}

	get drained(): boolean
	{
		return this.stopped && this.pending.length === 0 && !this.invocation;
	}

	// Caller opens a fresh physical session and supplies its admitted descriptor header.
	openSession(generation: number, header: ByteInput): boolean
	{
		if (this.stopped || this.confirming || this.recoveryRequired || this.pending.length ||
		    this.invocation || !u32(generation) || generation === 0 || generation <= this.lastEpoch)
			return false;
		let matches = false;
		let declared: Uint8Array;
		try {
			declared = bytes(header).slice();
			matches = declared.length === 64 && this.identity(declared);
		} catch {
			return false;
		}
		// The identity callback may synchronously close, admit or enqueue on this client.
		if (!matches || this.stopped || this.confirming || this.recoveryRequired ||
		    this.pending.length || this.invocation || generation <= this.lastEpoch)
			return false;
		this.fingerprint = new DataView(declared!.buffer).getBigUint64(16, true);
		this.epoch = this.lastEpoch = generation;
		this.nextId = 1;
		this.admitted = true;
		return true;
	}

	// Assertion by the application: old transport and remote retained session are closed/drained.
	// Re-admitting a header alone never makes an uncertain old session safe.
	confirmSessionClosed(generation: number): boolean
	{
		if (this.confirming || this.admitted || !this.epoch || generation !== this.epoch ||
		    this.pending.length ||
		    (this.invocation && (this.invocation.request.session !== generation ||
		                         !this.invocation.controller.signal.aborted)))
			return false;
		this.confirming = true;
		try {
			const confirm = this.backend.confirmSessionClosed;
			if (confirm) {
				const confirmed: unknown = confirm.call(this.backend, generation);
				if (confirmed !== true) {
					// Refuse async/truthy hooks; observe a rejected async hook without trusting it.
					Promise.resolve(confirmed).catch(() => {
					                                 });
					return false;
				}
			}
		} catch {
			return false;
		} finally {
			this.confirming = false;
		}
		this.invocation = undefined;
		this.recoveryEpoch = 0;
		return true;
	}

	resetSession(): void
	{
		this.admitted = false;
		this.cancelAll(Status.SessionLost);
	}

	close(): void
	{
		this.stopped = true;
		this.admitted = false;
		this.cancelAll(Status.Cancelled);
	}

	read<T>(field: Field<T>|ReadBinding<T>, timeoutMs?: number): RequestPromise<T>
	{
		const binding = 'read' in field ? field.read : field;
		return this.enqueue(binding, undefined, binding.decode, timeoutMs);
	}

	write<T>(field: Field<T>|WriteBinding<T>, value: T,
	         timeoutMs?: number): RequestPromise<undefined>
	{
		const binding = 'read' in field ? field.write : field;
		if (!binding)
			return this.refused('FieldWrite', Status.ReadOnly);
		return this.enqueue(binding, value, undefined, timeoutMs);
	}

	command<T>(binding: CommandBinding<T>, value: T, timeoutMs?: number): RequestPromise<undefined>
	{
		return this.enqueue(binding, value, undefined, timeoutMs);
	}

	service<Q, R>(binding: ServiceBinding<Q, R>, value: Q, timeoutMs?: number): RequestPromise<R>
	{
		return this.enqueue(binding, value, binding.decodeResponse, timeoutMs);
	}

	private refused<T>(operation: Operation, status: ClientStatus): RequestPromise<T>
	{
		const result = this.makeResult(operation, this.epoch, 0, status);
		return Object.assign(Promise.resolve(result as Result<T>), {
			requestId : 0,
			session : this.epoch,
			cancel : () => false,
		});
	}

	private enqueue<T>(binding: {
		fingerprint: bigint; operation : Operation; id : number; requestBytes : number;
		responseBytes : number;
		encode?: (value: never) => Uint8Array;
	},
	                   value: unknown, decode?: (input: ByteInput) => T,
	                   timeoutMs?: number): RequestPromise<T>
	{
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
		let payload: Uint8Array;
		try {
			payload = operation === 'FieldRead' ? new Uint8Array(0)
			                                    : binding.encode!(value as never).slice();
			if (!(payload instanceof Uint8Array) || payload.length !== binding.requestBytes)
				return this.refused(operation, Status.CodecError);
		} catch {
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
		const request: WireRequest = Object.freeze({
			operation,
			endpointId : binding.id,
			session : this.epoch,
			requestId : this.nextId++,
			payload,
			responseBytes : binding.responseBytes
		});
		let resolve!: (result: Result<unknown>) => void;
		const promise = new Promise<Result<T>>(done => {
			resolve = done as (result: Result<unknown>) => void;
		});
		const pending: Pending = {
			request,
			resolve,
			decode,
			deadline : now + timeout,
			controller : new AbortController(),
			possibleExecution : false,
			done : false
		};
		this.pending.push(pending);
		this.payloadBytes += payload.length;
		this.schedule();
		this.armTimer();
		return Object.assign(promise, {
			requestId : request.requestId,
			session : request.session,
			cancel : () => this.cancel(pending)
		});
	}

	private makeResult(operation: Operation, session: number, requestId: number,
	                   status: ClientStatus, wire?: WireResult, value?: unknown): Result<unknown>
	{
		const dispatch = wire?.dispatch ?? 0, endpointStatus = wire?.endpointStatus ?? 0;
		const succeeded =
		    status === Status.Ok && dispatch === 0 &&
		    (endpointStatus === 0 || (operation === 'Command' && endpointStatus === 1));
		return Object.freeze({
			status,
			operation,
			session,
			requestId,
			dispatch,
			endpointStatus,
			succeeded,
			accepted : succeeded && operation === 'Command' && endpointStatus === 1,
			executed : succeeded && operation === 'Command' && endpointStatus === 0,
			payload : wire?.payload.slice() ?? new Uint8Array(0),
			value
		});
	}

	private schedule(): void
	{
		if (this.scheduled)
			return;
		this.scheduled = true;
		queueMicrotask(() => {
			this.scheduled = false;
			this.process();
		});
	}

	process(): void
	{
		if (this.pumping)
			return;
		this.pumping = true;
		try {
			const now = this.clock.now();
			for (const pending of this.pending.slice()) {
				if (!pending.done && now >= pending.deadline)
					this.finish(pending, Status.Timeout);
			}
			if (!this.active && !this.invocation && this.ready && this.pending.length) {
				const pending = this.invocation = this.active = this.pending[0];
				const context: BackendContext = {
					session : pending.request.session,
					requestId : pending.request.requestId,
					signal : pending.controller.signal,
					clock : this.clock,
					handoff : mayExecute => {
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
				const release = () => {
					if (this.invocation === pending)
						this.invocation = undefined;
				};
				Promise.resolve()
				    .then(() => {
					    if (pending.done)
						    throw new BackendFailure(Status.Cancelled, 'Cancelled before start');
					    return this.backend.execute(pending.request, context);
				    })
				    .then(
				        wire => {
					        const released = wire?.released;
					        if (released) {
						        const quiescent = () => {
							        release();
							        this.schedule();
						        };
						        Promise.resolve(released).then(quiescent, quiescent);
					        } else {
						        release();
					        }
					        if (pending.done) {
						        if (!released)
							        this.schedule();
						        return;
					        }
					        if (this.clock.now() >= pending.deadline) {
						        this.finish(pending, Status.Timeout);
						        return;
					        }
					        try {
						        checkWireResult(pending.request, wire);
						        let value: unknown;
						        if (wire.dispatch === 0 && wire.endpointStatus === 0 &&
						            pending.decode) {
							        try {
								        value = pending.decode(wire.payload);
							        } catch {
								        this.finish(pending, Status.CodecError, wire);
								        return;
							        }
						        }
						        this.finish(pending, Status.Ok, wire, value);
					        } catch {
						        this.finish(pending, Status.MalformedReply);
					        }
				        },
				        error => {
					        release();
					        if (!pending.done)
						        this.finish(pending, error instanceof BackendFailure
						                                 ? error.status
						                                 : Status.TransportError);
					        else
						        this.schedule();
				        });
			}
		} finally {
			this.pumping = false;
			this.armTimer();
		}
	}

	private cancel(pending: Pending): boolean
	{
		if (pending.done)
			return false;
		this.finish(pending, Status.Cancelled);
		return true;
	}

	private cancelAll(reason: ClientStatus): void
	{
		for (const pending of this.pending.slice())
			this.finish(pending, pending === this.active ? reason : Status.Cancelled);
	}

	private finish(pending: Pending, status: ClientStatus, wire?: WireResult, value?: unknown): void
	{
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
		// Abort listeners may finish recovery and enqueue a newer session synchronously.
		const abandoned = uncertain ? this.pending.slice() : [];
		const request = pending.request;
		const result = this.makeResult(request.operation, request.session, request.requestId,
		                               status, wire, value);
		pending.resolve(result);
		pending.controller.abort();
		for (const queued of abandoned)
			this.finish(queued, Status.Cancelled);
		// Resolution queues Promise reactions before the next FIFO request starts.
		this.schedule();
		this.armTimer();
	}

	private armTimer(): void
	{
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

export type EncodedSend = (packet: Uint8Array, context: ExchangeContext) => void|Promise<void>;

// Complete frames only. The port owns framing and exposes received frames via receive().
export class EncodedBackend implements Backend {
	private execution?: {context: BackendContext};
	private waiting?: {
		request: WireRequest; context : BackendContext; resolve : (value: WireResult) => void;
		reject : (error: unknown) => void
	};

	constructor(private readonly send: EncodedSend)
	{}

	static fromExchange(exchange: Exchange): EncodedBackend
	{
		const backend = new EncodedBackend(async (packet, context) => {
			const reply = await exchange(packet, context);
			if (!context.signal.aborted)
				backend.receive(reply);
		});
		return backend;
	}

	confirmSessionClosed(generation: number): boolean
	{
		if (this.execution && (this.execution.context.session !== generation ||
		                       !this.execution.context.signal.aborted))
			return false;
		this.execution = undefined;
		return true;
	}

	async execute(request: WireRequest, context: BackendContext): Promise<WireResult>
	{
		if (this.execution)
			throw new BackendFailure(Status.Busy, 'Encoded backend already active');
		const packet = new Uint8Array(32 + request.payload.length),
		      data = new DataView(packet.buffer);
		data.setUint32(0, 0x31514554, true);
		data.setUint8(4, 1);
		data.setUint8(6, Operations[request.operation]);
		data.setUint32(8, request.session, true);
		data.setUint32(12, request.requestId, true);
		data.setUint32(16, request.endpointId, true);
		data.setUint32(20, request.payload.length, true);
		data.setUint32(24, request.responseBytes, true);
		packet.set(request.payload, 32);
		const execution = this.execution = {context};
		let transfer: Promise<unknown> = Promise.resolve();
		const response = new Promise<WireResult>((resolve, reject) => {
			const waiting = {request, context, resolve, reject};
			this.waiting = waiting;
			context.signal.addEventListener('abort', () => {
				if (this.waiting === waiting) {
					this.waiting = undefined;
					reject(new BackendFailure(Status.Cancelled, 'Aborted'));
				}
			}, {once : true});
			if (!context.handoff(true)) {
				if (this.waiting === waiting)
					this.waiting = undefined;
				reject(new BackendFailure(Status.Cancelled, 'Handoff refused'));
				return;
			}
			try {
				transfer = Promise.resolve(this.send(packet, context));
				transfer.catch(error => {
					if (this.waiting === waiting) {
						this.waiting = undefined;
						reject(error);
					}
				});
			} catch (error) {
				if (this.waiting === waiting)
					this.waiting = undefined;
				reject(error);
			}
		});
		const release = () => {
			if (this.execution === execution)
				this.execution = undefined;
		};
		try {
			const wire = await response;
			// Preserve a checked outcome even while the send still owns physical I/O.
			// Cleanup must precede the client's release handler and next FIFO invocation.
			return {...wire, released : transfer.then(release, release)};
		} catch (error) {
			// Abort requests cancellation; only settlement proves the port released its bytes/I/O.
			await transfer.then(release, release);
			throw error;
		}
	}

	receive(input: ByteInput): boolean
	{
		const waiting = this.waiting;
		if (!waiting || waiting.context.signal.aborted)
			return false;
		const malformed = () => {
			this.waiting = undefined;
			waiting.reject(new BackendFailure(Status.MalformedReply, 'Malformed TEQ1 reply'));
			return false;
		};
		let frame: Uint8Array;
		try {
			frame = bytes(input);
		} catch {
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
			dispatch : frame[7],
			endpointStatus : frame[28],
			payload : frame.subarray(32)
		};
		try {
			checkWireResult(request, result);
		} catch {
			return malformed();
		}
		this.waiting = undefined;
		waiting.resolve({...result, payload : result.payload.slice()});
		return true;
	}
}

// Resource/RPC packet algorithm shared with the optional application adapter.
export interface RpcOptions {
	transactionId: number;
	fileIndex?: number;
	fragmentBytes?: number;
	maxPackets?: number;
	maxReplyBytes?: number;
	maxRequestBytes?: number;
}
export interface RpcOutcome {
	readonly operation: Operation;
	readonly id: number;
	readonly transactionId: number;
	readonly dispatchStatus: number;
	readonly endpointStatus: number;
	readonly accepted: boolean;
	readonly payload: Uint8Array;
}

export class RpcError extends Error {
	readonly clientStatus?: ClientStatus;
	readonly resourceStatus?: number;
	readonly dispatchStatus?: number;
	readonly endpointStatus?: number;

	constructor(message: string, details: object = {})
	{
		super(message);
		this.name = 'RpcError';
		Object.assign(this, details);
	}
}

const codes = Operations;
const ensure: (condition: unknown, message: string) => asserts condition = (condition, message) => {
	if (!condition)
		throw new RpcError(message);
};
const cursor = (transactionId: number, offset: number) =>
    (BigInt(transactionId) << 32n) | BigInt(offset);
const integer = (value: number, minimum: number, maximum: number, name: string) =>
    ensure(Number.isSafeInteger(value) && value >= minimum && value <= maximum, name);
const view = (bytes: Uint8Array) => new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);

function resourceRead(fileIndex: number, offset: bigint)
{
	const packet = new Uint8Array(13), data = view(packet);
	data.setUint8(0, 3);
	data.setUint32(1, fileIndex, true);
	data.setBigUint64(5, offset, true);
	return packet;
}

function resourceWrite(fileIndex: number, offset: bigint, fragment: Uint8Array, final: boolean)
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

function parseResource(reply: Uint8Array, bytes: number)
{
	ensure(reply instanceof Uint8Array, 'Exchange must return Uint8Array');
	if (reply.length === 1 && reply[0] === 7)
		throw new RpcError('Resource refused malformed request',
		                   {resourceStatus : 7, clientStatus : Status.TransportError});
	ensure(reply.length >= bytes, 'Truncated Resource reply');
	ensure(reply[0] <= 8, 'Unknown Resource status');
	if (reply[0] !== 0) {
		const data = view(reply);
		ensure(reply.length === bytes &&
		           (bytes === 14 ? data.getUint32(9, true) === 0 && reply[13] === 0
		                         : data.getUint16(10, true) === 0 && reply[9] === 0),
		       'Malformed Resource refusal');
		throw new RpcError('Resource refused packet',
		                   {resourceStatus : reply[0], clientStatus : Status.TransportError});
	}
	return view(reply);
}

function readChunk(reply: Uint8Array)
{
	const data = parseResource(reply, 12), count = data.getUint16(10, true);
	ensure(reply.length === 12 + count, 'Resource READ length');
	ensure(reply[9] <= 1, 'Resource READ eof');
	return {next : data.getBigUint64(1, true), eof : reply[9] === 1, bytes : reply.subarray(12)};
}

// Return checked transport outcomes; this function never decodes value payloads.
async function rpcPacketExchange(exchange: (packet: Uint8Array) => Uint8Array | Promise<Uint8Array>,
                                 operation: Operation, id: number, payload: Uint8Array,
                                 options: RpcOptions,
                                 preserveRefusals: boolean): Promise<RpcOutcome>
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
	const send = async (packet: Uint8Array) => {
		if (++packets > maxPackets)
			throw new RpcError('Packet budget exhausted', {clientStatus : Status.Timeout});
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
	let first: ReturnType<typeof readChunk>,
	    resultHeader: {payloadBytes: number; dispatchStatus : number; endpointStatus : number;};
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
		if (!preserveRefusals)
			throw new RpcError('Encoded dispatch refused operation',
			                   {dispatchStatus, endpointStatus});
	}
	const maximumStatus = {FieldRead : 0, FieldWrite : 5, Command : 7, Service : 4}[operation];
	ensure(endpointStatus <= maximumStatus, 'Unknown endpoint status');
	const successful = endpointStatus === 0 || (operation === 'Command' && endpointStatus === 1);
	const hasValue = operation === 'FieldRead' || operation === 'Service';
	ensure((successful && hasValue) || resultHeader.payloadBytes === 0,
	       'Operation/status must have no payload');
	if (!successful && !preserveRefusals)
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

export function rpcExchange(exchange: (packet: Uint8Array) => Uint8Array | Promise<Uint8Array>,
                            operation: Operation, id: number, payload: Uint8Array,
                            options: RpcOptions): Promise<RpcOutcome>
{
	return rpcPacketExchange(exchange, operation, id, payload, options, false);
}

// Generated metadata supplies the operation and codec. Status checks above run
// before any decode, including zero-byte Empty/Void service response codecs.
export function rpcInvoke<R>(exchange: (packet: Uint8Array) => Uint8Array | Promise<Uint8Array>,
                             binding: Pick<ReadBinding<R>, 'operation'|'id'|'decode'>,
                             value: undefined,
                             options: RpcOptions): Promise<RpcOutcome&{readonly value : R}>;
export function rpcInvoke<Q, R>(
    exchange: (packet: Uint8Array) => Uint8Array | Promise<Uint8Array>,
    binding: Pick<ServiceBinding<Q, R>, 'operation'|'id'|'encode'|'decodeResponse'>, value: Q,
    options: RpcOptions): Promise<RpcOutcome&{readonly value : R}>;
export function rpcInvoke<Q>(
    exchange: (packet: Uint8Array) => Uint8Array | Promise<Uint8Array>,
    binding: Pick<WriteBinding<Q>|CommandBinding<Q>, 'operation'|'id'|'encode'>, value: Q,
    options: RpcOptions): Promise<RpcOutcome&{readonly value : undefined}>;

export async function rpcInvoke<Q, R>(
    exchange: (packet: Uint8Array) => Uint8Array | Promise<Uint8Array>, binding: {
	    readonly operation: Operation; readonly id : number; encode ? (value: Q) : Uint8Array;
	    decode ? (input: ByteInput) : R;
	    decodeResponse ? (input: ByteInput) : R
    },
    value: Q, options: RpcOptions): Promise<RpcOutcome&{readonly value : R | undefined}>
{
	ensure(binding !== null && typeof binding === 'object',
	       'Generated operation metadata required');
	const payload = binding.operation === 'FieldRead' ? new Uint8Array(0) : binding.encode!(value);
	const outcome = await rpcExchange(exchange, binding.operation, binding.id, payload, options);
	if (outcome.dispatchStatus !== 0 || (outcome.endpointStatus !== 0 && !outcome.accepted))
		throw new RpcError('Endpoint refused operation', outcome);
	const decoded = binding.operation === 'FieldRead' ? binding.decode!(outcome.payload)
	                : binding.operation === 'Service' ? binding.decodeResponse!(outcome.payload)
	                                                  : undefined;
	return Object.freeze({...outcome, value : decoded});
}

export interface RpcBackendOptions extends Omit<RpcOptions, 'transactionId'> {
	readonly ackTimeoutMs?: number;
	readonly pollIntervalMs?: number;
	readonly maxRetries?: number;
}

// RPC retries repeat only the same active Resource packet/transaction. An uncertain
// terminal outcome is quarantined by DeviceClient; a fresh request is never a retry.
export class RpcBackend implements Backend {
	private readonly options: RpcBackendOptions;
	private executing?: {context: BackendContext};

	constructor(private readonly exchange: Exchange, options: RpcBackendOptions = {})
	{
		this.options = Object.freeze({...options});
		const ack = options.ackTimeoutMs ?? 1000, poll = options.pollIntervalMs ?? 10,
		      retries = options.maxRetries ?? 2;
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

	confirmSessionClosed(generation: number): boolean
	{
		if (this.executing && (this.executing.context.session !== generation ||
		                       !this.executing.context.signal.aborted))
			return false;
		this.executing = undefined;
		return true;
	}

	async execute(request: WireRequest, context: BackendContext): Promise<WireResult>
	{
		if (this.executing)
			throw new BackendFailure(Status.Busy, 'RPC backend already active');
		if (request.payload.length > (this.options.maxRequestBytes ?? 1048576) - 24 ||
		    request.responseBytes > (this.options.maxReplyBytes ?? 1048576) - 32)
			throw new BackendFailure(Status.BufferTooSmall, 'RPC request/result capacity');
		const execution = {context};
		this.executing = execution;
		const release = () => {
			if (this.executing === execution)
				this.executing = undefined;
		};
		try {
			return await this.run(request, context);
		} finally {
			release();
		}
	}

	private async run(request: WireRequest, context: BackendContext): Promise<WireResult>
	{
		const exchange = async(packet: Uint8Array): Promise<Uint8Array> => {
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
					} catch {
						throw new BackendFailure(Status.MalformedReply,
						                         'Resource reply requires bytes');
					}
				} catch (error) {
					if (context.signal.aborted)
						throw new BackendFailure(Status.Cancelled, 'RPC cancelled');
					if (!(error instanceof BackendFailure) || error.status !== Status.Timeout ||
					    attempt >= maximum)
						throw error;
				}
			}
		};
		let outcome: RpcOutcome;
		try {
			outcome = await rpcPacketExchange(
			    exchange, request.operation, request.endpointId, request.payload,
			    {...this.options, transactionId : request.requestId}, true);
		} catch (error) {
			if (error instanceof BackendFailure)
				throw error;
			if (error instanceof RpcError)
				throw new BackendFailure(error.clientStatus ?? Status.MalformedReply,
				                         error.message);
			throw error;
		}
		return {
			dispatch : outcome.dispatchStatus,
			endpointStatus : outcome.endpointStatus,
			payload : outcome.payload
		};
	}

	private delay(milliseconds: number, context: BackendContext): Promise<void>
	{
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
			context.signal.addEventListener('abort', abort, {once : true});
		});
	}

	private attempt(packet: Uint8Array, context: BackendContext,
	                mayExecute: boolean): Promise<ByteInput>
	{
		return new Promise((resolve, reject) => {
			const ownedPacket = packet.slice();
			const controller = new AbortController();
			let done = false;
			let interrupted: {error: unknown}|undefined;
			const settle = (error?: unknown, reply?: ByteInput, failed = true) => {
				if (done)
					return;
				done = true;
				context.clock.clearTimeout(timer);
				context.signal.removeEventListener('abort', abort);
				controller.abort();
				if (interrupted || failed)
					reject(interrupted ? interrupted.error : error);
				else
					resolve(reply!);
			};
			const interrupt = (error: unknown) => {
				interrupted ??= {error};
				context.clock.clearTimeout(timer);
				controller.abort();
			};
			const abort = () => interrupt(new BackendFailure(Status.Cancelled, 'RPC cancelled'));
			const timer = context.clock.setTimeout(
			    () => interrupt(new BackendFailure(Status.Timeout, 'RPC ACK timeout')),
			    this.options.ackTimeoutMs ?? 1000);
			context.signal.addEventListener('abort', abort, {once : true});
			if (context.signal.aborted) {
				settle(new BackendFailure(Status.Cancelled, 'RPC cancelled'));
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
					    session : context.session,
					    requestId : context.requestId,
					    signal : controller.signal
				    }))
				    .then(reply => settle(undefined, reply, false), error => settle(error));
			} catch (error) {
				settle(error);
			}
		});
	}
}
