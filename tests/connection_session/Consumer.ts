// Public installed types: generated static inference and deliberately unknown Dynamic values. MIT.
import {connectStatic, connectDynamic, Status} from 'telemetry-sdk';
import {type ConnectionProfile, type SessionResult, type SessionRequest} from 'telemetry-sdk/session';
import {demoWebSocket, bindWebSocket} from 'telemetry-sdk/transports/websocket';
import {bindWebSocket as borrowedCarrier} from 'telemetry-sdk/transports/websocket/carrier';
import * as sdk from './generated/js/device.js';

export async function staticConsumer(transport: ConnectionProfile) {
    const session = await connectStatic({sdk, transport});
    const read: SessionRequest<sdk.control.types.Config> = session.read(sdk.control.fields.Config);
    const id: number = read.requestId, generation: number = read.session;
    const cancelled: boolean = read.cancel();
    const result: SessionResult<sdk.control.types.Config> = await read;
    const status: number = result.rawResult.status;
    if (result.valueStatus === 'Ok' && result.value !== undefined) {
        const number: number = result.value.number, large: bigint = result.value.large;
        void number; void large;
    }
    session.write(sdk.control.fields.Config, {number: 1, enabled: true, gains: [1, 2, 3],
        mode: -123, large: 1n, negative: -1n, f32: 0, f64: -0});
    session.command(sdk.control.commands.Reset, undefined);
    session.service(sdk.control.services.Echo, {number: 2, enabled: true, gains: [1, 2, 3],
        mode: -123, large: 1n, negative: -1n, f32: 0, f64: 0});
    // @ts-expect-error Static reads require a generated Field binding rather than a numeric ID.
    session.read(65536);
    // @ts-expect-error Generated Config DTO cannot be replaced with an arbitrary number.
    session.write(sdk.control.fields.Config, 123);
    // @ts-expect-error Static inference retains exact bigint leaves.
    const rounded: number = result.value!.large;
    // @ts-expect-error Core is private; no untracked lifecycle/execution escape hatch.
    session.client.close();
    const unsubscribe = session.subscribe(snapshot => { const ready: boolean = snapshot.ready; void ready; });
    unsubscribe(); await session.disconnect(); await session.reconnect(); await session.close();
    void id; void generation; void cancelled; void status; void rounded;
}
export async function dynamicConsumer(transport: ConnectionProfile) {
    const session = await connectDynamic({transport});
    const read: SessionRequest<unknown> = session.read(65536);
    const result = await read;
    if (result.status === Status.Ok && typeof result.value === 'object' && result.value !== null &&
        'number' in result.value && typeof result.value.number === 'number') {
        const number: number = result.value.number; void number;
    }
    // @ts-expect-error Runtime values remain unknown without application narrowing.
    const guessed: sdk.control.types.Config = result.value;
    // @ts-expect-error Dynamic API deliberately uses current runtime IDs, not generated static bindings.
    session.read(sdk.control.fields.Config);
    session.write(65536, {}); session.command(65537); session.service(65537);
    const schema = session.schema, capabilities = session.capabilities;
    void guessed; void schema; void capabilities;
    await session.close();
}
const reference: ConnectionProfile = demoWebSocket('http://localhost:8080');
void reference; void bindWebSocket; void borrowedCarrier;
