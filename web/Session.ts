/* Optional owned connection coordination over one existing client FIFO. MIT.
 * Authors: Ruslan Kovtun (shpegun60), codexAi.
 */
import {
	DeviceClient,
	Status,
	type Backend,
	type BackendContext,
	type ByteInput,
	type ClientStatus,
	type Clock,
	type BatchItem,
	type BatchItems,
	type BatchOperation,
	type BatchResult,
	type CommandBinding,
	type Field,
	type Operation,
	type ReadBinding,
	type ReadManyField,
	type ReadManyItem,
	type Result,
	type ServiceBinding,
	type WireRequest,
	type WireResult,
	type WriteBinding
} from '../lib/client/web/Client.js';
import {
	DynamicClient,
	type DynamicBatchResult,
	type DynamicReadManyResult,
	type DynamicResult,
	type EndpointOperation
} from './DynamicClient.js';
import {
	matchesDescriptorHeader,
	parseDescriptor,
	type Limits,
	type RuntimeSchema
} from './telemetry.js';

export type SessionMode = 'static'|'dynamic';
export type SessionState = 'connecting'|'bootstrapping'|'ready'|'disconnecting'|
                           'disconnected'|'recovery-required'|'closed';
export type ConnectionErrorCode = 'InvalidArgument'|'Busy'|'Closed'|'Aborted'|
    'BootstrapFailed'|'SchemaMismatch'|'RecoveryRequired'|'CleanupFailed'|
    'AdmissionFailed'|'TransportLost';

export class ConnectionError extends Error {
	constructor(readonly code: ConnectionErrorCode, message: string, cause?: unknown)
	{
		super(message, {cause});
		this.name = 'ConnectionError';
	}
}

export interface PreviousConnection {
	readonly peer: string;
	readonly session: number;
}
export interface ProfileOpenContext {
	readonly mode: SessionMode;
	readonly signal: AbortSignal;
	readonly previous?: PreviousConnection;
	readonly onDisconnect: (reason: unknown) => void;
}
export interface OwnedConnection extends PreviousConnection {
	readonly header: Uint8Array;
	readonly descriptor?: Uint8Array;
	/** Discovery snapshots only; operation-specific capability admission is separate. */
	readonly readMany?: Uint8Array;
	readonly batch?: Uint8Array;
	readonly backend: Backend;
	/** Exact old peer/generation whose remote lease this fresh bootstrap supersedes. */
	readonly supersedes?: PreviousConnection;
	/** Idempotent actual local close/drain fence, not evidence of remote completion. */
	close(): Promise<void>;
}
export interface ConnectionProfile {
	/** A privileged host adapter: successful open establishes an exclusive remote lease.
	 * Rejected/aborted opens must retain and release all partially opened resources. */
	open(context: ProfileOpenContext): Promise<OwnedConnection>;
}
export interface DiscoveredCapabilities {
	readonly readMany?: Uint8Array;
	readonly batch?: Uint8Array;
}
export type SessionValueStatus = 'Ok'|'NoValue'|'InvalidPayload'|'Stale';
export type SessionResult<T = undefined> = Readonly<Omit<Result<T>, 'value'>&{
	readonly valueStatus: SessionValueStatus;
	readonly value: T|undefined;
	/** Unmodified common operation Result, including historical checked facts. */
	readonly rawResult: Result<T>;
}>;
export interface SessionOperationRequest<R> extends Promise<R> {
	readonly requestId: number;
	readonly session: number;
	cancel(): boolean;
}
export interface SessionRequest<T> extends SessionOperationRequest<SessionResult<T>> {}
export interface SessionReadManyItem<T = unknown> {
	readonly id: number;
	readonly status: 0|1|2|3|4;
	readonly payload: Uint8Array;
	readonly valueStatus: SessionValueStatus;
	readonly value: T|undefined;
}
export type SessionBatchItem<T = unknown> = Readonly < Omit<Result<T>, 'value'>&
{
	readonly id: number;
	readonly valueStatus: SessionValueStatus;
	readonly value: T|undefined;
}
> ;
export type SessionItemsResult<I extends readonly unknown[]> =
    Readonly < Omit<SessionResult<undefined>, 'rawResult'>&
{
	readonly rawResult: Result<unknown>;
	readonly items: I|undefined;
}
> ;
type ReadManyValue<F> = F extends {readonly read : ReadBinding<infer T>}
                                      ? T
                                      : F extends ReadBinding<infer T>? T : never;
