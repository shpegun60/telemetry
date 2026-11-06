// Owned bootstrap for the Telemetry UX reference WebSocket server. MIT.
import {ConnectionError} from './Session.js';
import {bindWebSocket} from './WebSocketTransport.js';
import {defaultLimits} from './telemetry.js';
export {bindWebSocket} from './WebSocketTransport.js';

const protocol = 'telemetry.encoded.v1';
const realClock = {
	now : () => performance.now(),
	setTimeout : (fn, ms) => globalThis.setTimeout(fn, ms),
	clearTimeout : handle => globalThis.clearTimeout(handle),
};

function limit(value, minimum, maximum, name)
{
	if (!Number.isSafeInteger(value) || value < minimum || value > maximum)
		throw new ConnectionError('InvalidArgument', `Invalid ${name}`);
	return value;
}

/** Reference-server profile, not a generic HTTP/authentication bootstrap protocol. */
export function demoWebSocket(baseUrl, options = {})
{
	let base;
	try {
		base = new URL(baseUrl);
	} catch (cause) {
		throw new ConnectionError('InvalidArgument',
		                          'An absolute HTTP(S) demo server URL is required', cause);
	}
	if (!['http:', 'https:'].includes(base.protocol) || base.username || base.password)
		throw new ConnectionError('InvalidArgument',
		                          'Use an HTTP(S) demo origin without embedded credentials');
	const fetcher = options.fetch ?? globalThis.fetch;
	const Socket = options.WebSocket ?? globalThis.WebSocket;
	const clock = options.clock ?? realClock;
	const timeoutMs = limit(options.timeoutMs ?? 5000, 1, 0x7fffffff, 'timeoutMs');
	const descriptorLimit = limit(options.maxDescriptorBytes ?? defaultLimits.descriptorBytes, 64,
	                              defaultLimits.descriptorBytes, 'maxDescriptorBytes');
	const frameLimit = limit(options.maxFrameBytes ?? 65536, 32, 65536, 'maxFrameBytes');
	const bufferLimit =
	    limit(options.maxBufferedBytes ?? 65536, frameLimit, 0x7fffffff, 'maxBufferedBytes');
	const credentials = options.credentials ?? 'same-origin';
	const enableReadMany = options.enableReadMany ?? false,
	      enableBatch = options.enableBatch ?? false;
	if (typeof fetcher !== 'function' || typeof Socket !== 'function' || !clock ||
	    !['now', 'setTimeout', 'clearTimeout'].every(key => typeof clock[key] === 'function') ||
	    typeof enableReadMany !== 'boolean' || typeof enableBatch !== 'boolean' ||
	    !['omit', 'same-origin', 'include'].includes(credentials))
		throw new ConnectionError('InvalidArgument',
		                          'Invalid WebSocket profile dependencies or credentials');
	const origin = base.origin;
	let claimed = false;
	let highestSession = 0;

	return Object.freeze({
		async open(context) {
		    if (claimed)
			    throw new ConnectionError(
			        'Busy', 'This profile still owns an opening or closing connection');
		    if (!context || !['static', 'dynamic'].includes(context.mode) ||
		        !(context.signal instanceof AbortSignal) ||
		        typeof context.onDisconnect !== 'function')
			    throw new ConnectionError('InvalidArgument', 'Invalid profile open context');
		    const previous =
		        context.previous === undefined
		            ? undefined
		            : Object.freeze(
		                  {peer : context.previous.peer, session : context.previous.session});
		    if (previous && (typeof previous.peer !== 'string' || !previous.peer ||
		                     !Number.isInteger(previous.session) || previous.session < 1 ||
		                     previous.session > 0xffffffff))
			    throw new ConnectionError('InvalidArgument',
			                              'Invalid previous connection identity');
		    if (context.signal.aborted)
			    throw new ConnectionError('Aborted', 'Connection was aborted before bootstrap');
		    claimed = true;
		    const aborter = new AbortController();
		    const aborted = () =>
		        aborter.abort(new ConnectionError('Aborted', 'Connection bootstrap was aborted'));
		    context.signal.addEventListener('abort', aborted, {once : true});
		    let timer;
		    let socket, carrier, resolveClosed;
		    let closePromise, published = false, intentional = false, notified = false;
		    let closing;
		    const notify = reason => {
			    if (published && !intentional && !notified) {
				    notified = true;
				    context.onDisconnect(reason);
			    }
		    };
		    const onClosed = event => {
			    resolveClosed();
			    if (!published && !aborter.signal.aborted)
				    aborter.abort(new ConnectionError('BootstrapFailed',
				                                      'WebSocket closed during bootstrap'));
			    notify(Object.freeze({
				    kind : 'close',
				    code : event.code,
				    reason : event.reason,
				    wasClean : event.wasClean === true
			    }));
		    };
		    const close = () => {
			    if (closing)
				    return closing;
			    intentional = true;
			    closing = (async () => {
				    carrier?.dispose();
				    if (socket) {
					    if (socket.readyState !== 2 && socket.readyState !== 3)
						    socket.close();
					    if (socket.readyState === 3)
						    resolveClosed();
					    await closePromise;
					    socket.removeEventListener('close', onClosed);
				    }
				    claimed = false;
			    })();
			    return closing;
		    };
		    const check = () => {
			    if (aborter.signal.aborted)
				    throw aborter.signal.reason;
		    };
		    // Race every awaited host operation: injected fetch/reader implementations may
		    // ignore AbortSignal. Their late result cannot be admitted by this attempt.
		    const bounded = (promise, disposeLate) => new Promise((resolve, reject) => {
			    const cancelled = () => reject(aborter.signal.reason);
			    aborter.signal.addEventListener('abort', cancelled, {once : true});
			    Promise.resolve(promise)
			        .then(value => {
				        if (aborter.signal.aborted) {
					        disposeLate?.(value);
					        check();
				        }
				        return value;
			        })
			        .then(resolve, reject)
			        .finally(() => aborter.signal.removeEventListener('abort', cancelled));
			    if (aborter.signal.aborted)
				    cancelled();
		    });
		    const bytes = async (path, maximum, optional = false) => {
			    check();
			    const response = await bounded(
			        fetcher(new URL(path, origin).href,
			                {signal : aborter.signal, cache : 'no-store', credentials}),
			        late => {
				        void late.body?.cancel().catch(() => {
				                                       });
			        });
			    if (optional && response.status === 404) {
				    void response.body?.cancel().catch(() => {
				                                       });
				    return undefined;
			    }
			    if (!response.ok) {
				    void response.body?.cancel().catch(() => {
				                                       });
				    throw new ConnectionError('BootstrapFailed',
				                              `${path}: HTTP ${response.status}`);
			    }
			    if (!response.body?.getReader) {
				    const data = new Uint8Array(await bounded(response.arrayBuffer()));
				    if (data.byteLength > maximum)
					    throw new ConnectionError('BootstrapFailed',
					                              `${path}: response exceeds its byte limit`);
				    return data;
			    }
			    const reader = response.body.getReader();
			    const parts = [];
			    let size = 0, finished = false;
			    try {
				    for (;;) {
					    const part = await bounded(reader.read());
					    if (part.done) {
						    finished = true;
						    break;
					    }
					    if (!(part.value instanceof Uint8Array) ||
					        part.value.byteLength > maximum - size)
						    throw new ConnectionError('BootstrapFailed',
						                              `${path}: response exceeds its byte limit`);
					    parts.push(part.value.slice());
					    size += part.value.byteLength;
				    }
			    } finally {
				    if (!finished)
					    void reader.cancel().catch(() => {
					                               });
				    reader.releaseLock();
			    }
			    const data = new Uint8Array(size);
			    let offset = 0;
			    for (const part of parts) {
				    data.set(part, offset);
				    offset += part.byteLength;
			    }
			    return data;
		    };
		    try {
			    timer = clock.setTimeout(
			        () => aborter.abort(new ConnectionError(
			            'BootstrapFailed', 'Connection bootstrap deadline expired')),
			        timeoutMs);
			    const discoveryBytes = await bytes('/demo/transport', 4096);
			    const discovery =
			        JSON.parse(new TextDecoder('utf-8', {fatal : true}).decode(discoveryBytes));
			    const remote = new URL(discovery.url);
			    if (discovery.kind !== 'websocket' || discovery.protocol !== protocol ||
			        !['ws:', 'wss:'].includes(remote.protocol) ||
			        remote.hostname !== base.hostname ||
			        (base.protocol === 'https:' && remote.protocol !== 'wss:') || remote.username ||
			        remote.password)
				    throw new ConnectionError('BootstrapFailed',
				                              'Unsupported demo transport discovery');
			    const peer = `telemetry-demo:${origin}|${remote.href}|${protocol}`;
			    if (previous && previous.peer !== peer)
				    throw new ConnectionError(
				        'RecoveryRequired',
				        'Demo peer identity changed; old remote lease is not superseded');
			    check();
			    socket = new Socket(remote.href, protocol);
			    closePromise = new Promise(resolve => {
				    resolveClosed = resolve;
			    });
			    socket.addEventListener('close', onClosed);
			    socket.binaryType = 'arraybuffer';
			    const session = await bounded(new Promise((resolve, reject) => {
				    const cleanup = () => {
					    socket.removeEventListener('message', message);
					    socket.removeEventListener('error', error);
					    aborter.signal.removeEventListener('abort', cancelled);
				    };
				    const cancelled = () => {
					    cleanup();
					    reject(aborter.signal.reason);
				    };
				    const error = () => {
					    cleanup();
					    reject(new ConnectionError('BootstrapFailed',
					                               'WebSocket failed before the session greeting'));
				    };
				    const message = event => {
					    cleanup();
					    try {
						    if (socket.protocol !== protocol || typeof event.data !== 'string' ||
						        event.data.length > 256)
							    throw new Error(
							        'Expected a bounded session greeting and matching subprotocol');
						    const greeting = JSON.parse(event.data);
						    if (!greeting || Object.keys(greeting).length !== 1 ||
						        Object.keys(greeting)[0] !== 'session' ||
						        !Number.isInteger(greeting.session) || greeting.session < 1 ||
						        greeting.session > 0xffffffff)
							    throw new Error('Invalid session generation');
						    if (greeting.session <= highestSession ||
						        (previous && greeting.session <= previous.session))
							    throw new ConnectionError(
							        'RecoveryRequired',
							        'Demo session generation did not advance; server restart is not recovery proof');
						    // Retain an observed lease even if timeout wins before the awaiting continuation.
						    highestSession = greeting.session;
						    resolve(greeting.session);
					    } catch (cause) {
						    reject(cause instanceof ConnectionError
						               ? cause
						               : new ConnectionError('BootstrapFailed',
						                                     'Invalid demo session greeting',
						                                     cause));
					    }
				    };
				    socket.addEventListener('message', message);
				    socket.addEventListener('error', error);
				    aborter.signal.addEventListener('abort', cancelled, {once : true});
				    if (aborter.signal.aborted)
					    cancelled();
			    }));
			    const descriptor = context.mode === 'dynamic'
			                           ? await bytes('/descriptor.bin', descriptorLimit)
			                           : undefined;
			    const header =
			        descriptor ? descriptor.slice(0, 64) : await bytes('/schema-header.bin', 64);
			    if (header.byteLength !== 64 || (descriptor && descriptor.byteLength < 64))
				    throw new ConnectionError('BootstrapFailed',
				                              'Descriptor header must contain exactly 64 bytes');
			    const readMany = await bytes('/capabilities/read-many.bin', 24, true);
			    const batch = await bytes('/capabilities/batch.bin', 24, true);
			    if ((readMany && readMany.byteLength !== 24) || (batch && batch.byteLength !== 24))
				    throw new ConnectionError('BootstrapFailed',
				                              'Capability record must contain exactly 24 bytes');
			    check();
			    // Local support and remote capability admission remain independent.
			    carrier = bindWebSocket(socket, {
				    onDisconnect : notify,
				    maxFrameBytes : frameLimit,
				    maxBufferedBytes : bufferLimit,
				    encodedOptions : {readMany : enableReadMany, batch : enableBatch}
			    });
			    published = true;
			    return Object.freeze({
				    peer,
				    session,
				    header,
				    descriptor,
				    readMany,
				    batch,
				    backend : carrier.backend,
				    supersedes : previous,
				    close
			    });
		    } catch (cause) {
			    aborter.abort(cause);
			    try {
				    await close();
			    } catch (cleanup) {
				    throw new ConnectionError('CleanupFailed',
				                              'Bootstrap failed and the owned socket did not close',
				                              cleanup);
			    }
			    if (cause instanceof ConnectionError)
				    throw cause;
			    throw new ConnectionError('BootstrapFailed', 'Demo WebSocket bootstrap failed',
			                              cause);
		    } finally {
			    clock.clearTimeout(timer);
			    context.signal.removeEventListener('abort', aborted);
		    }
		}
	});
}
