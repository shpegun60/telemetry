# Native values by const reference

Authors: Ruslan Kovtun (shpegun60), codexAi.

This separate extension follows the completed Stage 16–20 baseline
`8c56a5e` (exact CI 6/6) and the test-only ARM branch gate checkpoint
`595a65b`. Endpoint declarations remain name + binding. The extension changes
native ownership and Field scratch metadata, without changing canonical type
shapes, packed u32 IDs, descriptor v3.0 or Values bytes.

## Declaration and results

The existing factories infer ownership from the exact callback signature.
There is no second declaration or call syntax:

```cpp
struct Config { std::array<std::uint32_t, 1024> words; };
struct Device {
    Config config{};
    const Config& readConfig() const noexcept { return config; }
    telemetry::WriteResult writeConfig(const Config& next) noexcept {
        config = next;
        return telemetry::WriteResult::Applied;
    }
};
inline Device device;
inline constexpr telemetry::FieldTable fields{
    telemetry::field<&Device::readConfig, &Device::writeConfig>("Config", device)
};
inline constexpr telemetry::ServiceTable services{
    telemetry::service<&Device::readConfig>("Config", device)
};

auto view = fields.read<0>();
auto reply = services.call<0>();
if (view && reply.hasValue()) {
    const Config& first = view.value();
    const Config& second = reply.value();
    // Both refer to device.config; no Config is stored in either result.
    (void)first;
    (void)second;
}
auto copy = fields.readAs<Config, 0>(); // explicit owning optional<Config>
```

Include `<telemetry/Telemetry.hpp>` plus standard headers used by the application.
Function pointers, NTTP functions/methods, capture-free lambdas, stable callable
lvalues and the existing slots use the same signature rule.

| Callback | Native result |
| --- | --- |
| Field `T() noexcept` | `std::optional<T>` |
| Field `const T&() noexcept` | `BorrowedValue<T>` |
| Field `readAs<To>()` in either mode | owning `std::optional<To>` |
| Service `T(...) noexcept` or `ServiceResult<T>(...) noexcept` | owning `ServiceResult<T>` |
| Service `const T&(...) noexcept` | `BorrowedServiceResult<T>` |
| Service `BorrowedServiceResult<T>(...) noexcept` | the same borrowed result and application status |
| Service `void(...) noexcept` | `ServiceResult<void>` |

Only const, nonvolatile lvalue references are added. Mutable/volatile references,
rvalue references and pointers remain rejected. A borrowed Field supports the
same scalar, enum, array and aggregate types as an owning Field. A Service still
accepts zero or one aggregate Request and returns an aggregate Response or void.
Both Service wrappers must be returned by value with the exact unqualified type.
Returning a reference to either wrapper remains rejected. Field getters cannot
return either wrapper: their payload remains `T` or `const T&`.

A fallible borrowed Service constructs its result explicitly:

```cpp
telemetry::BorrowedServiceResult<Config> readConfig() noexcept {
    if (busy) return telemetry::BorrowedServiceResult<Config>::failure(
        telemetry::ServiceStatus::Busy);
    return telemetry::BorrowedServiceResult<Config>::success(device.config);
}
```

Here `busy` is application state. A failure contains no payload, invokes no
response encoder and writes zero response bytes. The registry unwraps both
Service result kinds to `Config`; status/view storage is never a wire type.

Setter inference uses canonical `T`, regardless of getter ownership. It accepts
exact `T` or `const T&` and returns `WriteResult`. Command contracts do not change.

`BorrowedValue<T>` contains one `const T*`. Default construction is an empty view.
`from(value)` accepts only a deduced exact nonvolatile lvalue; ordinary/const
temporaries, braced temporaries, proxy conversions and explicit-template bypasses
are rejected. `BorrowedServiceResult<T>` contains a status and that view.
`success(value)` has the same argument rule; `failure(status)` requires an existing
non-Ok ServiceStatus. There is no public pointer success factory or inconsistent
default Service result.

