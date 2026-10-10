# Codec і Workspace: canonical bytes та payload lifetime

Автори: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.

[API Overview](APIOverview.md) · [Посібник](README.md) · [Model](Model.md) · [Fields](Fields.md) ·
[Таблиці й каталоги](TablesAndCatalogs.md) · [Descriptor і Values](DescriptorAndValues.md)

Codec перетворює supported native object у fixed-size little-endian payload.
Готовий object можна заповнити через `decode(bytes, object)` без Workspace.
`Workspace` надає caller-owned aligned storage, коли треба створити новий object
під час decode або encoded endpoint call. `decode<T>(bytes, workspace)` ховає
Lease у RAII result `Decoded<T>`; explicit Lease API теж залишається. Bytes можуть
залишатися у buffer після завершення object lifetime, але старі references та
pointers використовувати вже не можна.

Public implementation:
[Codec.hpp](../../lib/telemetry/codec/Codec.hpp),
[Workspace.hpp](../../lib/telemetry/codec/Workspace.hpp),
[StoragePolicy.hpp](../../lib/telemetry/codec/StoragePolicy.hpp).
Ця сторінка описує поточний C++20 API та фактичні перевірки.

## Коли вибрати Codec

Виберіть standalone codec, коли application уже знає тип `T` і потребує
тільки `T ↔ bytes`: наприклад, для DTO у власному packet або для збереження
canonical bytes. Endpoint declarations, таблиці, catalogs, TypeRegistry і Model
створювати не потрібно. Достатньо `<telemetry/codec/Codec.hpp>` та include paths
для його поточних reflection dependencies; compiled `.cpp` для цього сценарію
немає. Якщо bytes мають вибрати Field, Command чи Service за runtime ID,
перейдіть до [encoded доступу](Model.md#encoded-api-catalog-index-або-modelview).

Сам codec не виділяє heap. Для `decode(bytes, object)` native storage уже
належить caller; для двох інших decode-форм його надає Workspace. Місце для
wire bytes у всіх формах також виділяє caller. Codec не збирає packet із
UART/TCP chunks: передайте повний payload відповідного `T`.

## Навігація

- [Коли вибрати Codec](#коли-вибрати-codec)
- [Wire shape](#wire-shape)
- [Повний standalone приклад](#повний-standalone-приклад)
- [Public codec API і statuses](#public-codec-api-і-statuses)
- [Decoded result: status і lifetime](#decoded-result-status-і-lifetime)
- [Workspace та Lease API](#workspace-та-lease-api)
- [Alignment, nested leases і lifetime](#alignment-nested-leases-і-lifetime)
- [Local object budget](#local-object-budget)
- [Encoded endpoint boundaries](#encoded-endpoint-boundaries)
- [Overlap rules](#overlap-rules)
- [Construction та structural limits](#construction-та-structural-limits)
- [Практичний порядок інтеграції](#практичний-порядок-інтеграції)

## Wire shape

`ts::wireSize<T>` — compile-time canonical extent. Aggregate padding,
native pointers і object bookkeeping не передаються. Для struct bytes ідуть
у reflected member declaration order, для `std::array` — у element order.
Кожний scalar leaf кодується самостійно.

Для supported C++20 aggregates reflection facade використовує pinned
Boost.PFR: він надає compile-time member count, types та доступ до members.
Для enum dictionaries facade використовує pinned `magic_enum` або explicit
entries. Codec enum передає underlying integer, а не ім'я чи словник; тому
decode не вимагає, щоб code входив у named entries. Backend contracts і повний
набір supported types наведено у [Model: reflection](Model.md).

| Native shape | Canonical payload |
| --- | --- |
| `bool` | Один byte: тільки 0 або 1 |
| Supported unsigned integer | Його fixed-width bytes у little-endian order |
| Supported signed integer | Full-width two's-complement bits у little-endian order |
| `float`, `double` | IEEE binary32/binary64 bit pattern у little-endian order |
| Scoped enum | Canonical underlying integer; unnamed representable codes допустимі |
| `std::array<E,N>` | N послідовних payloads `E` |
| Supported aggregate | Послідовні payloads members без native padding |
| `void` | Немає native object; standalone encode/decode відхиляються під час compilation |

NaN, infinity та unknown enum codes не означають codec error. Вони мають
representable bits; application setter/Service перевіряє їхню прикладну
допустимість. Неканонічний bool byte, наприклад 2, дає `InvalidValue` до
construction і до invocation application callback.

Empty aggregate або `std::array<E,0>` можуть мати `wireSize == 0`, зберігаючи
власний native object lifetime. Empty spans не перетинаються з жодним range.
Це відрізняється від missing Command/Service request `void`, для якого object
взагалі не створюється.

## Повний standalone приклад

Програма кодує nested aggregate зі `std::array` у 17-byte payload і показує всі
три форми decode. Field, Command, Service, Model і compiled sources не потрібні.
[Повна runnable програма](../../examples/user_guide/Codec.cpp) додатково перевіряє
golden bytes, незмінність existing object на відмові, insufficient storage та LIFO.

```cpp
#include <telemetry/codec/Codec.hpp>

#include <array>
#include <cassert>
#include <cstdint>

namespace ts = telemetry;

struct Channel {
    std::uint16_t adc;
    float rms;
};

struct Measurement {
    std::array<Channel, 2> channels;
    std::uint32_t timestamp;
    bool valid;
};

int main()
{
    static_assert(ts::wireSize<Measurement> == 17);
    const Measurement source{{{{1234, 220.5f}, {2345, 221.75f}}}, 1000, true};
    std::array<std::byte, ts::wireSize<Measurement>> wire{};
    assert(ts::encode(source, wire) == ts::CodecStatus::Ok);

    // Existing object: decode directly, with validation before mutation.
    Measurement existing{};
    assert(ts::decode(wire, existing) == ts::CodecStatus::Ok);
    assert(existing.timestamp == 1000 && existing.valid);

    std::array<std::byte, ts::scratchBytes<Measurement>> storage{};
    ts::Workspace workspace{storage};
    {
        // New object: result owns its lifetime and reservation, not the bytes.
        auto result = ts::decode<Measurement>(wire, workspace);
        assert(result && result.status() == ts::CodecStatus::Ok);
        assert(result.value().channels[0].adc == 1234);
    }
    assert(workspace.used() == 0);

    {
        // Existing low-level API: caller manages the Lease explicitly.
        auto lease = workspace.reserve<Measurement>();
        Measurement* decoded = nullptr;
        assert(ts::decode(wire, lease, decoded) == ts::CodecStatus::Ok);
        assert(decoded == lease.get());
        assert(decoded->channels[1].rms == 221.75f);
    }
    assert(workspace.used() == 0);

    // An invalid bool at the end does not partially overwrite existing.
    wire.back() = std::byte{2};
    assert(ts::decode(wire, existing) == ts::CodecStatus::InvalidValue);
    assert(existing.timestamp == 1000 && existing.valid);
}
```

Для обох Workspace forms reservation передує decode. Якщо вона успішна, але
decode відхилено, bytes залишаються зарезервованими до destruction result/lease.
Після успіху scope exit завершує object lifetime і повертає попередній cursor.

## Public codec API і statuses

| API | Результат | Об'єкти та контракт |
| --- | --- | --- |
| `encode(const T& value, span<byte> output)` | `CodecStatus` | Existing T належить caller; `output.size()` дорівнює `wireSize<T>`; native object і output disjoint |
| `decode(span<const byte> input, T& output)` | `CodecStatus` | Existing mutable nonvolatile T належить caller; exact input length; input disjoint від усіх native bytes output, включно з padding; Workspace не потрібний |
| `decode<T>(span<const byte> input, Workspace& workspace)` | `Decoded<T>` | Новий T створюється у Workspace; noncopyable/nonmovable result утримує reservation і object lifetime, backing bytes належать caller |
| `decode(span<const byte> input, Workspace::Lease<T>& lease, T*& output)` | `CodecStatus`; на успіху `output` вказує на T у Lease | Новий T створюється у caller-managed fresh valid lease; exact input length; input disjoint від complete Workspace span |
| `buffersOverlap(a,b)` | `bool` | Перевіряє overlap двох byte ranges; empty range завжди disjoint |
| `buffersDisjoint(input,output,workspace)` | `bool` | Перевіряє всі три pairs; це helper для сильнішого pairwise-disjoint контракту |

Усі наведені functions — у namespace `telemetry`; public codec operations
мають `noexcept`. Source object/input bytes і destination залишаються живими
та синхронізованими протягом виклику. Decode копіює semantic members у native
object, тому після повернення більше не позичає input bytes. New T у Workspace
живе до destruction result/lease, а не до destruction receive buffer.

Standalone `encode` не приймає більший output span автоматично. Якщо транспорт
надав великий buffer, передайте `output.first(ts::wireSize<T>)` після своєї capacity
перевірки. Endpoint read/Service APIs нижче мають інший capacity контракт.

Existing-object decode спочатку перевіряє exact length, overlap і повну canonical
representation, а вже потім записує всі members. Відмови preflight/representation
не змінюють semantic values existing object. Це не CPU atomic write і не
synchronization: application забезпечує exclusive access до object і input
протягом усього виклику. Decode не перевіряє бізнес-обмеження, тому live
`device.settings` зазвичай слід змінювати через setter після окремого DTO decode.

Low-level `decode(input, lease, output)` насамперед присвоює `output = nullptr`, потім перевіряє length, Workspace
overlap, lease capacity та fresh state. Bool validation завершується до object
construction. Уже constructed lease не використовується для повторного decode;
створіть новий scope/lease після звільнення попереднього.

| `CodecStatus` | Коли повертається | Дія caller |
| --- | --- | --- |
| `Ok` | Exact payload успішно encoded/decoded | Використати bytes, existing object або новий object у межах result/lease lifetime |
| `LengthMismatch` | Span size відрізняється від `wireSize<T>` | Перевірити expected shape і payload length |
| `InvalidValue` | Decode знайшов bool byte поза 0..1 | Existing object незмінний; new object ще не constructed |
| `WorkspaceTooSmall` | Decode отримав invalid reservation | Надати достатню aligned capacity або звільнити outer leases |
| `Overlap` | Encode output перетинає native object; decode input перетинає existing object або Workspace | Використати disjoint storage |
| `InvalidState` | Lease вже constructed або внутрішній codec cursor не відповідає exact extent | Для повторного decode створити fresh lease; cursor mismatch вказує на порушення implementation invariant |

Encode length/overlap errors не записують output. Decode preflight/representation
errors не створюють new object і не змінюють existing object; low-level output pointer залишається null. При повторному
decode уже constructed lease повертає `InvalidState`, але його попередній object
залишається живим до destruction lease.

## Decoded result: status і lifetime

`Decoded<T>` доступний через `<telemetry/codec/Codec.hpp>`. Він утримує
`Workspace::Lease<T>` і status, але не володіє Workspace чи його backing buffer.
`T` конструюється безпосередньо у reserved bytes; у result немає другої копії T.
Повернення immobile result працює через guaranteed prvalue construction C++20.

| `Decoded<T>` API | Значення |
| --- | --- |
| `status()` | `CodecStatus` завершеного decode |
| `hasValue()`, explicit `bool` | True тільки для `CodecStatus::Ok` |
| `value() &` | Mutable `T&` для успішного named result |
| `value() const&` | `const T&` для успішного const named result |
| `value() &&`, `value() const&&` | Видалені: reference не береться прямо з temporary result |
| Copy/move | Видалені; result лишається у своєму scope |
| Destructor | Destroy T, якщо він constructed, і release reservation |

Немає nullable pointer accessor; спочатку перевірте result, потім викликайте
`value()`. Access на failure є порушенням contract і завершується через `abort`.
Збережене `T&` не продовжує lifetime result. Workspace та backing bytes мають
жити довше за result і всіх references до його T.

Successful reservation залишається активною навіть для `LengthMismatch`,
`InvalidValue` або `Overlap`. На відмові T не constructed, але `used()` може
бути ненульовим до destructor result. Invalid reservation не споживає bytes.
Для кількох results/explicit leases у тому самому Workspace потрібне LIFO
destruction незалежно від success status. Для retry у fresh storage завершіть
попередній result scope.

## Workspace та Lease API

`Workspace` позичає `span<byte>` без allocation. Його byte span може бути у
static RAM, на caller stack чи в іншому application-owned storage. Сам Workspace
і bytes живуть довше за всі reservations; доступ до cursor серіалізує application.

| `Workspace` API | Значення |
| --- | --- |
| `Workspace{storage}` | Позичає весь span, initial `used() == 0` |
| `storage()` | Повний backing span, включно з активними outer leases і вільним хвостом |
| `used()` | Cursor зайнятої prefix області, включно з alignment gaps |
| `reserve<T>()` | Повертає noncopyable/nonmovable aligned `Lease<T>` без construction |
| `scratchBytes<T>` | `sizeof(T) + alignof(T) - 1`: достатній fresh-span bound без припущення про initial alignment |

| `Workspace::Lease<T>` API | Значення |
| --- | --- |
| `Lease{workspace}` / `workspace.reserve<T>()` | Резервує aligned native storage після поточного cursor |
| `valid()` | Reservation існує; це ще не означає, що object constructed |
| `constructed()` | T lifetime уже почався |
| `get()` | Pointer на constructed T або null |
| `workspaceStorage()` | Complete Workspace span для overlap checks |
| `constructDefault()` | Default-initialize trivially default-constructible T; semantic members треба заповнити перед читанням |
| `constructFrom(factory)` | Construct exact T prvalue від factory без окремого named T temporary |
| Destructor | Destroy constructed object і повернути previous cursor |

Construction methods повертають null для invalid або вже constructed lease.
Вони не виконують wire validation. Для bytes використовуйте public `decode`,
який перевіряє representation до construction. `constructFrom` вимагає exact T
від factory; Workspace сам по собі не перетворює factory exceptions на status.
Encoded telemetry callbacks мають свій `noexcept` contract.

Workspace Lease підтримує mutable object із nothrow destructor. Supported wire
shape додатково перевіряє `Type<T>` у codec; reservation сама по собі не робить
довільний object придатним до serialization.

## Alignment, nested leases і lifetime

На fresh Workspace `scratchBytes<T>` враховує worst-case leading padding.
Якщо caller явно забезпечує `alignas(T)`, для однієї reservation достатньо
`sizeof(T)` bytes. Actual cursor залежить від адреси span та alignment T.

```cpp
// Fragment: both leases are released automatically in reverse order.
std::array<std::byte, ts::scratchBytes<Measurement> + ts::scratchBytes<std::uint64_t>> storage{};
ts::Workspace workspace{storage};
{
    auto first = workspace.reserve<Measurement>();
    assert(first.valid());
    const auto outerUsage = workspace.used();
    {
        auto second = workspace.reserve<std::uint64_t>();
        assert(second.valid());
        auto* value = second.constructFrom([]() noexcept -> std::uint64_t {
            return 42;
        });
        assert(value && *value == 42);
    }
    assert(workspace.used() == outerUsage);
}
assert(workspace.used() == 0);
```

Leases і `Decoded<T>` results звільняються тільки LIFO. Nonmoving objects та звичайні nested
scopes дають потрібний порядок. Звільнення outer lease раніше за inner порушує
cursor contract. Не зберігайте decoded pointers після виходу з owning lease
scope та не destroy backing bytes або Workspace під час активного lease.

Одна reservation не pin-ить значення проти application concurrent access.
Workspace cursor, construction/destruction та використання object потребують
узгодженої caller synchronization. Для незалежних concurrent operations зручно
виділити окремий Workspace на кожний execution context.

## Local object budget

Encoded endpoint thunks можуть тримати маленькі owning/decoded objects локально.
За замовчуванням `TELEMETRY_STRUCTURED_LOCAL_BYTES == 32`; public
`ts::maxLocalObjectBytes` має те саме значення. Macro задається однаково в усіх
translation units до включення telemetry headers.

| Encoded payload | Compile-time storage choice |
| --- | --- |
| Owning Field read | Native Value локально, якщо `sizeof(Value)` вкладається в budget; інакше leased Value |
| Borrowed Field read | Getter повертає `const Value&`; value storage не створюється |
| Field write | Decoded native Value локально або leased |
| Command request | Void не створює object; aggregate request локально або leased |
| Service request | Request отримує budget першим |
| Owning Service result | Actual `ServiceResult<Response>` перевіряється проти залишку budget після local request |
| Borrowed / void Service response | Owning response payload storage не створюється |

Розмір wire payload і native object size можуть відрізнятися через padding.
Storage policy використовує native `sizeof`, а transport buffers — `wireSize`.
Для Service важливий розмір actual owning wrapper, бо request живе під час
callback і response serialization. Якщо request leased, весь local budget ще
може бути доступний result wrapper.

Zero budget спрямовує owning/decoded payload objects у Workspace. Borrowed
response view або status bookkeeping не перетворюються на owning payload.
Native typed returns і wire bytes від macro не змінюються. Budget не обмежує
загальний stack frame: compiler padding, spills, wrapper bookkeeping, callback
stack і nested application calls потребують окремої оцінки.

Скористайтеся advertised requirements:

| Metadata | Що потрібно operation |
| --- | --- |
| `FieldEntry::readScratchBytes` | Owning getter storage; borrowed getter має 0 |
| `FieldEntry::writeScratchBytes` | Decoded setter value; read-only Field має 0 |
| `CommandEntry::scratchBytes` | Decoded request storage |
| `ServiceEntry::scratchBytes` | Одночасний request/owning-result storage із alignment allowance |
| `model.maxScratch()` | Fresh Workspace bound для однієї operation будь-якої family |

Fields рекламують read/write requirements окремо: borrowed getter може не
потребувати Workspace, тоді як setter того самого Field декодує великий Value.
Для Values provider потрібне саме Field read scratch; див.
[Descriptor і Values](DescriptorAndValues.md).

## Encoded endpoint boundaries

Checked entries у
[FieldTable.hpp](../../lib/telemetry/field/FieldTable.hpp),
[CommandTable.hpp](../../lib/telemetry/command/CommandTable.hpp) та
[ServiceTable.hpp](../../lib/telemetry/service/ServiceTable.hpp) встановлюють
payload bounds перед викликом typed internal thunk.
[Encoded.hpp](../../lib/telemetry/detail/Encoded.hpp) використовує вже перевірені
extents, зберігаючи bool validation перед construction.

| Operation | Input | Output |
| --- | --- | --- |
| Field read | Немає payload | Capacity не менша за `wireBytes`; записується canonical prefix |
| Writable Field write | Рівно `wireBytes` | Немає response payload; повертається setter status |
| Command | Рівно `requestWireBytes`; void request має 0 | Немає response payload; повертається Command status |
| Service | Рівно `requestWireBytes` | Capacity не менша за `responseWireBytes`; successful response записує canonical prefix |

Read-only Field write повідомляє `ReadOnly` без setter callback. Application
відмова Service має dispatch `Ok`, відповідний `ServiceStatus` і `written == 0`.
Declared successful response size перевіряється до callback, навіть якщо
конкретний callback згодом відмовить або поверне void/status-only result.

| `DispatchStatus` | Значення на endpoint boundary |
| --- | --- |
| `Ok` | Routing/codec завершені; оцініть application endpoint status |
| `NotFound` | Packed group/entry не існує |
| `InvalidPayload` | Неправильний exact input, bool representation або заборонений overlap |
| `BufferTooSmall` | Output capacity менша за declared successful payload |
| `WorkspaceTooSmall` | Required aligned payload storage недоступний |
| `Unavailable` | Selected getter/setter/callback або OwnerSlot порожній |
| `InternalError` | Невідомий Field/Command callback code або порушення internal operation invariant |

Native setter із порожнім slot повертає `WriteResult::Unavailable`. Encoded
adapter виявляє відсутній selected target як `DispatchStatus::Unavailable`.
Якщо engaged setter сам повернув `WriteResult::Unavailable`, dispatch успішний,
а application status — `Unavailable`. Ці два випадки розрізняються полями result.

Encoded tables звільняють leases, які самі придбали, перед поверненням — і на
success, і на refusal. Outer caller leases залишаються активними; `used()`
повертається до cursor, з яким operation увійшла. Повністю local endpoint не
звертається до Workspace storage; його span може бути порожнім.

## Overlap rules

| Шлях | Дозволено | Потрібна disjoint область |
| --- | --- | --- |
| Standalone encode | Будь-який valid native object з окремим output | Усі native bytes T, включно з padding, проти output |
| Standalone decode у existing object | Input із окремого receive buffer | Input проти native bytes T, включно з padding |
| Standalone Workspace decode, обидві форми | Input із окремого receive buffer | Input проти complete Workspace span |
| Endpoint із required scratch | Request/response можуть бути поза Workspace | Wire spans проти complete Workspace span, включно з active outer leases |
| Fully local endpoint | Wire storage може не бути disjoint із невикористаним Workspace | Borrowed native response усе одно не може перетинати output |
| Service request/output | Exact або partial overlap між input та output | Required Workspace scratch та actual borrowed response проти wire output |

Service decode завершується до першого response write, тому один transport
buffer можна використати для input і output. Це не дозволяє overwrite object,
який callback повернув як borrowed response: його actual native address стає
відомою після callback і перевіряється до encode. Заборонений overlap дає
`InvalidPayload` та нуль reported payload bytes.

Complete Workspace span перевіряється цілком, а не тільки вільний хвіст. Це
захищає input/output від active outer object, який nested operation ще використовує.
`buffersDisjoint` перевіряє додатково input проти output, тому він суворіший за
Service boundary. Використовуйте його, коли ваш application buffer layout
вимагає саме трьох disjoint spans.

## Construction та structural limits

Codec заповнює кожний semantic member і не трактує application default member
initializers як telemetry metadata. Existing-object decode не конструює T
повторно й не запускає DMI. Для new T у Workspace trivially default-constructible T створюється
без зайвої value initialization, після чого members перезаписуються. Для
aggregate із DMI neutral construction явно задає кожний member перед decode.

`ts::maxNeutralConstructionNodes == 1024` обмежує саме nontrivial aggregate
construction expansion. Великий trivial array декодується loop і не розгортається
як такий initializer. Це окрема межа від `Limits::maxExpandedValueNodes`.

| Structural bound | Поточне значення |
| --- | ---: |
| Nesting depth | 32 |
| Members одного struct | 256 |
| Elements одного array | 65 536 |
| Canonical value bytes | 1 048 576 |
| Expanded value nodes | 262 144 |
| Nontrivial neutral construction nodes | 1 024 |

Supported representation та recursive limits перевіряються через
[Type<T>](../../lib/telemetry/type/Traits.hpp) під час compilation. Pointers,
references, raw array members, volatile fields і packed member alignment
потребують окремого application snapshot/DTO з підтриманою формою.
`Model` і reflection roles описані у [Model](Model.md).

## Практичний порядок інтеграції

1. Виберіть aggregate request/response та перевірте його `wireSize<T>` і native
   `sizeof(T)`; application validator визначає допустимі values.
2. Для standalone codec виділіть exact wire span і виберіть existing object,
   scoped `Decoded<T>` або explicit fresh lease. Для endpoint
   operations використайте advertised wire/scratch requirements або Model maxima.
3. Зберіть повний transport payload і викличте checked public boundary.
4. Перевірте routing/codec status, потім application status. Використовуйте тільки
   reported `written` bytes і тільки references/pointers із живими owning results/leases.
5. Завершіть result/lease scope перед повторним використанням storage; caller
   synchronization охоплює callback, borrowed response encode і lifetime.

Runtime packet framing та delivery описані у
[Transport walkthrough](TransportWalkthrough.md). Runnable приклади прямого
encoded dispatch містяться у [Encoded.cpp](../../examples/user_guide/Encoded.cpp),
а maintained codec checks — у
[codec runner](../../tests/structured/codec/run.py).
