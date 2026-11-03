/* Independent JS Descriptor oracle and deeply frozen structural metadata. MIT. */
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {pathToFileURL} from 'node:url';

const [modulePath, jsonPath, descriptorPath, runtimePath] = process.argv.slice(2);
const {structuralMetadata : md} = await import(pathToFileURL(modulePath));
const expected = JSON.parse(await readFile(jsonPath, 'utf8'));
const {parseDescriptor, matchesDescriptorHeader} = await import(pathToFileURL(runtimePath));
const schema = parseDescriptor(await readFile(descriptorPath));
assert.deepEqual(md, expected);
assert.equal(matchesDescriptorHeader(schema, new Uint8Array(md.descriptorHeader)), true);
assert.equal(md.fingerprint, '0x' + schema.fingerprint.toString(16).padStart(16, '0'));
let checks = 3;

function frozen(value)
{
	if (!value || typeof value !== 'object')
		return;
	assert.ok(Object.isFrozen(value));
	assert.throws(() => {
		value.extra = 'forbidden';
	}, TypeError);
	checks += 2;
	for (const child of Object.values(value))
		frozen(child);
}

frozen(md);
assert.equal(md.types.length, schema.types.length);
for (const row of schema.types) {
	const actual = md.types[row.id];
	assert.deepEqual([ actual.id, actual.kind, actual.wireBytes ],
	                 [ row.id, row.kind, row.wireBytes ]);
	if (row.kind === 1)
		assert.equal(actual.scalarCode, row.scalarCode);
	if (row.kind === 2) {
		assert.equal(actual.underlyingTypeId, row.underlyingTypeId);
		assert.deepEqual(actual.entries,
		                 row.entries.map(({code, name}) => ({code : String(code), name})));
	}
	if (row.kind === 3) {
		let offset = 0;
		assert.equal(actual.members.length, row.members.length);
		for (let i = 0; i < row.members.length; ++i) {
			const member = row.members[i];
			assert.deepEqual(
			    [
				    actual.members[i].typeId, actual.members[i].name, actual.members[i].jsName,
				    actual.members[i].wireOffset
			    ],
			    [ member.typeId, member.name, member.name, offset ]);
			offset += schema.types[member.typeId].wireBytes;
		}
		assert.equal(offset, row.wireBytes);
	}
	if (row.kind === 4)
		assert.deepEqual([ actual.elementTypeId, actual.elementCount ],
		                 [ row.elementTypeId, row.elementCount ]);
	++checks;
}
const groups = [ schema.fieldCatalogs, schema.commandCatalogs, schema.serviceCatalogs ];
const endpoints = [ schema.fields, schema.commands, schema.services ];
let catalogIndex = 0, endpointIndex = 0;
for (let category = 1; category <= 3; ++category) {
	for (const cat of groups[category - 1]) {
		const actual = md.catalogs[catalogIndex++];
		assert.deepEqual(
		    [ actual.category, actual.group, actual.firstInCategory, actual.count, actual.name ],
		    [ category, cat.group, cat.first, cat.count, cat.name ]);
		for (const row of endpoints[category - 1].slice(cat.first, cat.first + cat.count)) {
			const ep = md.endpoints[endpointIndex++];
			assert.deepEqual(
			    [
				    ep.category, ep.id, ep.catalogIndex, ep.catalogName, ep.name, ep.valueType,
				    ep.requestType, ep.responseType, ep.capabilities
			    ],
			    [
				    category, row.id, cat.group, cat.name, row.name, row.typeId ?? 0,
				    row.requestTypeId ?? 0, row.responseTypeId ?? 0, row.capabilities ?? 0
			    ]);
			++checks;
		}
		++checks;
	}
}
assert.equal(catalogIndex, md.catalogs.length);
assert.equal(endpointIndex, md.endpoints.length);
console.log(`Metadata checks=${checks} failures=0`);
