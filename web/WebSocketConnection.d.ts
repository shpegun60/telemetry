/* Owned connection profile for the Telemetry UX reference server. MIT. */
import type {Clock} from '../lib/client/web/Client.js';
import type {ConnectionProfile} from './Session.js';
export {bindWebSocket} from './WebSocketTransport.js';
export type {WebSocketDisconnect, WebSocketTransportOptions, WebSocketTransport} from './WebSocketTransport.js';

export interface DemoWebSocketOptions {
	readonly fetch?: typeof globalThis.fetch;
	readonly WebSocket?: typeof globalThis.WebSocket;
	readonly clock?: Clock;
	/** One absolute deadline for discovery, greeting, Descriptor and capabilities; default 5000ms. */
	readonly timeoutMs?: number;
	/** Descriptor body byte limit; default and maximum runtime defaultLimits.descriptorBytes (4MiB). */
	readonly maxDescriptorBytes?: number;
	/** Complete Encoded frame limit; default 65536. */
	readonly maxFrameBytes?: number;
	/** Maximum buffered TX bytes; default 65536, at least maxFrameBytes. */
	readonly maxBufferedBytes?: number;
	readonly credentials?: RequestCredentials;
}

/** Owns discovery and the socket for the current Telemetry UX reference HTTP/WS server.
 * The server grants an exclusive monotonic session lease before its greeting.
 * A new greeting from that SAME peer is the profile-specific remote supersession proof.
 * Local socket close alone is never such proof. A server generation reset is refused.
 * Authentication, production access rules and arbitrary HTTP/WS servers are not inferred.
 */
export function demoWebSocket(baseUrl: string | URL, options?: DemoWebSocketOptions): ConnectionProfile;