export type SessionReadManyItems<F extends readonly ReadManyField<unknown>[]> = {
	readonly[K in keyof F]: SessionReadManyItem<ReadManyValue<F[K]>>;
};
export type SessionBatchItems<I extends readonly BatchItem<unknown>[]> = {
	readonly[K in keyof I]: SessionBatchItem<BatchItems<I>[ K ]['value']>;
};
export interface SessionBatchOperation {
	readonly operation: BatchOperation;
	readonly id: number;
	readonly value?: unknown;
}
export interface SessionSnapshot {
	readonly mode: SessionMode;
	readonly state: SessionState;
	readonly ready: boolean;
	readonly session: number;
	readonly fingerprint: bigint|undefined;
	readonly recoveryRequired: boolean;
	readonly recoveryGeneration: number;
	readonly lastError: ConnectionError|undefined;
}
export interface ConnectionSession extends SessionSnapshot {
	/** Called immediately with the current immutable snapshot. Listener errors are isolated.
	 * A nonready snapshot revokes submission; it is not an outcome/projection drain fence. */
	subscribe(listener: (snapshot: SessionSnapshot) => void): () => void;
	/** Copies of unvalidated discovery bytes; absent outside the ready state. */
	readonly capabilities: DiscoveredCapabilities|undefined;
	/** Settles managed result projections and actual local close; retains remote obligations.
	 * Keep pending request/outcome owners alive until their request Promises resolve. */
	disconnect(): Promise<void>;
	reconnect(options?: {readonly signal?: AbortSignal}): Promise<void>;
	/** Terminal logical shutdown and actual LOCAL close. Remote recovery facts survive. */
	close(): Promise<void>;
}
export interface StaticConnectionSession extends ConnectionSession {
	/** The original generated SDK module; no client or connection ownership. */
	readonly sdk: StaticSdk;
	readonly metadata: unknown;
	read<T>(field: Field<T>|ReadBinding<T>, timeoutMs?: number): SessionRequest<T>;
	write<T>(field: Field<T>|WriteBinding<T>, value: T,
	         timeoutMs?: number): SessionRequest<undefined>;
	/** Edit using the original FieldRead bytes. Requires generated --wire-editing support. */
	writePreserving<T>(field: Field<T>|WriteBinding<T>, value: T, originalPayload: ByteInput,
	                   timeoutMs?: number): SessionRequest<undefined>;
	command<T>(endpoint: CommandBinding<T>, value: T,
	           timeoutMs?: number): SessionRequest<undefined>;
	service<Q, R>(endpoint: ServiceBinding<Q, R>, value: Q,
	              timeoutMs?: number): SessionRequest<R>;
	readMany<const F extends readonly ReadManyField<unknown>[]>(capability: ByteInput, fields: F,
	                                                            timeoutMs?: number):
	    SessionOperationRequest<SessionItemsResult<SessionReadManyItems<F>>>;
	batch<const I extends readonly BatchItem<unknown>[]>(capability: ByteInput, items: I,
	                                                     timeoutMs?: number):
	    SessionOperationRequest<SessionItemsResult<SessionBatchItems<I>>>;
}
export interface DynamicConnectionSession extends ConnectionSession {
	readonly schema: RuntimeSchema|undefined;
	read(id: number, timeoutMs?: number): SessionRequest<unknown>;
	write(id: number, value: unknown, timeoutMs?: number): SessionRequest<undefined>;
	writePreserving(id: number, value: unknown, originalPayload: ByteInput,
	                timeoutMs?: number): SessionRequest<undefined>;
	command(id: number, value?: unknown, timeoutMs?: number): SessionRequest<undefined>;
	service(id: number, value?: unknown, timeoutMs?: number): SessionRequest<unknown>;
	readMany(capability: ByteInput, ids: readonly number[], timeoutMs?: number):
	    SessionOperationRequest<SessionItemsResult<readonly SessionReadManyItem[]>>;
	batch(capability: ByteInput, items: readonly SessionBatchOperation[], timeoutMs?: number):
	    SessionOperationRequest<SessionItemsResult<readonly SessionBatchItem[]>>;
}
export interface StaticSdk {
	matchesHeader(header: ByteInput): boolean;
}
export interface ConnectionOptions {
	readonly transport: ConnectionProfile;
	readonly signal?: AbortSignal;
	readonly clock?: Clock;
	readonly timeoutMs?: number;
	readonly maxPending?: number;
	readonly maxPayloadBytes?: number;
}
export interface StaticSessionOptions extends ConnectionOptions {
	readonly sdk: StaticSdk;
	/** Optional structural metadata for a later UX adapter; unnecessary for typed I/O. */
	readonly metadata?: unknown;
}
export interface DynamicSessionOptions extends ConnectionOptions {
	readonly limits?: Partial<Limits>;
}

type RawValue = Result<unknown>|DynamicResult|DynamicReadManyResult|DynamicBatchResult|
    BatchResult<readonly unknown[]>;
type RawRequest = PromiseLike<RawValue>&
{
	readonly requestId: number;
	readonly session: number;
	cancel(): boolean;
};
interface Lease extends PreviousConnection {
	readonly source: OwnedConnection;
	readonly header: Uint8Array;
	readonly descriptor?: Uint8Array;
	readonly readMany?: Uint8Array;
	readonly batch?: Uint8Array;
	readonly backend: Backend;
	readonly supersedes?: PreviousConnection;
	local?: Promise<void>;
	locallyClosed: boolean;
}
interface Retirement {
	readonly owner: SessionOwner;
	readonly lease: Lease;
	readonly admitted: boolean;
	confirmed: boolean;
}
interface ProfileRecord {
	owner?: SessionOwner;
	retired?: Retirement;
	peer?: string;
	highWater: number;
}
// Identity-scoped exclusivity, not a global registry of arbitrary device URLs.
const profiles = new WeakMap<ConnectionProfile, ProfileRecord>();
const copy = (input: ByteInput): Uint8Array => new Uint8Array(
    input instanceof ArrayBuffer ? new Uint8Array(input)
                                 : new Uint8Array(input.buffer, input.byteOffset, input.byteLength));
const validGeneration = (value: number): boolean =>
    Number.isInteger(value) && value > 0 && value <= 0xffffffff;
