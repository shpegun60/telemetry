# Structured v1 / wire v3.0: Stage 15 contract candidate

Автори: Ruslan Kovtun (shpegun60), codexAi. Дата: 2026-10-03.

Цей документ фіксує програмну частину Stage 15 за
[implementation plan](StructuredTelemetryV3ImplementationPlan.md).
Контракт C++20 structured v1 та wire v3.0 зафіксовано як кандидат на freeze.
Повний Definition of Done Stage 15 залишається відкритим: нові Stage 14
probes ще не виконані на потрібній H7S. Апаратні результати можуть повторно
відкрити контракт, якщо потребуватимуть зміни API або wire.

| Частина | Статус | Доказ |
| --- | --- | --- |
| Контракт API/wire | Зафіксований кандидат; локальні gates пройдено | Новий freeze suite, незмінні goldens, попередні semantic suites |
| Повна програмна матриця CI | Перевіряється для SHA публікації цього зрізу | Нові host/ARM steps і збережені artifacts |
| Stage 14 hardware qualification | OPEN / deferred | Потрібна первісна H7S; нових MCU циклів або watermarks немає |
| Повний Stage 15 DoD | OPEN | Вимогу реального MCU receipt із §18.1 не вилучено |
| Міграція 16–20 | Ще не розпочата | Старий scalar/v2.1 baseline збережено |

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
- Boost.PFR boost-1.92.0, exact commit/header tree/license digests;
  magic_enum 0.9.8, exact commit/header digest, default scan -128..127.

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
| Public API/wire/dependency pins, чисті `.pri` consumers | [freeze suite](../tests/structured/freeze/README.md), qmake qualification/client та чотири resource selections |
| Реальний MCU receipt нових probes, whole-call-chain stack/cycles | Відкладено до Stage 14 на потрібній H7S |

Локальна нова матриця: MinGW 13.1, GCC 13.3 з null checks, Clang 18.1
ASan/UBSan, CubeIDE ARM 14.3.1 normal/null, ARM 13.2.1 normal/null;
усі O2/Os/Og. Host виконує 29 conditions на optimization, разом 87 на run.
ARM лише компілює, лінкує й читає nm/objdump/size/.su: його runtime count
дорівнює нулю. Кожен run також відкидає 10 invalid declarations з потрібною
діагностикою. Це contract gates, а не десять нових runtime findings.

Чисті нові build directories із Qt 6.10.1 / MinGW 13.1 перевіряють
multi-TU consumer (97 conditions), Qt client (exact 41-byte response),
core-only/v2-only/v3-only/both resource selections. Makefile нового consumer
не містить legacy JSON/v2 providers або optional Bind/Exchange.
Окремі examples і browser interoperability також залишаються в CI.

Виміряні linked sections та individual ARM frames з compiler/flags/input
hashes вже наведено в [MCU evidence](../tests/structured/mcu/README.md).
Для 4 KiB Field/Command/Service CubeIDE frames O2 становлять 192/176/216 B,
Os — 168/152/192 B. Це окремі compiler frames; їх не перейменовуємо на
максимальний live stack усього call chain. У цих прогонах немає MCU циклів.
Старі H7S receipts підтверджують свої попередні образи та restore;
вони не доводять виконання нових Stage 14 або Stage 15 probes.

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

Після exact-SHA CI програмний контракт лишається frozen candidate.
Для повного закриття Stage 15 потрібні новий Stage 14 MCU receipt,
backup/restore і actual call-chain stack/cycle evidence. Міграція 16–20
не запускається під виглядом цієї програмної кваліфікації.