Both views provide `value_type`, `hasValue()`, explicit bool, `valueOrNull()`,
`value()`, `*` and `->`. Payload access is const. Dereferencing requires a nonempty
view. Copying/moving/destroying a view never copies, moves or destroys its T.
An empty binding gives an empty Field view or native Service Unavailable;
encoded access uses the existing DispatchStatus::Unavailable.

## Lifetime and synchronization

Native results are views after read/call returns. The application keeps T alive
and stable for every use. A returned local object or a by-value callback parameter
cannot satisfy this requirement. A response referring into a caller's Request
cannot outlive that Request; passing a temporary does not extend its lifetime
through the result. Slot reset/rebind likewise does not preserve an old object.

Encoded access retains the reference only for the current invocation and
serialization. A response referring into a decoded const-reference Request is
valid through encoding: its local object or Workspace lease is still alive.
No returned response pointer is saved in the Definition, Table, Model or provider.

External synchronization prevents concurrent changes while reading/encoding.
The library does not make a coherent snapshot, lock the owner, or detect all
dangling references hidden by application helpers. Getters calculating a new
value continue to return by value; they cannot borrow a temporary calculation.

## Storage and checked boundaries

```text
Field getter returns T
    -> local T or Workspace T
    -> encode

Field getter returns const T&
    -> BorrowedValue<T> for native read
    -> encode existing T for encoded read

Service returns T / ServiceResult<T>
    -> existing Request + owning Result storage policy
    -> encode

Service returns const T& / BorrowedServiceResult<T>
    -> decode Request using existing policy
    -> invoke once and retain const reference through encode
    -> encode existing Response
```

The default compile-time local object budget remains 32 bytes. Borrowed payloads
are existing application objects and consume none of that budget, even at `0`.
Small status/view control objects are independent of payload size. A borrowed
Service uses `ServiceStorage<Request, void>`: only Request storage/alignment is
advertised, including when Request exactly consumes the local budget. A fully
local Request or no Request means no Workspace access.

Field requirements are independent:

```text
readScratchBytes  = borrowed getter or local T ? 0 : scratchBytes<T>
writeScratchBytes = no setter or local T      ? 0 : scratchBytes<T>
```

Read/write boundaries use their respective requirements. Model.maxFieldScratch
covers both; Values.requiredWorkspace covers only reads. Large setters still
validate/decode in Workspace. Existing lease alignment, cleanup and LIFO rules
remain intact, including nested callers with active outer leases.

Lookup, length/capacity preflight, availability snapshots and bool validation
retain their existing order. The referenced object address is known only after
the callback. Before the first payload write, borrowed encoding checks all
`sizeof(T)` native bytes, including padding, against the written output prefix.
Overlap returns InvalidPayload with zero bytes written; callback side effects
already performed are not rolled back. An unused output tail does not participate.
Service input/output overlap remains allowed after complete Request decoding.

Values and optional Exchange also write surrounding header/status bytes.
Their **complete output buffers must be disjoint from live native application
objects**, a caller precondition that a payload-only check cannot establish.
Neither adapter performs a second getter merely to discover addresses in advance.

`readAs<T>` deliberately copies from a borrowed view into an owning optional;
numeric conversions remain checked and exact structural mismatch performs no
getter call. Explicit large native copies can still require large caller stack.

## ABI and wire identity

The C++ ABI revision becomes 6. The old FieldEntry had one scratch member;
separate read/write requirements add one u32. The inspected ARM layout is 32/4
instead of 28/4. CommandEntry 20/4, ServiceEntry 24/4 and ModelView retain their
layouts. The exact ABI tag replaces its old scratch offset at part 82 with the
read offset and appends the write offset at part 132, preserving earlier indices.
Independent mismatch controls cover both offsets. Incompatible compiled
in-memory consumers must be rebuilt; no compatibility alias hides the layout.