const error = (code: ConnectionErrorCode, message: string, cause?: unknown): ConnectionError =>
    cause instanceof ConnectionError ? cause : new ConnectionError(code, message, cause);

class ForwardingBackend implements Backend {
	delegate?: Backend;
	supports(operation: Operation): boolean
	{
		return this.delegate?.supports?.(operation) ??
		       (!!this.delegate && operation !== 'ReadMany' && operation !== 'Batch');
	}
	execute(request: WireRequest, context: BackendContext): Promise<WireResult>
	{
		const target = this.delegate;
		return target ? target.execute(request, context)
		              : Promise.reject(new Error('No admitted connection backend'));
	}
	confirmSessionClosed(generation: number): boolean
	{
		const target = this.delegate;
		return !!target && (target.confirmSessionClosed
		                        ? target.confirmSessionClosed(generation) === true : true);
	}
}

class SessionOwner {
	readonly mode: SessionMode;
	readonly metadata: unknown;
	private readonly profile: ConnectionProfile;
	private readonly record: ProfileRecord;
	readonly sdk?: StaticSdk;
	private readonly limits?: Partial<Limits>;
	private readonly forwarder = new ForwardingBackend();
	private readonly core: DeviceClient|DynamicClient;
	private readonly listeners = new Set<(snapshot: SessionSnapshot) => void>();
	private readonly requests = new Set<{raw: RawRequest, mapped: Promise<unknown>}>();
	private readonly fences = new Set<Promise<void>>();
	private active?: Lease;
	private stateValue: SessionState = 'disconnected';
	private errorValue?: ConnectionError;
	private fingerprintValue?: bigint;
	private terminal = false;
	private revision = 0;
	private openAbort?: AbortController;
	private openWork?: Promise<void>;
	private opening?: Promise<void>;
	private disconnecting?: Promise<void>;
	private closing?: Promise<void>;
	private closedRecovery?: {required: boolean, generation: number};

	constructor(mode: SessionMode, options: StaticSessionOptions|DynamicSessionOptions)
	{
		if (!options || !options.transport || typeof options.transport.open !== 'function')
			throw new ConnectionError('InvalidArgument', 'A connection profile is required');
		this.mode = mode;
		this.profile = options.transport;
		this.record = profiles.get(this.profile) ?? {highWater : 0};
		profiles.set(this.profile, this.record);
		if (mode === 'static') {
			const selected = options as StaticSessionOptions;
			if (!selected.sdk || typeof selected.sdk.matchesHeader !== 'function')
				throw new ConnectionError('InvalidArgument', 'Generated sdk.matchesHeader is required');
			this.sdk = selected.sdk;
			this.metadata = selected.metadata;
		} else {
			this.limits = (options as DynamicSessionOptions).limits;
		}
		const coreOptions = {
			backend : this.forwarder, clock : options.clock, timeoutMs : options.timeoutMs,
			maxPending : options.maxPending, maxPayloadBytes : options.maxPayloadBytes
		};
		this.core = mode === 'static'
		                ? new DeviceClient({...coreOptions,
			                                     matchesHeader : bytes => this.sdk!.matchesHeader(bytes)})
		                : new DynamicClient(coreOptions);
	}

	get state(): SessionState { return this.stateValue; }
	get ready(): boolean { return this.stateValue === 'ready' && this.core.ready; }
	get session(): number { return this.core.session; }
	get fingerprint(): bigint|undefined { return this.fingerprintValue; }
	get recoveryRequired(): boolean
	{
		return this.closedRecovery?.required ??
		       (this.core.recoveryRequired || this.stateValue === 'recovery-required');
	}
	get recoveryGeneration(): number
	{
		return this.closedRecovery?.generation ?? this.core.recoveryGeneration;
	}
	get lastError(): ConnectionError|undefined { return this.errorValue; }
	get schema(): RuntimeSchema|undefined
	{
		return this.ready && this.core instanceof DynamicClient ? this.core.schema : undefined;
	}
	get capabilities(): DiscoveredCapabilities|undefined
	{
		return this.ready && this.active ? Object.freeze({
			readMany : this.active.readMany?.slice(), batch : this.active.batch?.slice()
		}) : undefined;
	}

