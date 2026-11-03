// Runtime values remain unknown until the application narrows them. MIT.
import {DynamicClient, ValueStatus} from './web/DynamicClient.js';
import {EncodedBackend, Status} from './lib/client/web/Client.js';
import {parseDescriptor, encodeValuePreserving, type RuntimeSchema} from './web/telemetry.js';

const client = new DynamicClient({
	backend : new EncodedBackend(
	    () => {
	    },
	    {readMany : true, batch : true})
});
const model = parseDescriptor(new Uint8Array(64));
client.admit(model, new Uint8Array(64), 1);
const writable = client.bind('FieldWrite', 65536);
if (writable.status === Status.Ok) {
	client.writeFieldPreserving(writable.endpoint, {number : 42}, new Uint8Array(41));
	// @ts-expect-error A preserving write requires original binary value bytes.
	client.writeFieldPreserving(writable.endpoint, {number : 42}, 41);
}

export function preservedPayload(schema: RuntimeSchema, typeId: number, value: unknown,
                                 seed: Uint8Array): Uint8Array
{
	return encodeValuePreserving(schema, typeId, value, seed);
}

// @ts-expect-error The runtime preserving encoder also requires a binary seed.
encodeValuePreserving(model, 14, {}, 'original bytes');
const selected = client.bind('FieldRead', 65536);
if (selected.status === Status.Ok) {
	client.readField(selected.endpoint).then(result => {
		if (result.valueStatus === ValueStatus.Ok && typeof result.value === 'object' &&
		    result.value !== null && 'number' in result.value &&
		    typeof result.value.number === 'number') {
			const exact: number = result.value.number;
			void exact;
		}
		// @ts-expect-error Runtime values cannot pretend to be generated DTOs.
		const unsafe: {number: number} = result.value;
		void unsafe;
	});
	const watch = client.bindReadMany(new Uint8Array(24), [ selected.endpoint ]);
	if (watch.status === Status.Ok)
		client.readMany(watch.binding);
	client.batch(new Uint8Array(24), [ {endpoint : selected.endpoint} ]);
	// @ts-expect-error Dynamic client owns its core; no submitEncoded bypass is public.
	client.core.submitEncoded(selected.endpoint);
}
// @ts-expect-error Only parser-created models are admissible.
const forgedModel: RuntimeSchema = {
	fingerprint : 0n
};
void forgedModel;
const forgedEndpoint = {
	operation : 'FieldRead',
	id : 65536,
	session : 1,
	requestType : 0,
	responseType : 14
};
// @ts-expect-error Endpoint identity cannot be forged from public fields.
client.readField(forgedEndpoint);
// @ts-expect-error Preserving writes retain the same private endpoint identity requirement.
client.writeFieldPreserving({...forgedEndpoint, operation : 'FieldWrite' as const}, {},
                            new Uint8Array(41));
// @ts-expect-error Dynamic APIs use opaque endpoints, not raw IDs.
client.readField(65536);
new DynamicClient({
	backend : new EncodedBackend(() => {
	                             }),
	// @ts-expect-error Static generated matching-header policy is not a DynamicClient option.
	matchesHeader : () => true
});
// @ts-expect-error The wrapper does not expose the static typed read API.
client.read(selected);
