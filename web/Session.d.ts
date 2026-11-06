import { type Backend, type ByteInput, type Clock, type BatchItem, type BatchItems, type BatchOperation, type CommandBinding, type Field, type ReadBinding, type ReadManyField, type Result, type ServiceBinding, type WriteBinding } from '../lib/client/web/Client.js';
import { type Limits, type RuntimeSchema } from './telemetry.js';
export type SessionMode = 'static' | 'dynamic';
export type SessionState = 'connecting' | 'bootstrapping' | 'ready' | 'disconnecting' | 'disconnected' | 'recovery-required' | 'closed';
export type ConnectionErrorCode = 'InvalidArgument' | 'Busy' | 'Closed' | 'Aborted' | 'BootstrapFailed' | 'SchemaMismatch' | 'RecoveryRequired' | 'CleanupFailed' | 'AdmissionFailed' | 'TransportLost';
export declare class ConnectionError extends Error {
    readonly code: ConnectionErrorCode;
    constructor(code: ConnectionErrorCode, message: string, cause?: unknown);
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
export type SessionValueStatus = 'Ok' | 'NoValue' | 'InvalidPayload' | 'Stale';
export type SessionResult<T = undefined> = Readonly<Omit<Result<T>, 'value'> & {
    readonly valueStatus: SessionValueStatus;
    readonly value: T | undefined;
    /** Unmodified common operation Result, including historical checked facts. */
    readonly rawResult: Result<T>;
}>;
export interface SessionOperationRequest<R> extends Promise<R> {
    readonly requestId: number;
    readonly session: number;
    cancel(): boolean;
}
export interface SessionRequest<T> extends SessionOperationRequest<SessionResult<T>> {
}
export interface SessionReadManyItem<T = unknown> {
    readonly id: number;
    readonly status: 0 | 1 | 2 | 3 | 4;
    readonly payload: Uint8Array;
    readonly valueStatus: SessionValueStatus;
    readonly value: T | undefined;
}
export type SessionBatchItem<T = unknown> = Readonly<Omit<Result<T>, 'value'> & {
    readonly id: number;
    readonly valueStatus: SessionValueStatus;
    readonly value: T | undefined;
}>;
export type SessionItemsResult<I extends readonly unknown[]> = Readonly<Omit<SessionResult<undefined>, 'rawResult'> & {
    readonly rawResult: Result<unknown>;
    readonly items: I | undefined;
}>;
type ReadManyValue<F> = F extends {
    readonly read: ReadBinding<infer T>;
} ? T : F extends ReadBinding<infer T> ? T : never;
export type SessionReadManyItems<F extends readonly ReadManyField<unknown>[]> = {
    readonly [K in keyof F]: SessionReadManyItem<ReadManyValue<F[K]>>;
};
export type SessionBatchItems<I extends readonly BatchItem<unknown>[]> = {
    readonly [K in keyof I]: SessionBatchItem<BatchItems<I>[K]['value']>;
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
    readonly fingerprint: bigint | undefined;
    readonly recoveryRequired: boolean;
    readonly recoveryGeneration: number;
    readonly lastError: ConnectionError | undefined;
}
export interface ConnectionSession extends SessionSnapshot {
    /** Called immediately with the current immutable snapshot. Listener errors are isolated.
     * A nonready snapshot revokes submission; it is not an outcome/projection drain fence. */
    subscribe(listener: (snapshot: SessionSnapshot) => void): () => void;
    /** Copies of unvalidated discovery bytes; absent outside the ready state. */
    readonly capabilities: DiscoveredCapabilities | undefined;
    /** Settles managed result projections and actual local close; retains remote obligations.
     * Keep pending request/outcome owners alive until their request Promises resolve. */
    disconnect(): Promise<void>;
    reconnect(options?: {
        readonly signal?: AbortSignal;
    }): Promise<void>;
    /** Terminal logical shutdown and actual LOCAL close. Remote recovery facts survive. */
    close(): Promise<void>;
}
export interface StaticConnectionSession extends ConnectionSession {
    /** The original generated SDK module; no client or connection ownership. */
    readonly sdk: StaticSdk;
    readonly metadata: unknown;
    read<T>(field: Field<T> | ReadBinding<T>, timeoutMs?: number): SessionRequest<T>;
    write<T>(field: Field<T> | WriteBinding<T>, value: T, timeoutMs?: number): SessionRequest<undefined>;
    /** Edit using the original FieldRead bytes. Requires generated --wire-editing support. */
    writePreserving<T>(field: Field<T> | WriteBinding<T>, value: T, originalPayload: ByteInput, timeoutMs?: number): SessionRequest<undefined>;
    command<T>(endpoint: CommandBinding<T>, value: T, timeoutMs?: number): SessionRequest<undefined>;
    service<Q, R>(endpoint: ServiceBinding<Q, R>, value: Q, timeoutMs?: number): SessionRequest<R>;
    readMany<const F extends readonly ReadManyField<unknown>[]>(capability: ByteInput, fields: F, timeoutMs?: number): SessionOperationRequest<SessionItemsResult<SessionReadManyItems<F>>>;
    batch<const I extends readonly BatchItem<unknown>[]>(capability: ByteInput, items: I, timeoutMs?: number): SessionOperationRequest<SessionItemsResult<SessionBatchItems<I>>>;
}
export interface DynamicConnectionSession extends ConnectionSession {
    readonly schema: RuntimeSchema | undefined;
    read(id: number, timeoutMs?: number): SessionRequest<unknown>;
    write(id: number, value: unknown, timeoutMs?: number): SessionRequest<undefined>;
    writePreserving(id: number, value: unknown, originalPayload: ByteInput, timeoutMs?: number): SessionRequest<undefined>;
    command(id: number, value?: unknown, timeoutMs?: number): SessionRequest<undefined>;
    service(id: number, value?: unknown, timeoutMs?: number): SessionRequest<unknown>;
    readMany(capability: ByteInput, ids: readonly number[], timeoutMs?: number): SessionOperationRequest<SessionItemsResult<readonly SessionReadManyItem[]>>;
    batch(capability: ByteInput, items: readonly SessionBatchOperation[], timeoutMs?: number): SessionOperationRequest<SessionItemsResult<readonly SessionBatchItem[]>>;
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
export declare function connectStatic(options: StaticSessionOptions): Promise<StaticConnectionSession>;
export declare function connectDynamic(options: DynamicSessionOptions): Promise<DynamicConnectionSession>;
export {};