	subscribe(listener: (snapshot: SessionSnapshot) => void): () => void
	{
		if (typeof listener !== 'function')
			throw new TypeError('A session listener is required');
		this.listeners.add(listener);
		this.deliver(listener, this.snapshot());
		return () => { this.listeners.delete(listener); };
	}
	private snapshot(): SessionSnapshot
	{
		return Object.freeze({mode : this.mode, state : this.state, ready : this.ready,
		                       session : this.session, fingerprint : this.fingerprint,
		                       recoveryRequired : this.recoveryRequired,
		                       recoveryGeneration : this.recoveryGeneration,
		                       lastError : this.lastError});
	}
	private deliver(listener: (snapshot: SessionSnapshot) => void, value: SessionSnapshot): void
	{
		try { listener(value); } catch { /* Observers cannot change execution outcomes. */ }
	}
	private notify(): void
	{
		const revision = this.revision, value = this.snapshot();
		for (const listener of [...this.listeners]) {
			if (revision !== this.revision)
				break;
			if (this.listeners.has(listener))
				this.deliver(listener, value);
		}
	}
	private current(revision: number, signal: AbortSignal): void
	{
		if (this.terminal)
			throw new ConnectionError('Closed', 'The session is closed');
		if (signal.aborted || revision !== this.revision)
			throw new ConnectionError('Aborted', 'The connection attempt was retired', signal.reason);
	}
	private claim(): void
	{
		if (this.record.owner && this.record.owner !== this)
			throw new ConnectionError('Busy', 'This connection profile already has an owner');
		this.record.owner = this;
	}
	private closeLease(lease: Lease): Promise<void>
	{
		if (!lease.local) {
			let resolve!: () => void, reject!: (reason: unknown) => void;
			lease.local = new Promise<void>((yes, no) => { resolve = yes; reject = no; });
			this.fences.add(lease.local);
			lease.local.catch(() => {});
			try {
				Promise.resolve(lease.source.close()).then(() => {
					lease.locallyClosed = true;
					this.fences.delete(lease.local!);
					resolve();
				}, reject);
			} catch (cause) { reject(cause); }
		}
		return lease.local;
	}
	private retire(lease: Lease): void
	{
		if (!this.record.retired && typeof lease.peer === 'string' && lease.peer &&
		    validGeneration(lease.session))
			this.record.retired = {owner : this, lease,
			                       admitted : this.core.session === lease.session,
			                       confirmed : false};
		void this.closeLease(lease);
	}
	private async requestsFinished(): Promise<void>
	{
		while (this.requests.size)
			await Promise.allSettled([...this.requests].map(item => item.mapped));
	}
	private async localFences(): Promise<void>
	{
		await Promise.all([...this.fences]);
	}
	private invalidate(): void
	{
		if (this.core instanceof DynamicClient)
			this.core.invalidate();
		else
			this.core.invalidateAdmission();
		for (const request of [...this.requests])
			request.raw.cancel();
	}

	disconnect(): Promise<void>
	{
		++this.revision;
		this.openAbort?.abort();
		if (this.disconnecting)
			return this.disconnecting;
		let resolve!: () => void, reject!: (reason: unknown) => void;
		const pending = new Promise<void>((yes, no) => { resolve = yes; reject = no; });
		this.disconnecting = pending;
		this.stateValue = this.terminal ? 'closed' : 'disconnecting';
		this.invalidate();
		if (this.active) {
			const lease = this.active;
			this.active = undefined;
			this.retire(lease);
		}
		const open = this.openWork;
		this.notify();
		void (async () => {
			try {
				await open?.catch(() => {});
				await this.requestsFinished();
				await this.localFences();
				if (!this.terminal)
					this.stateValue = this.core.recoveryRequired ? 'recovery-required' : 'disconnected';
				resolve();
			} catch (cause) {
				this.errorValue = error('CleanupFailed', 'The owned transport has not closed', cause);
				if (!this.terminal)
					this.stateValue = 'recovery-required';
				reject(this.errorValue);
			} finally {
				if (this.disconnecting === pending)
					this.disconnecting = undefined;
				this.notify();
			}
		})();
		return pending;
	}

