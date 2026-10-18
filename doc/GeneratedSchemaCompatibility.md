# Generated schema compatibility

The generated SDK describes one exact Descriptor identity. Its schema
fingerprint is the existing FNV-1a-64 descriptor fingerprint, including names,
ordering, capabilities and records. It is not a cryptographic hash, transport
authentication, or a structural compatibility proof between different schemas.
SHA-256 in the manifest identifies the full input artifact separately.

Before typed calls, obtain the Descriptor for the selected peer/session and
validate it fully when downloading it. Compare its identity to the generated
SDK. The generated C++ `schema::matchesHeader` and JS `matchesHeader` compare
an exact 64-byte declared descriptor header against the generated header.
They do not hash a body, authenticate a peer or prove that a reported header
belongs to the endpoint model currently executing requests.

The application owns this binding: descriptor source, endpoint namespace,
peer, connection/session generation and generated SDK must refer to the same
model. The explicit RpcBindings trust tag asserts that the caller performed
this admission. It does not perform a remote schema check itself.

After successful admission, ordinary calls need no Descriptor parsing or
TypeGraph traversal. On mismatch, stop typed calls for that mapping. Download
and validate the matching schema and regenerate/select the right SDK. Do not
decode a similarly sized response with an older type. A new connection,
peer change, reset or firmware replacement invalidates session admission;
repeat it before enabling typed calls. Never retain a stale admission across
a generation change merely because an endpoint ID still exists.

Operation results are gated independently: complete transport result,
dispatch success, endpoint-family status, exact response length, codec result.
Pending/Running is not a completed Service. Service refusal with an empty
payload is not a successful empty struct. Command Accepted may mean work has
been queued; application policy determines later completion confirmation.

See [generator workflow](CodeGenerator.md), [C++](GeneratedCppApi.md) and
[JS/TS](GeneratedTsJsApi.md).
