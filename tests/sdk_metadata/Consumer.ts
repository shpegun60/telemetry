/* Strict use-site narrowing for optional metadata; no generated runtime import. MIT. */
import {structuralMetadata, type StructuralType} from './generated/ts/device.metadata.js';
export function describe(type: StructuralType): number {
    switch (type.kind) {
    case 0: return type.wireBytes;
    case 1: return type.scalarCode;
    case 2: return type.entries.length + type.underlyingTypeId;
    case 3: return type.members.reduce((sum, member) => sum + member.wireOffset, 0);
    case 4: return type.elementCount + type.elementTypeId;
    }
}
structuralMetadata.types.forEach(describe);
// @ts-expect-error Type metadata is immutable.
structuralMetadata.types.push({id: 0, kind: 0, wireBytes: 0});
// @ts-expect-error Struct metadata requires its members.
const bad: StructuralType = {id: 12, kind: 3, wireBytes: 0};
void bad;
