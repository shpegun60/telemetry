// Browser consumer of the reference server: discover, read one Field, close. MIT.
import {connectDynamic, demoWebSocket} from 'telemetry-sdk';

const output = document.querySelector('#result');
let session;
try {
	session = await connectDynamic({transport : demoWebSocket(location.origin)});
	const field = session.schema.fields.find(endpoint => (endpoint.capabilities & 1) !== 0);
	if (!field)
		throw new Error('The device has no Field');
	const result = await session.read(field.id);
	output.textContent = JSON.stringify(
	    result, (_, value) => typeof value === 'bigint' ? value.toString() : value, 2);
} catch (error) {
	output.textContent = `${error.code ?? error.name}: ${error.message}`;
} finally {
	await session?.close();
}
