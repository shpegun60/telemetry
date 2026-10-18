# Generated web client

Authors: Ruslan Kovtun (shpegun60), codexAi. [MIT](../../LICENSE).

`client.ts` uses the generated contract of the native
[`tests/codegen/Device.hpp`](../../tests/codegen/Device.hpp) model. Generate its
descriptor into `generated/` next to the copied example, then compile with
`tsc --strict --target ES2022 --module NodeNext --moduleResolution NodeNext`.
The codegen runner performs that setup outside the source tree and executes
the same adapter against the native mailbox bridge for JS and compiled TS.

Pass `run(exchange, declaredHeader)` an asynchronous function that sends one
complete Resource packet and returns one complete reply. UART/TCP framing,
timeouts, admission, descriptor agreement and connection generation remain
application responsibilities. `matchesHeader` checks the declared 64-byte
header identity; it does not hash a descriptor body or authenticate a peer.

`transport.mjs` shows bounded Resource WRITE fragmentation, RPC status polling
and result READ assembly. Each operation uses an explicit, strictly increasing
nonzero transaction ID in one serialized session. Configure byte/packet budgets
for the peer. The adapter validates reply identity, state, cursors, lengths and
both status layers before invoking any payload decoder. Failed empty responses
therefore throw, and Command `Accepted` remains distinct from completion.

No operation is automatically retried after a transport error. A lost reply or
connection leaves execution uncertain; the application decides how to recover.
