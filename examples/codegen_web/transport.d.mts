// Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
export type Exchange = (packet: Uint8Array) => Uint8Array | Promise<Uint8Array>;
export type Operation = 'FieldRead' | 'FieldWrite' | 'Command' | 'Service';
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
    readonly resourceStatus?: number;
    readonly dispatchStatus?: number;
    readonly endpointStatus?: number;
    constructor(message: string, details?: object);
}
export declare function rpcExchange(exchange: Exchange, operation: Operation, id: number,
    payload: Uint8Array, options: RpcOptions): Promise<RpcOutcome>;
export declare function rpcInvoke<T>(exchange: Exchange,
    binding: {readonly operation: 'FieldRead'; readonly id: number; decode(input: Uint8Array): T},
    value: undefined, options: RpcOptions): Promise<RpcOutcome & {readonly value: T}>;
export declare function rpcInvoke<Q, R>(exchange: Exchange,
    binding: {readonly operation: 'Service'; readonly id: number; encode(value: Q): Uint8Array; decodeResponse(input: Uint8Array): R},
    value: Q, options: RpcOptions): Promise<RpcOutcome & {readonly value: R}>;
export declare function rpcInvoke<Q>(exchange: Exchange,
    binding: {readonly operation: 'FieldWrite' | 'Command'; readonly id: number; encode(value: Q): Uint8Array},
    value: Q, options: RpcOptions): Promise<RpcOutcome & {readonly value: undefined}>;
