# Structured v1 / wire v3.0: Stage 15 freeze

[Documentation index](README.md) · [User guide](user/README.md) · [Architecture](Architecture.md) · [Wire v3.0](WireV3.md)

Автори: Ruslan Kovtun (shpegun60), codexAi. Дата: 2026-10-03.

Це історичний Stage 15/20 checkpoint. Recorded source identities, ABI revision
і статуси нижче стосуються цього зрізу. Чинні контракти описують
[архітектура](Architecture.md), [Wire v3.0](WireV3.md) та
[freeze manifest](../tests/structured/freeze/contract.json); наступний
[borrowed-result slice](BorrowedNativeValues.md) має окремі результати.

Цей документ фіксує програмну частину Stage 15 та подальший Stage 20 baseline.
Контракт C++20 structured v1 та wire v3.0 заморожено.
Stage 14 probes виконано на потрібній H7S без зміни runtime бібліотеки.
Повний Definition of Done Stage 15 закрито: [CI 37145176280](https://github.com/shpegun60/telemetry/actions/runs/37145176280)
для `389c995083b4dc0a39cd8775b211ef765494240f` завершився success, 9/9 jobs.
Нових API/wire blockers апаратний прогін не
виявив; це не обіцянка відсутності всіх можливих дефектів.
Це історичний Stage 15 gate. Stage 16–19 реалізовано, а Stage 20
local/software/hardware qualification кінцевого дерева завершено.
Чотири Stage 20 H7S receipts зафіксували code HEAD
`01fd180ff3678d7c47066f8a728dc1e02f0f3973`. Завершений publication
exact-SHA gate цього baseline та його межі наведено у
[final qualification](StructuredTelemetryV3FinalQualification.md).

| Частина | Статус | Доказ |
| --- | --- | --- |
| Контракт API/wire | Freeze прийнято | Freeze suite, незмінні goldens, semantic suites |
| Повна програмна матриця CI | PASS, exact `389c995`, 9/9 jobs | CI 37145176280, host/ARM artifacts |
| Історична Stage 14 hardware qualification | PASS для captured `37bc857` | [Archived results](evidence/pre-unification/stage14/RESULTS.md), 30128 conditions, restore, cycles та PSP observations |
| Повний Stage 15 DoD | PASS | MCU receipt із §18.1 та exact-SHA CI |
| Міграція 16–20 | Stage 16–20 закрито в наступному baseline checkpoint | [Final qualification](StructuredTelemetryV3FinalQualification.md); source identities розділено |

## Навігація

- [Межі зафіксованого API](#межі-зафіксованого-api)
- [Wire і залежності](#wire-і-залежності)
- [Програмні докази та їхні межі](#програмні-докази-та-їхні-межі)
- [Lifetime, concurrency і storage](#lifetime-concurrency-і-storage)

## Межі зафіксованого API

```text
C++20 DTOs + callable signatures
             |
      normalized reflection facade
             |
    one shape-only TypeRegistry
             |
 FieldTable / CommandTable / ServiceTable
             |
         catalogs + Model
          /           \
 native typed       checked encoded indexes
 read/write/As      canonical LE + bounded storage
          \           /
   resource descriptor / dense values

Application owns transport, peer state and agreement policy.
Optional protocol/client examples are separate consumers.
```

Field, Command і Service оголошуються через `field`, `command`, `service`:
тільки назва та binding. Unit, limits, defaults, semantic argument/member
metadata не входять у structured v1. Getter повертає exact native `T`;
setter приймає той самий `T` або `const T&` і повертає `WriteResult`.
Command має void або один aggregate Request і повертає `CommandResult`.
Service має void або один aggregate Request/Response; public call повертає
`ServiceResult<Response>`. Targets мають бути noexcept.

Усі три families мають `get`, ordered `forEach`, runtime `visit`,
`empty/begin/end/operator[]`. Відвідування metadata не викликає endpoints
і не матеріалізує їхні значення. Локальний template position належить своїй
таблиці; глобальний `makeId(group, entry)` — u32 з двома 16-bit компонентами.
Runtime routing має прямий доступ до масивів із перевіркою меж.

Field `read/write` зберігають exact type; `readAs/writeAs` явно дозволяють
checked numeric conversion. Arrays/structs потребують exact C++ type:
інша структура з такими самими members не є автоматично сумісною.
Runtime mismatch не викликає джерело й повертає nullopt/InvalidValue.
Legacy Scalar не є fallback нового native чи encoded шляху.

## Wire і залежності

[Contract manifest](../tests/structured/freeze/contract.json) та
[compile-time contract](../tests/structured/freeze/Contract.cpp) фіксують:

- Descriptor `TDS3`, 64-byte header; Values `TVL3`, 24-byte header.
- Version 3.0, record version 1, 8-byte record header; усі record/category,
  scalar/type codes, Readable/Writable bits і ValueStatus codes.
- Canonical LE без C++ padding; u32 TypeId/packed endpoint ID.
- FNV-1a-64 constants і cached descriptor fingerprint у Descriptor/Values.
  Це fingerprint моделі, а не хеш мінливих значень або поле кожного пакета.
- Independent descriptor edge/empty/mixed та dense values goldens.
- Structured ABI revision 5; default local object budget 32 B.
- Усі 11 default technical ceilings із `telemetry::Limits`
  (до Stage 17 — `telemetry::structured::Limits`).
  Вони належать до зафіксованого v1 acceptance contract: зміна навіть одного
  значення потребує явного перегляду сумісності та оновлення контракту.
  Це не semantic min/max/default значень endpoint-ів. Явні application
  profiles в інтерфейсах, які вже їх підтримують, залишаються окремими від
  default profile; нижчі межі клієнта можуть явно відхиляти завеликі моделі.
  Значення ceilings не додаються у wire або fingerprint.
- Boost.PFR boost-1.92.0, exact commit/header tree/license digests;
  magic_enum 0.9.8, exact commit/header/license digests, default scan -128..127.

Нова числова таблиця wire потребує явної зміни контракту й goldens,
а не тихого оновлення очікувань тестів. TypeRegistry/Codec/Model/Descriptor
споживають normalized facade. PFR та magic_enum залишаються C++20 backend;
std::meta не є залежністю MCU. Factory/shape rules не розширюються лише
через появу іншого reflection backend.

## Програмні докази та їхні межі

| Вимога §18.1 | Власник доказів |
| --- | --- |
| Mixed scalar/enum/array/struct Fields, exact native values, bindings/slots | [Stage 08](../tests/structured/README.md#stage-08-mixed-field-command-and-model), [Stage 06](../tests/structured/README.md#stage-06-native-service-binding), [Stage 07](../tests/structured/README.md#stage-07-service-tables-and-first-encoded-model-path) |
| Reflection boundary, callable qualifiers, enums/aliases/empty dictionaries, technical ceilings | [Stage 02](../tests/structured/README.md#stage-02-stable-reflection-facade), [Stage 03](../tests/structured/README.md#stage-03-fixed-wire-types-and-enum-dictionaries), [Stage 05](../tests/structured/README.md#stage-05-compile-time-typeregistry) |
| Canonical LE, representation/lifetime, large return ABI, bounded scratch | [Stage 04](../tests/structured/README.md#stage-04-codec-workspace-and-object-lifetime), [qualification](../tests/structured/qualification/README.md), [MCU offline probes](../tests/structured/mcu/README.md) |
| Cross-TU ABI, GC/LTO/PIC/PIE, typed direct-call codegen, no-heap symbols | [qualification](../tests/structured/qualification/README.md), [MCU seven-role receipt](../tests/structured/mcu/local-receipt.json) |
| Descriptor/fingerprint/values byte contract і одноразове читання live token | [descriptor](../tests/structured/descriptor/README.md), [resources](../tests/structured/resources/README.md) |
| Traversal, As conversion, runtime bounds, borrowed lifetime | [traversal](../tests/structured/traversal/README.md), новий [freeze contract](../tests/structured/freeze/README.md) |
| JS/Qt client без ручного serializer для кожного DTO, optional packet layer поза core | [client](../tests/structured/client/README.md), [examples](../examples/structured_client/README.md), [exchange](../tests/structured/exchange/README.md) |
| Public API/wire/dependency pins, чисті `.pri` consumers | [freeze suite](../tests/structured/freeze/README.md), qmake qualification/client та core-only/v3 resource selections |
| Історичний Stage 15 MCU gate, whole-call-chain stack/cycles | [Archived H7S results](evidence/pre-unification/stage14/RESULTS.md), [195-input receipt](evidence/pre-unification/stage14/receipt.json) |
| Current final-source MCU/Descriptor/Values/Bind-Exchange qualification | [Final qualification](StructuredTelemetryV3FinalQualification.md), [current H7S results](../tests/structured/mcu/h7s/RESULTS.md) |

Історична freeze-матриця Stage 15: MinGW 13.1, GCC 13.3 з null checks, Clang 18.1
ASan/UBSan, CubeIDE ARM 14.3.1 normal/null, ARM 13.2.1 normal/null;
усі O2/Os/Og. Host виконує 29 conditions на optimization, разом 87 на run.
ARM лише компілює, лінкує й читає nm/objdump/size/.su: його runtime count
дорівнює нулю. Кожен run також відкидає 11 invalid declarations з потрібною
діагностикою та 11 незалежних змін expected ceilings. Лічильники цих двох
груп відмов окремі від runtime checks. Це contract gates, а не нові runtime findings.

На Stage 15 чисті build directories із Qt 6.10.1 / MinGW 13.1 перевіряли
multi-TU consumer (97 conditions), Qt client (exact 41-byte response),
тодішні core-only/v2-only/v3-only/both resource selections. Makefile нового consumer
не містить legacy JSON/v2 providers або optional Bind/Exchange.
Окремі examples і browser interoperability також залишаються в CI.
Після Stage 19 legacy v2 і combined selections видалено; кінцевий qmake
runner перевіряє core-only і v3 selections. Повторну програмну матрицю
кінцевого дерева зафіксовано у [final qualification](StructuredTelemetryV3FinalQualification.md).

Історичні linked sections та individual ARM frames з compiler/flags/input
hashes збережено в [Stage 14 offline evidence](evidence/pre-unification/stage14/mcu-offline-README.md).
Для 4 KiB Field/Command/Service CubeIDE frames O2 становлять 192/176/216 B,
Os — 168/152/192 B. Це окремі compiler frames; їх не перейменовуємо на
максимальний live stack усього call chain. У цих прогонах немає MCU циклів.
Старі H7S receipts підтверджують свої попередні образи та restore.
Історичний Stage 14 receipt доводить виконання captured sources `37bc857`:
195 inputs збігаються з Git blobs цього SHA, а `source_dirty=true` чесно
зберігає наявність 92 untracked review files поза build inputs. 840 DWT
windows і 744 PSP observations пройшли незалежну перевірку; 70 mutations
receipt відхилено. Flash backup/readback по 65536 B збігаються.
4 KiB encoded roots показали 244–360 B observed full-chain stack;
owning runtime `readAs<Big>` — 8256/8264 B. Ці watermarks не враховують
untouched reserved slots, caller MSP та interrupts і не є універсальною
worst-case межею. Однакові native instruction streams у різних адресах
образу дали різні цикли; instruction equivalence не означає cycle equivalence.

Stage 20 final-source receipts окремі від попередніх baseline measurements:
[MCU](evidence/pre-borrowed/h7s/mcu/receipt.json),
[Descriptor](evidence/pre-borrowed/h7s/descriptor/receipt.json),
[Values/resources](evidence/pre-borrowed/h7s/resources/receipt.json) і
[Bind/Exchange](evidence/pre-borrowed/h7s/exchange/receipt.json).
Усі чотири завершені з повним restore/readback 65536 B. Їхні captured LF
inputs — відповідно 140/133/133/140 — збігаються з Git blobs code HEAD
`01fd180`; captured code після вимірювань не змінювався.
Глобальний `source_dirty=true` збережено через unrelated untracked review
tree; це не clean-tree claim. Fresh MCU run виконав 29098 conditions,
784 DWT windows і 696 stack observations. Stage 20 cycles і observed
full-chain stack наведено в [H7S results](../tests/structured/mcu/h7s/RESULTS.md);
історичні 30128 conditions і prior H7S cycles не перепозначено як нові.

## Lifetime, concurrency і storage

- Names, owners, stateful callables, slots, tables та metadata views мають
  залишатися живими під час використання. Заборона видимих temporaries
  не може перевірити довільний helper, який приховав dangling reference.
- Tables не копіюються й не переміщаються. Catalog/Model/index views
  запозичують їх; ці view не подовжують lifetime.
- Прямий коректний object reference не потребує null check. Late-bound
  targets перевіряються; rebind/reset та виклики синхронізує application.
- Workspace належить caller. Leases звільняються LIFO, один active call
  має ексклюзивний scratch. Workspace не є thread-safe allocator.
- Compile-time 32-B budget змінює лише storage. Fully local endpoints
  не читають Workspace; великі objects використовують caller scratch.
  Validation, supported types і wire bytes від порога не змінюються.
- Service дозволяє input/output overlap після повного decode Request.
  Перетин wire buffers зі scratch перевіряється, коли scratch потрібний.
- Native `readAs<Big>` повертає явно запитаний owning `optional<Big>`.
  Application відповідає за його storage; bounded encoded frames не є
  обіцянкою маленького stack для такого native call або user target.
- Header-only boundaries потребують явного ABI anchor, який лінкер
  зберігає. Compiled adapters мають exact tag у символах. Однаковий ABI
  у всіх TU одного executable залишається вимогою application build.

Stage 15 завершено з історичним exact `389c995` CI 9/9 і його MCU receipt.
Міграція 16–19 реалізована зі збереженням frozen contract; підсумковий
Stage 20 publication gate зафіксовано у
[final qualification](StructuredTelemetryV3FinalQualification.md).
Для нового commit потрібний його власний exact-SHA CI.