Value/Request/Response aliases and registry roots use canonical T, never a view
wrapper. Ownership has no descriptor bit: otherwise equal names, bindings,
capabilities and shapes have identical TypeIds, descriptor bytes/fingerprint and
Values payloads. Wire v3.0, 11 acceptance ceilings, dependency pins and four
frozen golden files remain unchanged.

## Qualification

The [dedicated suite](../tests/structured/borrowed/README.md) separates view
factory/signature controls, native/slot behavior, encoded storage/overlap,
wire parity and ARM code/stack evidence. Existing owning direct/local/global
instruction gates and frame ceilings remain required; a changed erased row
stride is not presented as an identical linked image.

The same work corrects generic address-taking with `std::addressof`: an accepted
aggregate may overload `operator&`, which must not redirect payload construction,
destruction, pointer access, decoded Request selection, overlap or nested-member
alignment checks. Before-fix repros are retained separately from the new
53-condition AddressProbe.

Baseline Stage 20 receipts retain their measured source identity. New code needs
new source/input/image records; archived timing cannot be relabelled as this
extension. The dedicated H7S fixture compares 4 KiB native/encoded owning and
borrowed paths, and 64 KiB borrowed native/owning encoded/borrowed encoded paths.
It never runs a 64 KiB owning native result on the existing 16 KiB probe stack.
Software and hardware qualification results are recorded after their gates pass.

## Qualification results

The code is sealed at `7b73fb4c97e48ebb012ea0b6010bc7a4c3c434b3`.
The [qualification record](evidence/BorrowedNativeValuesQualification.json)
keeps each factual capture HEAD, dirty state, compiler identity, report digest
and receipt identity. All 197 distinct captured inputs were compared with the
sealed Git blobs. Most software reports were captured before the code commit,
at `595a65b` with dirty inputs; the ARM13 null role and all hardware captures
record the later sealed HEAD. These are input-equivalence claims, not invented
clean-tree captures.

| Dedicated software role | Executed conditions | Intended compile rejections |
| --- | ---: | ---: |
| MinGW GCC 13.1 | 2580 | 63 |
| Clang 18, ASan/UBSan | 2588 | 63 |
| GCC 13, null-check flag | 2588 | 63 |
| CubeIDE ARM GCC 14.3.1, normal / null | 0 / 0 | 63 / 63 |
| ARM GCC 13.2.1, normal / null | 0 / 0 | 63 / 63 |

All roles passed storage budgets 0/16/32/64. ARM O2/Os/Og individual
native/Entry/thunk frames were at most 48/56/136 B; separately retained
compiled Model wrappers were at most 280/248/256 B. Release normal-mode
direct/local/global roots match instruction bytes **and** exact relocations.
Og/null modes do not claim this equality. Real changed-body, wrong-owner,
large-frame and linked-allocation controls exercise those respective gates.
The existing seven-role owning matrix also passed: 130914 host conditions,
42 configurations, 424 compiler/link/inspection commands and zero ARM
executions. Its [renewed receipt](../tests/structured/mcu/local-receipt.json)
remains separate from the dedicated suite.

On NUCLEO-H7S3L8, the dedicated O2/Os images each passed 127 conditions
and compared 626688 full encoded payload bytes, with 294 timing and 258
stack rows per image. Ten further images passed the previous owning MCU,
Descriptor, Values/resource and Bind/Exchange plans. All five device runners
restored their full 65536-byte backup and verified a fresh readback, with
the same SHA-256 before and after. The [measured tables](../tests/structured/borrowed/h7s/RESULTS.md)
retain all comparison scopes, including the Os encoded path's 4 B greater
observed stack and the absence of a 64 KiB owning-native comparison.

Current receipts qualify this extension's inputs. The [Stage 20 archive](evidence/pre-borrowed/README.md)
retains exact previous receipt/validator bytes and original measured source
identities. The existing four golden files, dependency pins and acceptance
ceilings still pass the freeze checks. This evidence does not convert a
borrowed view into an owning snapshot or remove the application's lifetime
and synchronization obligations. Published-SHA CI remains an independent gate.
