// Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
export {RpcError, rpcExchange, rpcInvoke} from '../../lib/telemetry/client/web/Client.js';
export type {Operation, RpcOptions, RpcOutcome} from '../../lib/telemetry/client/web/Client.js';
// Preserve the original helper's Uint8Array reply contract. The async backend
// additionally accepts ArrayBuffer/DataView replies through its own Exchange.
export type Exchange = (packet: Uint8Array) => Uint8Array | Promise<Uint8Array>;
