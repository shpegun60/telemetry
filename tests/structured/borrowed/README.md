# Borrowed Field and Service qualification

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.

This suite checks raw `const T&` callbacks and explicit by-value
`BorrowedServiceResult<T>` Service callbacks. Borrowed Field reads return
`BorrowedValue<T>` and borrowed Service calls return `BorrowedServiceResult<T>`.
These views expose const data and do not own, copy, freeze or extend the lifetime
of that data. Existing owning calls and owning `readAs<T>` remain separate tests.

`Result.cpp` checks view identity, immutable access, failure statuses and payload
lifetime with deleted payload copy operations. `Native.cpp` covers supported
Field shapes, exact return types, explicit owning As copies, traversal and all
five slot families. `Encoded.cpp` covers 4 KiB/64 KiB complete response bytes,
void/small/full-budget/large requests, independent Field read/write scratch,
empty and nested Workspaces, cleanup, preflight callbacks, bad bool, rebinding
and native-object/output overlap including padding and the written prefix.
`Status.cpp` checks explicit borrowed-result callbacks, all four failure statuses,
unwritten output on failures, 4 KiB/64 KiB responses, Request-only storage,
request-backed references through encoding and all five slot forms.
`Wire.cpp` compares paired owning/borrowed Model type identity, every descriptor
byte/fingerprint and Values bytes against an independent LE/pattern oracle.
The prerequisite `AddressProbe.cpp` checks real native addresses with overloaded
address operators. `Negative.cpp` has one intended diagnostic per unsupported
factory or endpoint form, including explicit-template factory attempts. Field
wrapper-return callbacks, cv-qualified/borrowed-wrapper references, scalar/array
Service response types and pointer/mutable-reference callbacks are rejected.

Host probes observe `operator new` during actual operations, with a retained
allocation positive control that must produce one failure. Output/file I/O is
outside that observation. The ELF host roles also execute the checked failure
factory controls for Ok and an unknown status; the Windows role avoids desktop
abort dialogs. ARM roots are separately linked and checked for live allocation,
formatting and retired value-erasure symbols. A real linked-symbol control must
be rejected, as must a real 4 KiB compiler frame, a changed native code body and
a changed owner relocation with identical unresolved instruction bytes.

```sh
python tests/structured/borrowed/run.py --cxx g++ --build-dir build/borrowed-gcc
python tests/structured/borrowed/run.py --cxx g++ --null-checks --build-dir build/borrowed-null
python tests/structured/borrowed/run.py --cxx clang++-18 --sanitize --build-dir build/borrowed-sanitized
python tests/structured/borrowed/run.py --cxx arm-none-eabi-g++ --arm --build-dir build/borrowed-arm
python tests/structured/borrowed/run.py --cxx arm-none-eabi-g++ --arm --null-checks --build-dir build/borrowed-arm-null
```

Every invocation requires a fresh output directory and captures LF source
inputs before compiling. Host runs use O2, and ARM compile/link uses O2/Os/Og;
both test storage budgets 0/16/32/64. Counts are independently pinned by probe:
Result 24, Native 125 on the PE host or 127 on an ELF host, Encoded 201,
Status 215, Wire 27 and AddressProbe 53, for 2,580 or 2,588 host conditions
over four budgets. There are 63 intended compile rejections. Declaration-only
callbacks with cv/ref-qualified payloads or borrowed `void` must fail at the
Service factory boundary, before result construction or native invocation.
Compile rejections and runtime/control rejections are recorded separately.

ARM executes zero C++ checks. `Arm.cpp` compares direct/manual, local and global
pointer/view return bodies for both 4 KiB and 64 KiB, including explicit borrowed
status callbacks. Release normal-mode bodies and exact owner/function relocation
records must match; Og/null modes retain assembly but do not claim instruction equality.
Every individual frame in these isolated native/Entry/thunk roots must be static
and at most 192 bytes. `ModelRoots.cpp` separately retains the compiled adapter's
by-value `ModelView` boundary and records its assembly and static frames against
a 320-byte ceiling. Those view copies are independent of Response size. Ordinary
runtime-test objects are not subject to that
frame ceiling: they intentionally contain caller buffers and explicit owning
copy checks. No cycle or whole-call-chain stack result is inferred here.

`summary.json` records actual condition counts, flags, compiler executable hash,
Git HEAD/dirty status, captured input hashes, executable/object/ELF identities,
wire digest, raw assembly and `.su` identities. Existing reports are never
overwritten. A successful local run does not establish exact-SHA CI or hardware
execution. The existing owning suites, freeze goldens, ceilings and native
codegen gates must also pass; this suite does not relax them.

Encoded object/output checks cover only the written wire prefix. The complete
Values or optional Exchange output must remain disjoint from all live native
borrowed objects because headers/status bytes are written outside the payload
thunk. Native callers must keep a request-backed result's Request alive through
every result use; passing a temporary does not extend its lifetime.

The completed local matrix and five separate device families are recorded in
[BorrowedNativeValues.md](../../../doc/BorrowedNativeValues.md#qualification-results).
Each factual captured HEAD is preserved, with source equivalence checked
against the sealed code commit. CI repeats this suite on the published SHA;
the local qualification record is not a replacement for that run.
