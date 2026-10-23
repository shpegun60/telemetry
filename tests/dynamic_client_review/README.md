# Independent core admission acceptance

This suite independently checks the generic admission seam used by DynamicClient.
It uses a controlled backend to exercise successful and rejected authorization,
non-executing packets, execution evidence retained across later packets, passive
completion, deadline precedence, reentrant callbacks and per-request evidence
reset. Compile-time checks require every typed overload to remain available on
the default static policy and unavailable on a runtime-only policy.

Direct `invalidateAdmission()` cases run with both the default static policy and
an independently current custom policy. Repeated revocation invokes no callback
or backend cancellation itself and rejects submissions and handoffs. Subsequent
owner processing drains queued work without starting it. Active cases distinguish
no authorization, non-executing packet authorization and rejected authorization
from a successful execution-capable authorization. The latter remains recorded across later
non-executing packets: new TX stops while a checked passive reply can succeed
before the deadline, timeout retains `OutcomeUnknown`, and an already terminal
result survives. Completion remains exactly once.

Failed re-admission, restored backend readiness and recovery confirmation alone
cannot revive admission. The cases also retain `RecoveryRequired` and `Closed`
gates and require a successful fresh Descriptor admission before new work starts.

```sh
python3 -B tests/dynamic_client_review/run.py --cxx g++ --build-dir /external/core-policy
python3 -B tests/dynamic_client_review/run.py --cxx clang++-18 --sanitize --build-dir /external/core-policy-sanitized
```

An additional real Encoded integration probe distinguishes a malformed wire
reply with recovery from stale schema identity, preserves the operation result
when canonical Boolean validation fails, and actually frees descriptor/index
storage after invalidation while retaining the workspace control object.

The suite does not replace the complete Encoded/RPC integration tests. Its clock and
backend are controlled host fixtures; no physical transport or board behavior
is claimed. The runner retains compiler commands, logs, binary hashes and
before/after source hashes outside the checkout.