	reconnect(options: {readonly signal?: AbortSignal} = {}): Promise<void>
	{
		if (this.terminal)
			return Promise.reject(new ConnectionError('Closed', 'The session is closed'));
		if (this.opening)
			return this.opening;
		// A synchronous observer can issue another disconnect while ours publishes.
		// That explicit revocation must retire this requested reconnect too.
		const revision = this.revision + 1, disconnected = this.disconnect();
		const pending = disconnected.then(async () => {
			if (this.terminal)
				throw new ConnectionError('Closed', 'The session is closed');
			if (revision !== this.revision)
				throw new ConnectionError('Aborted', 'The connection attempt was retired');
			this.claim();
			const work = this.openConnection(options.signal);
			this.openWork = work;
			try { await work; }
			finally { if (this.openWork === work) this.openWork = undefined; }
		});
		this.opening = pending;
		pending.then(() => { if (this.opening === pending) this.opening = undefined; },
		             () => { if (this.opening === pending) this.opening = undefined; });
		return pending;
	}
	private async openConnection(external?: AbortSignal): Promise<void>
	{
		const controller = new AbortController(), revision = ++this.revision;
		this.openAbort = controller;
		const aborted = () => controller.abort(external?.reason);
		external?.addEventListener('abort', aborted, {once : true});
		if (external?.aborted) aborted();
		let candidate: Lease|undefined;
		try {
			this.current(revision, controller.signal);
			this.stateValue = 'connecting';
			this.errorValue = undefined;
			this.notify();
			this.current(revision, controller.signal);
			const previous = this.record.retired;
			const lease = await this.profile.open({mode : this.mode, signal : controller.signal,
			    previous : previous ? Object.freeze({peer : previous.lease.peer,
			                                         session : previous.lease.session}) : undefined,
			    onDisconnect : reason => {
				    if (revision !== this.revision || this.terminal) return;
				    this.errorValue = error('TransportLost', 'The owned connection ended', reason);
				    if (this.active) void this.disconnect().catch(() => {});
				    else controller.abort(reason);
			    }});
			// Capture a returned lease before checking attempt cancellation so it is never orphaned.
			candidate = {source : lease, peer : '', session : 0,
			             backend : undefined as unknown as Backend, header : new Uint8Array(0),
			             locallyClosed : false};
			Object.assign(candidate, {peer : lease.peer, session : lease.session,
			             backend : lease.backend, header : copy(lease.header),
			             descriptor : lease.descriptor && copy(lease.descriptor),
			             readMany : lease.readMany && copy(lease.readMany),
			             batch : lease.batch && copy(lease.batch),
			             supersedes : lease.supersedes && Object.freeze({
				             peer : lease.supersedes.peer, session : lease.supersedes.session}),
			             locallyClosed : false});
			this.current(revision, controller.signal);
			if (typeof candidate.peer !== 'string' || !candidate.peer ||
			    !validGeneration(candidate.session) || candidate.header.length !== 64 ||
			    !candidate.backend || typeof candidate.backend.execute !== 'function' ||
			    typeof lease.close !== 'function')
				throw new ConnectionError('BootstrapFailed', 'Invalid owned connection bootstrap');
			if ((this.record.peer && candidate.peer !== this.record.peer) ||
			    candidate.session <= this.record.highWater)
				throw new ConnectionError('RecoveryRequired', 'A newer bootstrap from the same peer is required');
			this.record.peer = candidate.peer;
			this.record.highWater = candidate.session;
			this.stateValue = 'bootstrapping';
			this.notify();
			this.current(revision, controller.signal);
			let schema: RuntimeSchema|undefined;
			if (this.core instanceof DynamicClient) {
				if (!candidate.descriptor)
					throw new ConnectionError('BootstrapFailed', 'Dynamic connection requires a Descriptor');
				schema = parseDescriptor(candidate.descriptor, this.limits);
				if (!matchesDescriptorHeader(schema, candidate.header))
					throw new ConnectionError('SchemaMismatch', 'Observed header does not match the Descriptor');
			} else if (!this.sdk!.matchesHeader(candidate.header)) {
				throw new ConnectionError('SchemaMismatch', 'Static Descriptor header does not match the generated SDK');
			}
			this.current(revision, controller.signal);
			if (previous && !previous.confirmed) {
				if (candidate.peer !== previous.lease.peer ||
				    candidate.session <= previous.lease.session ||
				    candidate.supersedes?.peer !== previous.lease.peer ||
				    candidate.supersedes?.session !== previous.lease.session)
					throw new ConnectionError('RecoveryRequired', 'The fresh bootstrap did not prove old remote cleanup');
				await previous.owner.requestsFinished();
				await previous.owner.closeLease(previous.lease);
				this.current(revision, controller.signal);
				if (!previous.owner.confirm(previous))
					throw new ConnectionError('RecoveryRequired', 'The old client could not confirm session cleanup');
				previous.confirmed = true;
			}
			this.current(revision, controller.signal);
			if (this.record.retired === previous)
				this.record.retired = undefined;
			this.forwarder.delegate = candidate.backend;
			this.active = candidate;
			const admitted = this.core instanceof DynamicClient
			                     ? this.core.admit(schema!, candidate.header, candidate.session) === Status.Ok
			                     : this.core.openSession(candidate.session, candidate.header);
			if (!admitted)
				throw new ConnectionError('AdmissionFailed', 'Client admission refused the bootstrap');
			this.current(revision, controller.signal);
			this.fingerprintValue = new DataView(candidate.header.buffer,
			    candidate.header.byteOffset, candidate.header.byteLength).getBigUint64(16, true);
			this.stateValue = 'ready';
			this.notify();
			this.current(revision, controller.signal);
		} catch (cause) {
			if (candidate) {
				if (this.active === candidate) { this.invalidate(); this.active = undefined; }
				this.retire(candidate);
				try { await this.closeLease(candidate); }
				catch (cleanup) {
					this.errorValue = error('CleanupFailed', 'Candidate transport did not close', cleanup);
					if (!this.terminal) this.stateValue = 'recovery-required';
					this.notify();
					throw this.errorValue;
				}
			}
			const failure = error(controller.signal.aborted ? 'Aborted' : 'BootstrapFailed',
			                      'Connection bootstrap failed', cause);
			if (!this.terminal && revision === this.revision) {
				this.errorValue = failure;
				this.stateValue = failure.code === 'RecoveryRequired' || this.core.recoveryRequired
				                      ? 'recovery-required' : 'disconnected';
				this.notify();
			}
			throw failure;
		} finally {
			external?.removeEventListener('abort', aborted);
			if (this.openAbort === controller) this.openAbort = undefined;
		}
	}
	private confirm(retirement: Retirement): boolean
	{
		if (!retirement.lease.locallyClosed || this.requests.size || this.core.pendingCount)
			return false;
		if (!retirement.admitted) return true;
		return this.core instanceof DynamicClient
		           ? this.core.confirmQuiescence({session : retirement.lease.session,
			                                 txReleased : true, oldFramesExcluded : true,
			                                 remoteSessionClosed : true})
		           : this.core.confirmSessionClosed(retirement.lease.session);
	}

	close(): Promise<void>
	{
		if (this.closing) return this.closing;
		let resolve!: () => void, reject!: (reason: unknown) => void;
		const pending = new Promise<void>((yes, no) => { resolve = yes; reject = no; });
		this.closing = pending;
		this.terminal = true;
		this.stateValue = 'closed';
		const disconnected = this.disconnect();
		this.core.close();
		this.closedRecovery = {required : this.core.recoveryRequired,
		                       generation : this.core.recoveryGeneration};
		void (async () => {
			try {
				await disconnected;
				await this.requestsFinished();
				await this.localFences();
				this.closedRecovery = {required : this.core.recoveryRequired,
				                       generation : this.core.recoveryGeneration};
				if (this.record.owner === this) this.record.owner = undefined;
				this.stateValue = 'closed';
				this.notify();
				resolve();
			} catch (cause) {
				this.closedRecovery = {required : true,
				                       generation : this.core.recoveryGeneration || this.core.session};
				reject(cause);
			}
		})();
		return pending;
	}

