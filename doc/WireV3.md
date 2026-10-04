# Descriptor і Values: формат v3.0

Автори: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.

[Індекс](README.md) · [Архітектура](Architecture.md) ·
[V3 providers](../lib/resource/telemetry/v3/README.md) ·
[Transport guide](user/TransportAndResources.md)

Це чинний binary contract `descriptor.bin` і `values.bin`. Його constants,
dependency pins та незалежні golden fixtures зафіксовані у
[freeze manifest](../tests/structured/freeze/contract.json) та
[compile-time contract](../tests/structured/freeze/Contract.cpp).
In-memory C++ ABI revision 6 має окремий номер; canonical wire лишається 3.0.
Transport framing і optional Bind/Exchange описані окремо.

## Навігація

- [Загальні правила](#загальні-правила)
- [Descriptor header](#descriptor-header)
- [Record header](#record-header)
- [Type records](#type-records)
- [Catalog і endpoint records](#catalog-і-endpoint-records)
- [Fingerprint](#fingerprint)
- [Default technical ceilings](#default-technical-ceilings)
- [Values header і tokens](#values-header-і-tokens)
- [Читання Values порціями](#читання-values-порціями)
- [Providers і клієнт](#providers-і-клієнт)

## Загальні правила

- Багатобайтові integers — little-endian; counts, offsets, lengths і TypeId
  — u32, якщо явно не зазначено інше. Fingerprint — u64.
- String — `u32 byteLength` та рівно стільки UTF-8 bytes без NUL terminator.
  Embedded NUL і некоректний UTF-8 відхиляються.
- Між records немає padding. Native pointers, `size_t`, C++ object layout,
  alignment і struct padding не експортуються.
- Nonzero reserved bits/bytes, невідомі record kinds/flags/version та
  непідтримувана version header відхиляються клієнтом v3.0.
- Endpoint/group names непорожні. Endpoint names у catalog унікальні;
  catalog names унікальні в межах категорії. Різні категорії можуть мати
  однакові імена.
- Counts, offsets, record lengths і type references перевіряються до
  використання чи allocation. Розмір повного файла має відповідати header.

## Descriptor header

64 bytes:

| Offset | Тип | Значення |
| --- | --- | --- |
| 0 | 4 bytes | ASCII `TDS3` |
| 4 | u16 | major = 3 |
| 6 | u16 | minor = 0 |
| 8 | u16 | headerBytes = 64 |
| 10 | u16 | flags = 0 |
| 12 | u32 | totalBytes |
| 16 | u64 | descriptorFingerprint |
| 24 | u32 | typeCount, включно з Void і builtins |
| 28 | u32 | fieldCatalogCount |
| 32 | u32 | fieldCount |
| 36 | u32 | commandCatalogCount |
| 40 | u32 | commandCount |
| 44 | u32 | serviceCatalogCount |
| 48 | u32 | serviceCount |
| 52 | u32 | typesOffset |
| 56 | u32 | catalogsOffset |
| 60 | u32 | endpointsOffset |

Canonical order: Header → Types → Catalogs → Endpoints. Усередині Catalogs
і Endpoints порядок Field → Command → Service, далі group/entry position.
Порожні sections можуть мати однаковий start offset; sections не
перекриваються й закінчуються на totalBytes.

## Record header

8 bytes перед кожним descriptor payload:

| Offset | Тип | Значення |
| --- | --- | --- |
| 0 | u8 | kind: Type=1, Catalog=2, Field=3, Command=4, Service=5 |
| 1 | u8 | recordVersion = 1 |
| 2 | u16 | flags = 0 |
| 4 | u32 | payloadBytes, без 8-byte header |

Length визначає межу record. Воно не дозволяє мовчки прийняти невідому
семантику. Правила нового kind/minor потребують окремого version contract.

## Type records

Спільний 12-byte prefix:

```text
u32 typeId
u8  typeKind       // Void=0, Scalar=1, Enum=2, Struct=3, Array=4
u8  reserved[3]    // zero
u32 wireBytes
```

| Kind | Payload після prefix |
| --- | --- |
| Void | Немає; typeId=0, wireBytes=0 |
| Scalar | `u8 scalarCode`, 3 reserved zero bytes |
| Enum | `u32 underlyingTypeId`, `u32 entryCount`, далі `{code bytes, name string}` |
| Struct | `u32 memberCount`, далі `{u32 memberTypeId, memberName string}` |
| Array | `u32 elementTypeId`, `u32 elementCount` |

Scalar codes:

| Код | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Тип | Bool | U8 | S8 | U16 | S16 | U32 | S32 | U64 | S64 | F32 | F64 |

Bool payload — один byte 0 або 1. Integer payload займає 1/2/4/8 bytes,
signed representation — two's complement; floating payload — IEEE
binary32/binary64. Enum code займає розмір integer underlying type й
зберігає його signed representation. Representable code не зобов'язаний
бути членом словника: application validation визначає допустимі states.

TypeIds щільні й у postorder: складний type посилається на вже описані
менші TypeIds. Немає cycle, forward reference, duplicate/missing TypeId
або implicit alias record. Enum underlying — integer; Array element і
Struct member не можуть бути Void. WireBytes повторно обчислюється з
графа й порівнюється із записаним значенням.

Формат не має typeName. Consumer використовує endpoint/member names.
Structural identity на wire та exact C++ identity для native API — різні
контракти; два C++ structs однакової форми не стають exact native type.

## Catalog і endpoint records

Catalog payload:

```text
u8  category      // Field=1, Command=2, Service=3
u8  reserved[3]   // zero
u32 groupIndex
u32 firstEntry    // ordinal у відповідній категорії endpoint records
u32 entryCount
string name
```

Field payload:

```text
u32 packedId
u32 valueTypeId
u8  capabilities // Readable=1, Writable=2
u8  reserved[3]  // zero
string name
```

Command payload:

```text
u32 packedId
u32 requestTypeId // 0 for a zero-argument command
string name
```

Service payload:

```text
u32 packedId
u32 requestTypeId
u32 responseTypeId
string name
```

PackedId: high 16 bits — group position, low 16 bits — entry position.
Категорії мають окремі ID spaces. Усі endpoint records закінчуються після
name: semantic units, defaults, limits та metadata policy хвоста немає.
Request/Response members описує TypeRegistry. Порожні tables дозволені;
endpoint holes чи reserved placeholder entries не вводяться.
Late-bound slot state не змінює descriptor або fingerprint.

## Fingerprint

FNV-1a-64 над усіма canonical descriptor bytes, із bytes 16..23 власного
fingerprint заміненими на нулі. Basis — `0xcbf29ce484222325`, prime —
`0x100000001b3`; unsigned arithmetic modulo 2^64.

Hash включає headers, names, enum dictionary, type shapes, endpoint order
і Field capabilities. Поточні values, owner/callback addresses та slot
state не входять. Descriptor кешує fingerprint один раз; Values header
копіює його. Значення не перехешовуються після кожного READ/write/call.

Це identity опису, а не checksum live payload або доказ відсутності
колізій. Transport integrity, access policy та момент порівняння
fingerprint визначає application. Для offline export збережіть обидва
файли: `values.bin` сам не описує своїх типів.

## Default technical ceilings

Ці 11 default constants у `telemetry::Limits` обмежують складність та
обсяг type model/parser. Вони не визначають допустимі прикладні values,
не записуються в endpoint records і не змінюють fingerprint. Зміна
default contract потребує явного перегляду compatibility та freeze gates.

| Constant | Default | Що обмежує |
| --- | ---: | --- |
| `maxTypeDepth` | 32 | Вкладені Struct/Array; scalar/enum depth 0 |
| `maxTypeCount` | 4096 | Усі types, включно з Void/builtins |
| `maxStructMembers` | 256 | Members одного struct |
| `maxArrayElements` | 65536 | Elements одного fixed array |
| `maxDescriptorBytes` | 4194304 | Повний descriptor |
| `maxStringBytes` | 4096 | Один string |
| `maxEnumEntriesTotal` | 65536 | Сума enum dictionary entries |
| `maxCatalogCountTotal` | 65536 | Catalogs усіх категорій разом |
| `maxEndpointCountTotal` | 65536 | Endpoints усіх категорій разом |
| `maxValueWireBytes` | 1048576 | Один payload type; transport capacity окрема |
| `maxExpandedValueNodes` | 262144 | Розгорнута складність одного value |

Expanded nodes: scalar/enum=1, Void=0, Struct=1+сума member nodes,
Array=1+N×element nodes. Overflow і ceiling перевіряються перед
додаванням/множенням, включно з zero-wire-size aggregate types.

Descriptor і JS client підтримують явні profiles у своїх documented
interfaces. Profile не дозволяє unsupported wire types; client з нижчими
ceilings явно відхиляє завелику модель. Деталі профілів — у
[provider guide](../lib/resource/telemetry/v3/README.md#validation-and-limits).

## Values header і tokens

24-byte header:

| Offset | Тип | Значення |
| --- | --- | --- |
| 0 | 4 bytes | ASCII `TVL3` |
| 4 | u16 | major = 3 |
| 6 | u16 | minor = 0 |
| 8 | u32 | fieldCount |
| 12 | u32 | totalBytes |
| 16 | u64 | descriptorFingerprint |

Далі Fields у descriptor group/entry order:

```text
u8 readStatus
wireSize<T> payload bytes
```

ReadStatus: Ok=0, Unavailable=1; інші коди відхиляються. Відсутній
slot/target дає Unavailable. Getter повертає `T` або `const T&`; окремого
Field failure wrapper немає. При Unavailable payload того самого розміру
заповнений нулями й не інтерпретується як value.

Загальний розмір: `24 + sum(1 + wireSize<T_i>)`. Empty Field catalogs дають
тільки header; zero-wire-size struct усе одно має status byte.
Consumer звіряє fingerprint із descriptor перед decode, а не вгадує
схему за fieldCount чи розміром файла.

## Читання Values порціями

До getter provider перевіряє місце для цілого token і потрібний scratch.
Getter викликається один раз; payload повністю кодується до просування
cursor. Якщо token не вміщується, він не починається: getter не
викликається й cursor цього token не рухається. Уже завершений prefix
повертається з Ok; без progress — BufferTooSmall.

Owning getter дає snapshot одного `T`; borrowed getter позичає object
на час цілого encode. Application гарантує lifetime і стабільність
referent. Whole-token transfer не створює одночасний snapshot усіх Fields.

Допустимий Values cursor — позиція всередині immutable 24-byte header,
початок повного token або EOF. Cursor усередині payload — InvalidCursor.
Header можна розрізати; tokens не розрізаються. Header і наступні повні
tokens можуть бути видані одним READ. EOF дає 0 bytes та eof=true.
`size()`, STAT і побудова offset index не викликають getters.

`requiredWorkspace()` враховує scratch/alignment; `maxTokenSize()` задає
мінімальну READ payload capacity для найбільшого Field. Недостатній
Workspace — InternalError. Якщо є завершений prefix, READ повертає його
й повторення від nextCursor дістається до помилки без replay getters.
Output overlap із Workspace-backed Values повертає InvalidData до запису
header. Повністю local файл Workspace не використовує.

## Providers і клієнт

DescriptorFile читається з будь-якого byte offset, включно з серединою
record/string. Immutable metadata й names живуть протягом усіх reads.
ValuesFile копіює Field index і fingerprint із Descriptor, але позичає
underlying tables/names та Workspace. Обидва providers read-only.
Parallel reads, зокрема через копії provider, потребують окремого
Workspace або зовнішньої серіалізації.

Чинні includes, приклад `packDescriptor` та file registration наведені
у [provider guide](../lib/resource/telemetry/v3/README.md).
[JS client](../tests/structured/client/README.md) незалежно перевіряє
descriptor graph, canonical sizes, fingerprint і Values tokens.
Transport framing, resource packet header та контрольні запити — окремі
формати в [resource protocol](../lib/resource/protocol/README.md) і
[Bind/Exchange example](../examples/structured_protocol/README.md).
