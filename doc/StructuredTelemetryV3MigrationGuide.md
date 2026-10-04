# Перенести consumer на поточний Telemetry v3

Автори: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.

[Індекс](README.md) · [Посібник](user/README.md) · [Архітектура](Architecture.md) ·
[API](../lib/telemetry/README.md) · [Wire v3.0](WireV3.md)

Це чинна карта перенесення consumer. Поточний repository має один C++20
`telemetry`, exact native values, owning/borrowed outputs та binary wire
v3.0. Source, in-memory ABI і wire compatibility з попереднім Scalar/v2.1
API не надаються. Нове підключення починайте з
[GettingStarted](user/GettingStarted.md).

## Навігація

- [Includes, namespace і build](#includes-namespace-і-build)
- [Declarations і callback signatures](#declarations-і-callback-signatures)
- [Native access та lifetimes](#native-access-та-lifetimes)
- [Encoded transport і resources](#encoded-transport-і-resources)
- [Перевірка перенесення](#перевірка-перенесення)
- [Походження попередніх результатів](#походження-попередніх-результатів)

## Includes, namespace і build

| Попередня назва/шлях | Чинна інтеграція |
| --- | --- |
| `lib/telemetry_structured/Structured.hpp` | `<telemetry/Telemetry.hpp>` із `lib` в include paths |
| `telemetry::structured` | `telemetry`; `namespace ts = telemetry` може бути application alias |
| `structured.pri` | `lib/telemetry/telemetry.pri` |
| `lib/resource/structured/` | `<resource/telemetry/v3/...>` |
| `resource::structured` | `resource::telemetry::v3` |
| `resource_structured`, `resource_telemetry_v3` | Явний `CONFIG += resource_telemetry` перед `resource.pri` |
| `telemetry_no_json`, старий JSON/v2 backend | Configuration прибрана; consumer використовує native API чи v3 codec |
| `web/telemetryStructured.js` | `web/telemetry.js` |

Потрібні C++20 і include paths для `lib`, `lib/boost_pfr/include` та
`lib/magic_enum`. Compiled telemetry sources —
`lib/telemetry/abi/StructuredAbi.cpp` і `lib/telemetry/model/Adapter.cpp`.
Resource packet layer додає `lib/resource/protocol/Protocol.cpp`; v3 Values
provider — `lib/resource/telemetry/v3/detail/Values.cpp`.

Qmake manifests вибирають ці sources; generic resource manifest не
підключає telemetry назад. Optional
[protocol.pri](../examples/structured_protocol/protocol.pri) додається явно.
Повні portable/qmake/CMake кроки й готовий project наведені у
[GettingStarted](user/GettingStarted.md#крок-2-підключіть-source-та-include-paths)
та [device example](../examples/device_integration/README.md).

Не змішуйте старі та нові object files, namespace definitions або headers
в одному executable. Перезберіть усі TU з однаковими configuration macros.
Чинна structured ABI revision — 6; default local-object budget — 32 B.
Exact layout tag на compiled boundary виявляє ABI mismatch при linking.
Wire version 3.0 від цього має незалежну identity.

## Declarations і callback signatures

| Попередня поведінка | Чинне рішення |
| --- | --- |
| Getter/setter через універсальний Scalar container | Getter exact `T` або `const T&`; setter exact `T`/`const T&` → `WriteResult` |
| Неявні numeric conversions | Exact typed доступ або явні checked `readAs`/`writeAs` |
| Кілька positional Command arguments | Один supported aggregate Request або відсутній Request |
| Service з прикладним output | Нуль/один aggregate Request; aggregate Response або void; owning/borrowed result |
| Unit/default/limits/flags у declaration | Declaration тільки name + binding; перевірки й UI semantics у application |
| Enum metadata/default | Scoped enum з supported underlying integer; за потреби explicit `EnumReflection` dictionary |
| Late-bound callable/owner | Чинні FunctionSlot/ContextFunctionSlot/DelegateRefSlot/DelegateSlot/OwnerSlot |

Callbacks `noexcept`. Command повертає `CommandResult`; `Accepted` означає
прийняту дію, а не гарантоване завершення. Service повертає
`ServiceResult<Response>` або `BorrowedServiceResult<Response>`; status і
payload перевіряються разом. Getter `const T&` дає `BorrowedValue<T>`.

Units, display labels, defaults, допустимі enum codes та business limits
перенесіть у callback/UI/application storage, якщо consumer їх потребує.
Flag на старій declaration сам по собі не був реалізацією persistence.
Явно збережіть реальну application behavior й refusal-before-mutation.

`TypeKind::Scalar` і `ScalarCode` лишаються чинними leaf type categories
codec. Це не колишній Scalar value container. Повні supported types і
binding forms — у [Native API](user/NativeApi.md).

## Native access та lifetimes

Збережіть local table ordering та packed global IDs, якщо вони потрібні
consumer. Local Position вибирає entry; PackedId містить 16-bit group та
16-bit entry. Для зовнішніх значень перевіряйте ширину до cast; використовуйте
`tryMakeId`, а lookup нехай перевіряє actual catalog bounds.

Compile-time `read/write/call`, typed `forEach`, runtime `visit` та erased
encoded API — різні шляхи до тих самих endpoints.
`forEach`, range-for і `get` самі не виконують callbacks.
Numeric `As` conversions checked; struct/array access потребує exact C++ T.

Owners, names, callable lvalues, slots, tables і views мають стабільні
адреси та достатній lifetime. Після переприв'язування slot external
synchronization лишається application responsibility.
Borrowed results не подовжують referent lifetime й не утворюють snapshot.
`readAs<T>` явно повертає owning copy. Lock для borrowed/encoded доступу
охоплює весь період використання const object, а не тільки getter.

[Application integration](user/ApplicationIntegration.md) і
[borrowed contract](BorrowedNativeValues.md) показують практичні lifetime,
snapshot та synchronization рішення.

## Encoded transport і resources

Віддалений consumer спочатку отримує complete packet. Encoded Model
adapter приймає endpoint ID, payload bytes, response span і caller-owned
Workspace. Core не володіє connections, Ready, requestId, retries або
descriptor agreement state.

Descriptor/Values paths обирає application; готовий приклад використовує
`/telemetry/descriptor.bin` і `/telemetry/values.bin`. Замість старих
schema/commands files consumer читає один descriptor і v3 Values.
File indexes — позиції саме поточного filesystem; знаходьте path через
LIST, якщо layout не закріплений вашим application contract.

Перевірте receive/reply sizes, `model.maxScratch()` та для Values
`requiredWorkspace()`/`maxTokenSize()`. Local-object budget не є packet
capacity. Один Values token неподільний; READ payload має вміщати
найбільший Field. Resource cursor — provider-owned state; продовжуйте
операцію з повернутого `next`, а не з самостійно вгаданого offset.

Descriptor/Values fingerprint та canonical bytes лишаються wire v3.0
після додавання borrowed outputs. In-memory ABI змінюється окремо.
Зберігайте дві перевірки: dispatch та endpoint status; віддавайте рівно
committed payload bytes. Деталі — у [wire reference](WireV3.md),
[transport walkthrough](user/TransportWalkthrough.md) та
[optional protocol example](../examples/structured_protocol/README.md).

## Перевірка перенесення

1. Зберіть і виконайте [QuickStart](../examples/user_guide/QuickStart.cpp)
   потрібним compiler; перезберіть весь consumer із current headers/sources.
2. Перевірте ваші getter/setter/Command/Service: exact types, status,
   refusal-before-mutation та required side effects.
3. Перевірте IDs, lifetime, borrowed data stability, synchronized reads,
   повний frame receive/reply та capacities.
4. Для custom UI перевірте збереження units/defaults/labels/persistence;
   descriptor передає форму даних, ці semantics належать consumer.
5. Виконайте відповідні [maintained checks](../tests/README.md) та власну
   application integration перевірку. Host чи ARM compile evidence не
   означає runtime перевірки вашої MCU прошивки.

## Походження попередніх результатів

[Stage 15 freeze checkpoint](StructuredTelemetryV3FreezeQualification.md)
і [Stage 20 qualification](StructuredTelemetryV3FinalQualification.md)
описують свої recorded source identities, measured evidence та CI.
Їхні результати не є автоматичним підтвердженням нового consumer чи commit.

Попередній великий stage-by-stage migration document збережено byte for
byte у зовнішньому архіві перед заміною цією current API картою.
Inventory і перевірку архіву описує
[Repository maintenance](RepositoryMaintenance.md). Receipts не
перепозначаються під новий source SHA.