	read(endpoint: unknown, timeoutMs?: number): SessionRequest<unknown>
	{
		return this.submit('FieldRead', endpoint, undefined, timeoutMs);
	}
	write(endpoint: unknown, value: unknown, timeoutMs?: number): SessionRequest<unknown>
	{
		return this.submit('FieldWrite', endpoint, value, timeoutMs);
	}
	writePreserving(endpoint: unknown, value: unknown, originalPayload: ByteInput,
	                timeoutMs?: number): SessionRequest<unknown>
	{
		return this.submit('FieldWrite', endpoint, value, timeoutMs, originalPayload, true);
	}
	command(endpoint: unknown, value?: unknown, timeoutMs?: number): SessionRequest<unknown>
	{
		return this.submit('Command', endpoint, value, timeoutMs);
	}
	service(endpoint: unknown, value?: unknown, timeoutMs?: number): SessionRequest<unknown>
	{
		return this.submit('Service', endpoint, value, timeoutMs);
	}
	private gate(): ClientStatus
	{
		return this.terminal           ? Status.Closed
		       : this.recoveryRequired ? Status.RecoveryRequired
		       : this.ready            ? Status.Ok
		                               : Status.NotAdmitted;
	}

	private stillCurrent(revision: number): boolean
	{
		return this.ready && revision === this.revision;
	}

	private optionalRefused<I extends readonly unknown[]>(operation: 'ReadMany'|'Batch',
	                                                      status: ClientStatus):
	    SessionOperationRequest<SessionItemsResult<I>>
	{
		const refused = this.refused(operation, status);
		const mapped = refused.then(
		    result => Object.freeze({...result, value : undefined, items : undefined}));
		return Object.assign(mapped,
		                     {requestId : 0, session : refused.session, cancel : () => false});
	}

	readMany(capability: ByteInput, fields: readonly unknown[], timeoutMs?: number):
	    SessionOperationRequest<SessionItemsResult<readonly SessionReadManyItem[]>>
	{
		const gate = this.gate();
		if (gate !== Status.Ok)
			return this.optionalRefused('ReadMany', gate);
		const revision = this.revision;
		let record: Uint8Array, selected: unknown[];
		try {
			record = copy(capability);
			if (!Array.isArray(fields))
				throw new TypeError('ReadMany requires 1..32 Fields');
			const count = fields.length;
			if (!Number.isInteger(count) || count < 1 || count > 32)
				throw new TypeError('ReadMany requires 1..32 Fields');
			selected = [];
			for (let i = 0; i < count; ++i)
				selected.push(fields[i]);
		} catch {
			return this.optionalRefused(
			    'ReadMany', this.stillCurrent(revision) ? Status.InvalidArgument : this.gate());
		}
		if (!this.stillCurrent(revision))
			return this.optionalRefused('ReadMany', this.gate());
		let raw: RawRequest;
		if (this.core instanceof DynamicClient) {
			const endpoints = [];
			for (const id of selected) {
				if (typeof id !== 'number' || !Number.isInteger(id) || id < 0 || id > 0xffffffff)
					return this.optionalRefused('ReadMany', Status.InvalidArgument);
				const bound = this.core.bind('FieldRead', id);
				if (bound.status !== Status.Ok)
					return this.optionalRefused('ReadMany', bound.status);
				endpoints.push(bound.endpoint);
			}
			const bound = this.core.bindReadMany(record, endpoints);
			if (bound.status !== Status.Ok)
				return this.optionalRefused('ReadMany', bound.status);
			raw = this.core.readMany(bound.binding, timeoutMs);
		} else {
			const bound = this.core.bindReadMany(record, selected as ReadManyField<unknown>[]);
			if (bound.status !== Status.Ok)
				return this.optionalRefused('ReadMany', bound.status);
			if (!this.stillCurrent(revision))
				return this.optionalRefused('ReadMany', this.gate());
			raw = this.core.readMany(bound.binding, timeoutMs);
		}
		return this.track(raw, result => {
			const dynamic = this.mode === 'dynamic' ? result as DynamicReadManyResult : undefined;
			const rawResult = dynamic ? dynamic.operation : result as Result<unknown>;
			const current = this.stillCurrent(revision) && rawResult.session === this.session;
			const valueStatus: SessionValueStatus =
			    !current                                                ? 'Stale'
			    : dynamic                                               ? dynamic.valueStatus
			    : rawResult.status === Status.CodecError                ? 'InvalidPayload'
			    : rawResult.succeeded && Array.isArray(rawResult.value) ? 'Ok'
			                                                            : 'NoValue';
			const rows = dynamic ? dynamic.items
			                     : rawResult.value as readonly ReadManyItem<unknown>[] | undefined;
			const items = Array.isArray(rows) ? Object.freeze(rows.map(row => {
				const status = row.status === 0 ? current ? valueStatus : 'Stale' : 'NoValue';
				return Object.freeze({
					id : row.id,
					status : row.status,
					payload : copy(row.payload),
					valueStatus : status,
					value : status === 'Ok' ? row.value : undefined
				});
			}))
			                                  : undefined;
			return Object.freeze({...rawResult, rawResult, valueStatus, value : undefined, items});
		});
	}

