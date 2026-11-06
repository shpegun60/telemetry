/* Optional owned connection coordination over one existing client FIFO. MIT.
 * Authors: Ruslan Kovtun (shpegun60), codexAi.
 */
import { DeviceClient, Status } from '../lib/client/web/Client.js';
import { DynamicClient } from './DynamicClient.js';
import { matchesDescriptorHeader, parseDescriptor } from './telemetry.js';
export class ConnectionError extends Error {
    code;
    constructor(code, message, cause) {
        super(message, { cause });
        this.code = code;
        this.name = 'ConnectionError';
    }
}
// Identity-scoped exclusivity, not a global registry of arbitrary device URLs.
const profiles = new WeakMap();
const copy = (input) => new Uint8Array(input instanceof ArrayBuffer ? new Uint8Array(input)
    : new Uint8Array(input.buffer, input.byteOffset, input.byteLength));
const validGeneration = (value) => Number.isInteger(value) && value > 0 && value <= 0xffffffff;
const error = (code, message, cause) => cause instanceof ConnectionError ? cause : new ConnectionError(code, message, cause);
class ForwardingBackend {
    delegate;
    supports(operation) {
        return this.delegate?.supports?.(operation) ??
            (!!this.delegate && operation !== 'ReadMany' && operation !== 'Batch');
    }
    execute(request, context) {
        const target = this.delegate;
        return target ? target.execute(request, context)
            : Promise.reject(new Error('No admitted connection backend'));
    }
    confirmSessionClosed(generation) {
        const target = this.delegate;
        return !!target && (target.confirmSessionClosed
            ? target.confirmSessionClosed(generation) === true : true);
    }
}
class SessionOwner {
    mode;
    metadata;
    profile;
    record;
    sdk;
    limits;
    forwarder = new ForwardingBackend();
    core;
    listeners = new Set();
    requests = new Set();
    fences = new Set();
    active;
    stateValue = 'disconnected';
    errorValue;
    fingerprintValue;
    terminal = false;
    revision = 0;
    openAbort;
    openWork;
    opening;
    disconnecting;
    closing;
    closedRecovery;
    constructor(mode, options) {
        if (!options || !options.transport || typeof options.transport.open !== 'function')
            throw new ConnectionError('InvalidArgument', 'A connection profile is required');
        this.mode = mode;
        this.profile = options.transport;
        this.record = profiles.get(this.profile) ?? { highWater: 0 };
        profiles.set(this.profile, this.record);
        if (mode === 'static') {
            const selected = options;
            if (!selected.sdk || typeof selected.sdk.matchesHeader !== 'function')
                throw new ConnectionError('InvalidArgument', 'Generated sdk.matchesHeader is required');
            this.sdk = selected.sdk;
            this.metadata = selected.metadata;
        }
        else {
            this.limits = options.limits;
        }
        const coreOptions = {
            backend: this.forwarder, clock: options.clock, timeoutMs: options.timeoutMs,
            maxPending: options.maxPending, maxPayloadBytes: options.maxPayloadBytes
        };
        this.core = mode === 'static'
            ? new DeviceClient({ ...coreOptions,
                matchesHeader: bytes => this.sdk.matchesHeader(bytes) })
            : new DynamicClient(coreOptions);
    }
    get state() { return this.stateValue; }
    get ready() { return this.stateValue === 'ready' && this.core.ready; }
    get session() { return this.core.session; }
    get fingerprint() { return this.fingerprintValue; }
    get recoveryRequired() {
        return this.closedRecovery?.required ??
            (this.core.recoveryRequired || this.stateValue === 'recovery-required');
    }
    get recoveryGeneration() {
        return this.closedRecovery?.generation ?? this.core.recoveryGeneration;
    }
    get lastError() { return this.errorValue; }
    get schema() {
        return this.ready && this.core instanceof DynamicClient ? this.core.schema : undefined;
    }
    get capabilities() {
        return this.ready && this.active ? Object.freeze({
            readMany: this.active.readMany?.slice(), batch: this.active.batch?.slice()
        }) : undefined;
    }
    subscribe(listener) {
        if (typeof listener !== 'function')
            throw new TypeError('A session listener is required');
        this.listeners.add(listener);
        this.deliver(listener, this.snapshot());
        return () => { this.listeners.delete(listener); };
    }
    snapshot() {
        return Object.freeze({ mode: this.mode, state: this.state, ready: this.ready,
            session: this.session, fingerprint: this.fingerprint,
            recoveryRequired: this.recoveryRequired,
            recoveryGeneration: this.recoveryGeneration,
            lastError: this.lastError });
    }
    deliver(listener, value) {
        try {
            listener(value);
        }
        catch { /* Observers cannot change execution outcomes. */ }
    }
    notify() {
        const revision = this.revision, value = this.snapshot();
        for (const listener of [...this.listeners]) {
            if (revision !== this.revision)
                break;
            if (this.listeners.has(listener))
                this.deliver(listener, value);
        }
    }
    current(revision, signal) {
        if (this.terminal)
            throw new ConnectionError('Closed', 'The session is closed');
        if (signal.aborted || revision !== this.revision)
            throw new ConnectionError('Aborted', 'The connection attempt was retired', signal.reason);
    }
    claim() {
        if (this.record.owner && this.record.owner !== this)
            throw new ConnectionError('Busy', 'This connection profile already has an owner');
        this.record.owner = this;
    }
    closeLease(lease) {
        if (!lease.local) {
            let resolve, reject;
            lease.local = new Promise((yes, no) => { resolve = yes; reject = no; });
            this.fences.add(lease.local);
            lease.local.catch(() => { });
            try {
                Promise.resolve(lease.source.close()).then(() => {
                    lease.locallyClosed = true;
                    this.fences.delete(lease.local);
                    resolve();
                }, reject);
            }
            catch (cause) {
                reject(cause);
            }
        }
        return lease.local;
    }
    retire(lease) {
        if (!this.record.retired && typeof lease.peer === 'string' && lease.peer &&
            validGeneration(lease.session))
            this.record.retired = { owner: this, lease,
                admitted: this.core.session === lease.session,
                confirmed: false };
        void this.closeLease(lease);
    }
    async requestsFinished() {
        while (this.requests.size)
            await Promise.allSettled([...this.requests].map(item => item.mapped));
    }
    async localFences() {
        await Promise.all([...this.fences]);
    }
    invalidate() {
        if (this.core instanceof DynamicClient)
            this.core.invalidate();
        else
            this.core.invalidateAdmission();
        for (const request of [...this.requests])
            request.raw.cancel();
    }
    disconnect() {
        ++this.revision;
        this.openAbort?.abort();
        if (this.disconnecting)
            return this.disconnecting;
        let resolve, reject;
        const pending = new Promise((yes, no) => { resolve = yes; reject = no; });
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
                await open?.catch(() => { });
                await this.requestsFinished();
                await this.localFences();
                if (!this.terminal)
                    this.stateValue = this.core.recoveryRequired ? 'recovery-required' : 'disconnected';
                resolve();
            }
            catch (cause) {
                this.errorValue = error('CleanupFailed', 'The owned transport has not closed', cause);
                if (!this.terminal)
                    this.stateValue = 'recovery-required';
                reject(this.errorValue);
            }
            finally {
                if (this.disconnecting === pending)
                    this.disconnecting = undefined;
                this.notify();
            }
        })();
        return pending;
    }
    reconnect(options = {}) {
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
            try {
                await work;
            }
            finally {
                if (this.openWork === work)
                    this.openWork = undefined;
            }
        });
        this.opening = pending;
        pending.then(() => { if (this.opening === pending)
            this.opening = undefined; }, () => { if (this.opening === pending)
            this.opening = undefined; });
        return pending;
    }
    async openConnection(external) {
        const controller = new AbortController(), revision = ++this.revision;
        this.openAbort = controller;
        const aborted = () => controller.abort(external?.reason);
        external?.addEventListener('abort', aborted, { once: true });
        if (external?.aborted)
            aborted();
        let candidate;
        try {
            this.current(revision, controller.signal);
            this.stateValue = 'connecting';
            this.errorValue = undefined;
            this.notify();
            this.current(revision, controller.signal);
            const previous = this.record.retired;
            const lease = await this.profile.open({ mode: this.mode, signal: controller.signal,
                previous: previous ? Object.freeze({ peer: previous.lease.peer,
                    session: previous.lease.session }) : undefined,
                onDisconnect: reason => {
                    if (revision !== this.revision || this.terminal)
                        return;
                    this.errorValue = error('TransportLost', 'The owned connection ended', reason);
                    if (this.active)
                        void this.disconnect().catch(() => { });
                    else
                        controller.abort(reason);
                } });
            // Capture a returned lease before checking attempt cancellation so it is never orphaned.
            candidate = { source: lease, peer: '', session: 0,
                backend: undefined, header: new Uint8Array(0),
                locallyClosed: false };
            Object.assign(candidate, { peer: lease.peer, session: lease.session,
                backend: lease.backend, header: copy(lease.header),
                descriptor: lease.descriptor && copy(lease.descriptor),
                readMany: lease.readMany && copy(lease.readMany),
                batch: lease.batch && copy(lease.batch),
                supersedes: lease.supersedes && Object.freeze({
                    peer: lease.supersedes.peer, session: lease.supersedes.session
                }),
                locallyClosed: false });
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
            let schema;
            if (this.core instanceof DynamicClient) {
                if (!candidate.descriptor)
                    throw new ConnectionError('BootstrapFailed', 'Dynamic connection requires a Descriptor');
                schema = parseDescriptor(candidate.descriptor, this.limits);
                if (!matchesDescriptorHeader(schema, candidate.header))
                    throw new ConnectionError('SchemaMismatch', 'Observed header does not match the Descriptor');
            }
            else if (!this.sdk.matchesHeader(candidate.header)) {
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
                ? this.core.admit(schema, candidate.header, candidate.session) === Status.Ok
                : this.core.openSession(candidate.session, candidate.header);
            if (!admitted)
                throw new ConnectionError('AdmissionFailed', 'Client admission refused the bootstrap');
            this.current(revision, controller.signal);
            this.fingerprintValue = new DataView(candidate.header.buffer, candidate.header.byteOffset, candidate.header.byteLength).getBigUint64(16, true);
            this.stateValue = 'ready';
            this.notify();
            this.current(revision, controller.signal);
        }
        catch (cause) {
            if (candidate) {
                if (this.active === candidate) {
                    this.invalidate();
                    this.active = undefined;
                }
                this.retire(candidate);
                try {
                    await this.closeLease(candidate);
                }
                catch (cleanup) {
                    this.errorValue = error('CleanupFailed', 'Candidate transport did not close', cleanup);
                    if (!this.terminal)
                        this.stateValue = 'recovery-required';
                    this.notify();
                    throw this.errorValue;
                }
            }
            const failure = error(controller.signal.aborted ? 'Aborted' : 'BootstrapFailed', 'Connection bootstrap failed', cause);
            if (!this.terminal && revision === this.revision) {
                this.errorValue = failure;
                this.stateValue = failure.code === 'RecoveryRequired' || this.core.recoveryRequired
                    ? 'recovery-required' : 'disconnected';
                this.notify();
            }
            throw failure;
        }
        finally {
            external?.removeEventListener('abort', aborted);
            if (this.openAbort === controller)
                this.openAbort = undefined;
        }
    }
    confirm(retirement) {
        if (!retirement.lease.locallyClosed || this.requests.size || this.core.pendingCount)
            return false;
        if (!retirement.admitted)
            return true;
        return this.core instanceof DynamicClient
            ? this.core.confirmQuiescence({ session: retirement.lease.session,
                txReleased: true, oldFramesExcluded: true,
                remoteSessionClosed: true })
            : this.core.confirmSessionClosed(retirement.lease.session);
    }
    close() {
        if (this.closing)
            return this.closing;
        let resolve, reject;
        const pending = new Promise((yes, no) => { resolve = yes; reject = no; });
        this.closing = pending;
        this.terminal = true;
        this.stateValue = 'closed';
        const disconnected = this.disconnect();
        this.core.close();
        this.closedRecovery = { required: this.core.recoveryRequired,
            generation: this.core.recoveryGeneration };
        void (async () => {
            try {
                await disconnected;
                await this.requestsFinished();
                await this.localFences();
                this.closedRecovery = { required: this.core.recoveryRequired,
                    generation: this.core.recoveryGeneration };
                if (this.record.owner === this)
                    this.record.owner = undefined;
                this.stateValue = 'closed';
                this.notify();
                resolve();
            }
            catch (cause) {
                this.closedRecovery = { required: true,
                    generation: this.core.recoveryGeneration || this.core.session };
                reject(cause);
            }
        })();
        return pending;
    }
    read(endpoint, timeoutMs) {
        return this.submit('FieldRead', endpoint, undefined, timeoutMs);
    }
    write(endpoint, value, timeoutMs) {
        return this.submit('FieldWrite', endpoint, value, timeoutMs);
    }
    writePreserving(endpoint, value, originalPayload, timeoutMs) {
        return this.submit('FieldWrite', endpoint, value, timeoutMs, originalPayload, true);
    }
    command(endpoint, value, timeoutMs) {
        return this.submit('Command', endpoint, value, timeoutMs);
    }
    service(endpoint, value, timeoutMs) {
        return this.submit('Service', endpoint, value, timeoutMs);
    }
    refused(operation, status) {
        const rawResult = Object.freeze({ status, operation,
            session: this.session, requestId: 0, dispatch: 0, endpointStatus: 0,
            succeeded: false, accepted: false, executed: false,
            payload: new Uint8Array(0), value: undefined });
        return Object.assign(Promise.resolve(Object.freeze({ ...rawResult,
            valueStatus: this.ready ? 'NoValue' : 'Stale', rawResult })), { requestId: 0, session: rawResult.session, cancel: () => false });
    }
    submit(operation, endpoint, value, timeoutMs, originalPayload, preserving = false) {
        if (!this.ready)
            return this.refused(operation, this.terminal ? Status.Closed :
                this.recoveryRequired ? Status.RecoveryRequired : Status.NotAdmitted);
        const revision = this.revision;
        let seed;
        if (preserving) {
            // Own the seed before binding accessors or DTO conversion can reenter/mutate it.
            if (!(originalPayload instanceof ArrayBuffer) && !ArrayBuffer.isView(originalPayload))
                return this.refused(operation, Status.InvalidArgument);
            try {
                seed = copy(originalPayload);
            }
            catch {
                return this.refused(operation, Status.InvalidArgument);
            }
        }
        let raw;
        if (this.core instanceof DynamicClient) {
            if (typeof endpoint !== 'number' || !Number.isInteger(endpoint) || endpoint < 0 || endpoint > 0xffffffff)
                return this.refused(operation, Status.InvalidArgument);
            const binding = this.core.bind(operation, endpoint);
            if (binding.status !== Status.Ok)
                return this.refused(operation, binding.status);
            raw = operation === 'FieldRead' ? this.core.readField(binding.endpoint, timeoutMs)
                : operation === 'FieldWrite'
                    ? preserving
                        ? this.core.writeFieldPreserving(binding.endpoint, value, seed, timeoutMs)
                        : this.core.writeField(binding.endpoint, value, timeoutMs)
                    : operation === 'Command' ? this.core.command(binding.endpoint, value, timeoutMs)
                        : this.core.service(binding.endpoint, value, timeoutMs);
        }
        else {
            if (preserving) {
                try {
                    if (!endpoint || (typeof endpoint !== 'object' && typeof endpoint !== 'function'))
                        return this.refused(operation, Status.InvalidArgument);
                    const field = endpoint;
                    const write = 'read' in field ? field.write : field;
                    if (!write)
                        return this.refused(operation, Status.ReadOnly);
                    const { fingerprint, operation: declared, id, requestBytes, responseBytes } = write;
                    if (declared !== 'FieldWrite')
                        return this.refused(operation, Status.InvalidArgument);
                    const encode = write.encodePreserving;
                    if (typeof encode !== 'function')
                        return this.refused(operation, Status.Unsupported);
                    endpoint = { fingerprint, operation: declared, id, requestBytes, responseBytes,
                        encode: (edited) => encode.call(write, edited, seed) };
                }
                catch {
                    return this.refused(operation, Status.InvalidArgument);
                }
            }
            raw = operation === 'FieldRead'
                ? this.core.read(endpoint, timeoutMs)
                : operation === 'FieldWrite'
                    ? this.core.write(endpoint, value, timeoutMs)
                    : operation === 'Command'
                        ? this.core.command(endpoint, value, timeoutMs)
                        : this.core.service(endpoint, value, timeoutMs);
        }
        const mapped = raw.then(result => {
            const dynamic = this.mode === 'dynamic' ? result : undefined;
            const rawResult = dynamic ? dynamic.operation : result;
            const current = this.ready && revision === this.revision && rawResult.session === this.session;
            const valueStatus = !current ? 'Stale' : dynamic ? dynamic.valueStatus
                : rawResult.status === Status.CodecError ? 'InvalidPayload'
                    : rawResult.succeeded && rawResult.value !== undefined ? 'Ok' : 'NoValue';
            const projected = Object.freeze({ ...rawResult, rawResult, valueStatus,
                value: valueStatus === 'Ok' ? dynamic ? dynamic.value : rawResult.value : undefined });
            if (this.core.recoveryRequired && !this.terminal && this.stateValue === 'ready')
                void this.disconnect().catch(() => { });
            return projected;
        });
        const item = { raw, mapped };
        this.requests.add(item);
        mapped.then(() => { this.requests.delete(item); }, () => { this.requests.delete(item); });
        return Object.assign(mapped, { requestId: raw.requestId, session: raw.session,
            cancel: () => raw.cancel() });
    }
}
async function connect(mode, options) {
    const owner = new SessionOwner(mode, options);
    try {
        await owner.reconnect({ signal: options.signal });
    }
    catch (cause) {
        try {
            await owner.close();
        }
        catch { /* The profile retains its unresolved close fence. */ }
        throw cause;
    }
    return owner;
}
// Runtime encapsulation matters for JavaScript callers too: TypeScript private
// properties alone would expose the core and permit untracked submissions.
function facade(owner) {
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
        service: owner.service.bind(owner)
    };
    Object.defineProperty(publicSession, owner.mode === 'static' ? 'metadata' : 'schema', {
        enumerable: true, get: () => owner.mode === 'static' ? owner.metadata : owner.schema
    });
    if (owner.mode === 'static')
        Object.defineProperty(publicSession, 'sdk', { enumerable: true, get: () => owner.sdk });
    return Object.freeze(publicSession);
}
export async function connectStatic(options) {
    return facade(await connect('static', options));
}
export async function connectDynamic(options) {
    return facade(await connect('dynamic', options));
}
