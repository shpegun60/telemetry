# Independent core admission acceptance

This suite independently checks the generic admission seam used by DynamicClient.
It uses a controlled backend to exercise successful and rejected authorization,
non-executing packets, execution evidence retained across later packets, passive
completion, deadline precedence, reentrant callbacks and per-request evidence
reset. Compile-time checks require every typed overload to remain available on
the default static policy and unavailable on a runtime-only policy.

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