	batch(capability: ByteInput, items: readonly unknown[], timeoutMs?: number):
	    SessionOperationRequest<SessionItemsResult<readonly SessionBatchItem[]>>
	{
		const refuse = (status: ClientStatus) =>
		    this.optionalRefused<readonly SessionBatchItem[]>('Batch', status);
		const gate = this.gate();
		if (gate !== Status.Ok)
			return refuse(gate);
		const revision = this.revision;
		let record: Uint8Array, selected: unknown[];
		try {
			record = copy(capability);
			if (!Array.isArray(items))
				throw new TypeError('Batch requires 1..32 items');
			const count = items.length;
			if (!Number.isInteger(count) || count < 1 || count > 32)
				throw new TypeError('Batch requires 1..32 items');
			selected = [];
			for (let i = 0; i < count; ++i) {
				const item = items[i];
				selected.push(this.mode === 'dynamic'
				                  ? {operation : item.operation, id : item.id, value : item.value}
				                  : item);
			}
		} catch {
			return refuse(this.stillCurrent(revision) ? Status.InvalidArgument : this.gate());
		}
		if (!this.stillCurrent(revision))
			return refuse(this.gate());
		let raw: RawRequest;
		if (this.core instanceof DynamicClient) {
			const requests = [];
			for (const item of selected as SessionBatchOperation[]) {
				if (!['FieldRead', 'FieldWrite', 'Command', 'Service'].includes(item.operation) ||
				    !Number.isInteger(item.id) || item.id < 0 || item.id > 0xffffffff)
					return refuse(Status.InvalidArgument);
				const bound = this.core.bind(item.operation, item.id);
				if (bound.status !== Status.Ok)
					return refuse(bound.status);
				requests.push({endpoint : bound.endpoint, value : item.value});
			}
			raw = this.core.batch(record, requests, timeoutMs);
		} else {
			raw = this.core.batch(record, selected as BatchItem<unknown>[], timeoutMs);
		}
		return this.track(raw, result => {
			const dynamic = this.mode === 'dynamic' ? result as DynamicBatchResult : undefined;
			const rawResult =
			    dynamic ? dynamic.operation : result as BatchResult<readonly unknown[]>;
			const source = dynamic ?? result as BatchResult<readonly SessionBatchItem[]>;
			const current = this.stillCurrent(revision) && rawResult.session === this.session;
			const valueStatus: SessionValueStatus = current ? source.valueStatus : 'Stale';
			const items = source.items && Object.freeze(source.items.map(row => {
				const status =
				    !current && row.valueStatus !== 'NoValue' ? 'Stale' : row.valueStatus;
				return Object.freeze({
					...row,
					payload : copy(row.payload),
					valueStatus : status,
					value : status === 'Ok' ? row.value : undefined
				});
			}));
			return Object.freeze({...rawResult, rawResult, valueStatus, value : undefined, items});
		});
	}

