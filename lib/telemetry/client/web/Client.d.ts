export declare const Status: Readonly<{
    readonly Ok: 0;
    readonly QueueFull: 1;
    readonly PayloadFull: 2;
    readonly NotAdmitted: 3;
    readonly ReadOnly: 4;
    readonly Closed: 5;
    readonly Cancelled: 6;
    readonly Timeout: 7;
    readonly SessionLost: 8;
    readonly OutcomeUnknown: 9;
    readonly MalformedReply: 10;
    readonly TransportError: 11;
    readonly CodecError: 12;
    readonly BufferTooSmall: 13;
    readonly InvalidArgument: 14;
    readonly Busy: 15;
    readonly RecoveryRequired: 16;
    readonly IdsExhausted: 17;
}>;
export type ClientStatus = typeof Status[keyof typeof Status];
export type Operation = 'FieldWrite' | 'Command' | 'Service' | 'FieldRead';
export declare const Operations: Readonly<{
    readonly FieldWrite: 1;
    readonly Command: 2;
    readonly Service: 3;
    readonly FieldRead: 4;
}>;
export type ByteInput = ArrayBuffer | ArrayBufferView;
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
    readonly value: T | undefined;
}
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
}
export interface ExchangeContext {
    readonly session: number;
    readonly requestId: number;
    readonly signal: AbortSignal;
}
export type Exchange = (packet: Uint8Array, context?: ExchangeContext) => ByteInput | Promise<ByteInput>;
export interface BackendContext extends ExchangeContext {
    readonly clock: Clock;
    handoff(mayExecute: boolean): boolean;
}
export interface Backend {
    execute(request: WireRequest, context: BackendContext): Promise<WireResult>;
}
export interface ClientOptions {
    readonly backend: Backend;
    readonly matchesHeader: (header: ByteInput) => boolean;
    readonly maxPending?: number;
    readonly maxPayloadBytes?: number;
    readonly timeoutMs?: number;
    readonly clock?: Clock;
}
export declare class DeviceClient {
    private readonly backend;
    private readonly identity;
    private readonly clock;
    private readonly limit;
    private readonly byteLimit;
    private readonly timeout;
    private pending;
    private active?;
    private epoch;
    private fingerprint;
    private lastEpoch;
    private nextId;
    private payloadBytes;
    private recoveryEpoch;
    private stopped;
    private admitted;
    private timer?;
    private scheduled;
    private pumping;
    constructor(options: ClientOptions);
    get session(): number;
    get ready(): boolean;
    get recoveryRequired(): boolean;
    get recoveryGeneration(): number;
    get pendingCount(): number;
    get snapshotBytes(): number;
    openSession(generation: number, header: ByteInput): boolean;
    confirmSessionClosed(generation: number): boolean;
    resetSession(): void;
    close(): void;
    read<T>(field: Field<T> | ReadBinding<T>, timeoutMs?: number): RequestPromise<T>;
    write<T>(field: Field<T> | WriteBinding<T>, value: T, timeoutMs?: number): RequestPromise<undefined>;
    command<T>(binding: CommandBinding<T>, value: T, timeoutMs?: number): RequestPromise<undefined>;
    service<Q, R>(binding: ServiceBinding<Q, R>, value: Q, timeoutMs?: number): RequestPromise<R>;
    private refused;
    private enqueue;
    private makeResult;
    private schedule;
    process(): void;
    private cancel;
    private cancelAll;
    private finish;
    private armTimer;
}
export type EncodedSend = (packet: Uint8Array, context: ExchangeContext) => void | Promise<void>;
export declare class EncodedBackend implements Backend {
    private readonly send;
    private waiting?;
    constructor(send: EncodedSend);
    static fromExchange(exchange: Exchange): EncodedBackend;
    execute(request: WireRequest, context: BackendContext): Promise<WireResult>;
    receive(input: ByteInput): boolean;
}
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
export declare class RpcError extends Error {
    readonly clientStatus?: ClientStatus;
    readonly resourceStatus?: number;
    readonly dispatchStatus?: number;
    readonly endpointStatus?: number;
    constructor(message: string, details?: object);
}
export declare function rpcExchange(exchange: (packet: Uint8Array) => Uint8Array | Promise<Uint8Array>, operation: Operation, id: number, payload: Uint8Array, options: RpcOptions): Promise<RpcOutcome>;
export declare function rpcInvoke<R>(exchange: (packet: Uint8Array) => Uint8Array | Promise<Uint8Array>, binding: Pick<ReadBinding<R>, 'operation' | 'id' | 'decode'>, value: undefined, options: RpcOptions): Promise<RpcOutcome & {
    readonly value: R;
}>;
export declare function rpcInvoke<Q, R>(exchange: (packet: Uint8Array) => Uint8Array | Promise<Uint8Array>, binding: Pick<ServiceBinding<Q, R>, 'operation' | 'id' | 'encode' | 'decodeResponse'>, value: Q, options: RpcOptions): Promise<RpcOutcome & {
    readonly value: R;
}>;
export declare function rpcInvoke<Q>(exchange: (packet: Uint8Array) => Uint8Array | Promise<Uint8Array>, binding: Pick<WriteBinding<Q> | CommandBinding<Q>, 'operation' | 'id' | 'encode'>, value: Q, options: RpcOptions): Promise<RpcOutcome & {
    readonly value: undefined;
}>;
export interface RpcBackendOptions extends Omit<RpcOptions, 'transactionId'> {
    readonly ackTimeoutMs?: number;
    readonly pollIntervalMs?: number;
    readonly maxRetries?: number;
}
export declare class RpcBackend implements Backend {
    private readonly exchange;
    private readonly options;
    private executing?;
    constructor(exchange: Exchange, options?: RpcBackendOptions);
    execute(request: WireRequest, context: BackendContext): Promise<WireResult>;
    private run;
    private delay;
    private attempt;
}