	private refused(operation: Operation, status: ClientStatus): SessionRequest<unknown>
	{
		const rawResult: Result<unknown> = Object.freeze({status, operation,
		    session : this.session, requestId : 0, dispatch : 0, endpointStatus : 0,
		    succeeded : false, accepted : false, executed : false,
		    payload : new Uint8Array(0), value : undefined});
		return Object.assign(Promise.resolve(Object.freeze({...rawResult,
		    valueStatus : this.ready ? 'NoValue' as const : 'Stale' as const, rawResult})),
		    {requestId : 0, session : rawResult.session, cancel : () => false});
	}
	private submit(operation: EndpointOperation, endpoint: unknown, value: unknown,
	               timeoutMs?: number, originalPayload?: ByteInput,
	               preserving = false): SessionRequest<unknown>
	{
		if (!this.ready)
			return this.refused(operation, this.terminal ? Status.Closed :
			    this.recoveryRequired ? Status.RecoveryRequired : Status.NotAdmitted);
		const revision = this.revision;
		let seed: Uint8Array|undefined;
		if (preserving) {
			// Own the seed before binding accessors or DTO conversion can reenter/mutate it.
			if (!(originalPayload instanceof ArrayBuffer) && !ArrayBuffer.isView(originalPayload))
				return this.refused(operation, Status.InvalidArgument);
			try { seed = copy(originalPayload); }
			catch { return this.refused(operation, Status.InvalidArgument); }
		}
		let raw: RawRequest;
		if (this.core instanceof DynamicClient) {
			if (typeof endpoint !== 'number' || !Number.isInteger(endpoint) || endpoint < 0 || endpoint > 0xffffffff)
				return this.refused(operation, Status.InvalidArgument);
			const binding = this.core.bind(operation, endpoint);
			if (binding.status !== Status.Ok) return this.refused(operation, binding.status);
			raw = operation === 'FieldRead' ? this.core.readField(binding.endpoint, timeoutMs)
			      : operation === 'FieldWrite'
			          ? preserving
			                ? this.core.writeFieldPreserving(binding.endpoint, value, seed!, timeoutMs)
			                : this.core.writeField(binding.endpoint, value, timeoutMs)
			      : operation === 'Command' ? this.core.command(binding.endpoint, value, timeoutMs)
			                                : this.core.service(binding.endpoint, value, timeoutMs);
		} else {
			if (preserving) {
				try {
					if (!endpoint || (typeof endpoint !== 'object' && typeof endpoint !== 'function'))
						return this.refused(operation, Status.InvalidArgument);
					const field = endpoint as Field<unknown>|WriteBinding<unknown>;
					const write = 'read' in field ? field.write : field;
					if (!write) return this.refused(operation, Status.ReadOnly);
					const {fingerprint, operation: declared, id, requestBytes, responseBytes} = write;
					if (declared !== 'FieldWrite') return this.refused(operation, Status.InvalidArgument);
					const encode = (write as WriteBinding<unknown> & {
						encodePreserving?: (value: unknown, original: ByteInput) => Uint8Array
					}).encodePreserving;
					if (typeof encode !== 'function') return this.refused(operation, Status.Unsupported);
					endpoint = {fingerprint, operation: declared, id, requestBytes, responseBytes,
					            encode: (edited: unknown) => encode.call(write, edited, seed!)};
				} catch { return this.refused(operation, Status.InvalidArgument); }
			}
			raw = operation === 'FieldRead'
			          ? this.core.read(endpoint as Field<unknown>|ReadBinding<unknown>, timeoutMs)
			      : operation === 'FieldWrite'
			          ? this.core.write(endpoint as Field<unknown>|WriteBinding<unknown>, value, timeoutMs)
			      : operation === 'Command'
			          ? this.core.command(endpoint as CommandBinding<unknown>, value, timeoutMs)
			          : this.core.service(endpoint as ServiceBinding<unknown, unknown>, value, timeoutMs);
		}
		return this.track(raw, result => {
			const dynamic = this.mode === 'dynamic' ? result as DynamicResult : undefined;
			const rawResult = dynamic ? dynamic.operation : result as Result<unknown>;
			const current = this.ready && revision === this.revision && rawResult.session === this.session;
			const valueStatus: SessionValueStatus = !current ? 'Stale' : dynamic ? dynamic.valueStatus
			    : rawResult.status === Status.CodecError ? 'InvalidPayload'
			    : rawResult.succeeded && rawResult.value !== undefined ? 'Ok' : 'NoValue';
			const projected = Object.freeze({...rawResult, rawResult, valueStatus,
			    value : valueStatus === 'Ok' ? dynamic ? dynamic.value : rawResult.value : undefined});
			return projected;
		});
	}
	private track<R>(raw: RawRequest, project: (result: RawValue) => R): SessionOperationRequest<R>
	{
		const mapped = Promise.resolve(raw).then(result => {
			const projected = project(result);
			if (this.core.recoveryRequired && !this.terminal && this.stateValue === 'ready')
				void this.disconnect().catch(() => {
				                             });
			return projected;
		});
		const item = {raw, mapped};
		this.requests.add(item);
		mapped.then(
		    () => {
			    this.requests.delete(item);
		    },
		    () => {
			    this.requests.delete(item);
		    });
		return Object.assign(
		    mapped,
		    {requestId : raw.requestId, session : raw.session, cancel : () => raw.cancel()});
	}
}

async function connect(mode: SessionMode, options: StaticSessionOptions|DynamicSessionOptions):
    Promise<SessionOwner>
{
	const owner = new SessionOwner(mode, options);
	try { await owner.reconnect({signal : options.signal}); }
	catch (cause) {
		try { await owner.close(); } catch { /* The profile retains its unresolved close fence. */ }
		throw cause;
	}
	return owner;
}

// Runtime encapsulation matters for JavaScript callers too: TypeScript private
// properties alone would expose the core and permit untracked submissions.
function facade(owner: SessionOwner): object
{
	const publicSession = {
		get mode() { return owner.mode; },
		get state() { return owner.state; },
		get ready() { return owner.ready; },
		get session() { return owner.session; },
		get fingerprint() { return owner.fingerprint; },
		get recoveryRequired() { return owner.recoveryRequired; },
		get recoveryGeneration() { return owner.recoveryGeneration; },
		get lastError() { return owner.lastError; },
		get capabilities() { return owner.capabilities; },
		subscribe: owner.subscribe.bind(owner),
		disconnect: owner.disconnect.bind(owner),
		reconnect: owner.reconnect.bind(owner),
		close: owner.close.bind(owner),
		read: owner.read.bind(owner),
		write: owner.write.bind(owner),
		writePreserving: owner.writePreserving.bind(owner),
		command: owner.command.bind(owner),
		service: owner.service.bind(owner),
		readMany: owner.readMany.bind(owner),
		batch: owner.batch.bind(owner)
	};
	Object.defineProperty(publicSession, owner.mode === 'static' ? 'metadata' : 'schema', {
		enumerable: true, get: () => owner.mode === 'static' ? owner.metadata : owner.schema
	});
	if (owner.mode === 'static')
		Object.defineProperty(publicSession, 'sdk', {enumerable: true, get: () => owner.sdk});
	return Object.freeze(publicSession);
}
export async function connectStatic(options: StaticSessionOptions): Promise<StaticConnectionSession>
{
	return facade(await connect('static', options)) as StaticConnectionSession;
}
export async function connectDynamic(options: DynamicSessionOptions): Promise<DynamicConnectionSession>
{
	return facade(await connect('dynamic', options)) as DynamicConnectionSession;
}
