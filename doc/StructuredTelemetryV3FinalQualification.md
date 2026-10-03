# Telemetry v3: фінальна кваліфікація Stage 20

Автори: Ruslan Kovtun (shpegun60), codexAi. Дата: 2026-10-03.

Stage 16–19 реалізовано. Локальні host, sanitizer, ARM, Qt, browser та
апаратні перевірки кінцевого дерева пройшли. Публікаційний gate — повний
зелений CI для SHA коміту з цією міграцією та evidence. До його завершення
цей документ не проголошує exact-SHA CI успішним.

Код, який виконувала H7S:
`01fd180ff3678d7c47066f8a728dc1e02f0f3973`. Наступний evidence-коміт
публікує результати й документацію; бібліотеку та captured build inputs
після цих вимірювань не змінюємо.

## Кінцевий результат міграції

- Один C++20 core у `lib/telemetry`, umbrella `Telemetry.hpp`, namespace
  `telemetry` та один shape-only `TypeRegistry` для Field/Command/Service.
- Native values зберігають exact `T`; runtime `readAs`/`writeAs` дають явне
  checked numeric conversion або exact structural access. Packed u32 ID
  маршрутизується прямими індексами group/entry.
- Декларації мають лише name + binding. Semantic metadata, старі Scalar,
  JSON/v2.1 adapters та compatibility implementation видалені.
- Compile-time local-storage budget лишився 32 B; великі encoded об'єкти
  використовують caller-owned Workspace. Source, ABI та wire compatibility
  зі старою бібліотекою не надається.
- Generic resource core лишається незалежним. V3 providers розташовані у
  `lib/resource/telemetry/v3`; optional Bind/Exchange і clients — окремі
  consumers. Frozen descriptor/values bytes та 11 technical ceilings
  не змінено заради міграції.

Склад перенесення й збережені контракти описані в
[migration guide](StructuredTelemetryV3MigrationGuide.md). Вилучені
legacy-only тести мають [154 Git blob identities](evidence/StructuredLegacyTestRetirement.json).
Історичні review та [апаратні receipts](evidence/pre-unification/README.md)
збережені окремо від current evidence.

## Програмні перевірки

Компактний [software record](evidence/StructuredFinalSoftwareQualification.json)
містить SHA-256 15 фактичних локальних records. Сирі build outputs залишені
у `build/stage20`; записаний digest не обіцяє довічної доступності artifacts.

| Перевірка | Фактичний результат |
| --- | --- |
| Shared IDs/owners/slots/numeric regression | MinGW13, GCC13 null-checks, Clang18 ASan/UBSan; CubeIDE ARM14 normal/null. Кожен host виконав понад 1,57 млн умов; negative diagnostics і 48 standalone headers перевірені |
| Structured suites | Reflection, facade, types, codec, registry, service, model, endpoints, descriptor, resources, exchange, traversal, qualification, freeze; Clang suites з sanitizer там, де runner підтримує його |
| ARM inspection | CubeIDE14 O2/Os/Og, actual linked sections/symbols, disassembly, ABI/GC/LTO controls та individual `.su` frames; без удаваного виконання на ARM |
| Mixed/Scale offline matrix | Сім compiler/mode roles, 42 configurations, 138 captured LF inputs; 52 commands і 43638 умов на кожному з трьох hosts, 67 commands та нуль виконаних умов на кожному з чотирьох ARM roles |
| Generic resource | MinGW та Clang sanitizer: 9232 умови, 34 конкретні compile-time refusals; independent resource-only build |
| Qt | Playground, client, Exchange, multi-TU qualification та core/v3 resource selections; Qt6.10.1 MinGW13 |
| JS/browser interoperability | 3057 decoder/client умов, нуль failures; 20 browser умов та C++ interoperability |
| Gates як тести | Exact runtime counts, omitted/mutated output refusals, symbol controls, source hashes та receipt mutation controls |

[69/69 ARM code/layout artifacts](evidence/StructuredMigrationCodegen.json)
побайтово збігаються з pre-unification baseline. Native release direct/local/
global codegen пройшов свої gates. Порівняння інструкцій не підміняє
вимірювання циклів, а individual frame не є full-chain stack bound.

Фінальний [offline receipt](../tests/structured/mcu/local-receipt.json)
зафіксував HEAD `01fd180` і `source_dirty=false` лише для свого captured
input set. Раніші локальні records чесно зберігають їхні тодішні HEAD та
dirty state; їх не перепозначено як виконання нового published SHA.

## Фактичні H7S прогони

NUCLEO-H7S3L8, ST-LINK `002A001F3033510135393935`, COM6; Cortex-M7
600 MHz, caches enabled, CubeIDE ARM GCC14.3.1. Чотири suites послідовно
виконали десять O2/Os образів:

| Suite | Образи | Current evidence |
| --- | ---: | --- |
| Mixed + Scale | 4 | [Results](../tests/structured/mcu/h7s/RESULTS.md), [receipt](../tests/structured/mcu/h7s/receipt.json) |
| Descriptor | 2 | [Receipt та контракт](../tests/structured/descriptor/h7s/README.md) |
| Values/resource consumers | 2 | [Receipt та контракт](../tests/structured/resources/h7s/README.md) |
| Bind/Exchange | 2 | [Receipt та контракт](../tests/structured/exchange/h7s/README.md) |

Mixed/Scale: **29098 умов, нуль failures**, 784 DWT-вікна та 696 stack
observations. Descriptor, Values і Bind/Exchange пройшли власні повні
correctness/timing plans. Receipts зберігають глобальний `source_dirty=true`:
на момент запуску був unrelated untracked review tree. Captured source
inputs відповідають Git `01fd180`; це не твердження про clean working tree.

Незалежна [file-only перевірка](evidence/StructuredFinalHardwareVerification.json)
перехешувала всі десять ELF/binary images, captured sources, compiler,
objects, `.su`, scaffold/linker inputs та чотири backup/readback пари.
Усі 178 унікальних captured inputs збігаються з Git `01fd180`; 119
receipt mutation controls відхилено. Цей повторний аудит пристрою не торкався.

Перед доступом усі loadable ELF sections перевірено в межах backup range.
Після кожної suite всі 65536 bytes internal Flash відновлено та прочитано
повторно. SHA-256 backup і readback збігається в усіх чотирьох receipts:

`a5903024dba85fab5121150ca8ad13482f97384aa450aab67413881991fb9456`

Вихідну прошивку перезапущено й залишено працювати. Option bytes та
external memory не змінювалися. Локальні ELF, objects, logs, `.su`,
captured inputs, backups і readbacks перевіряються окремо від JSON receipt.
Offline receipt verification не означає повторного запуску пристрою.

## Межі висновків

Encoded 4 KiB шляхи лишилися нижче 1024 B observed full-probe stack ceiling.
Owning native Big return використовує близько 8 KiB application/probe
stack; цей виклик не є zero-stack. Watermark міряє фактично записані bytes
разом із nested calls, двома fill patterns, трьома repeats і 512 B control.
Він не включає interrupts, caller MSP або UART/formatting і не доводить
універсального worst case чи глибини untouched reserved slots.

Свіжі cycles наведені в Results, без relabelling старих Scalar benchmarks
і без обіцянки універсальної переваги швидкості. Збережено zero-cost typed
release paths у перевірених probes, frozen wire та безпечний bounded
encoded routing. Object/name/slot lifetime, external synchronization і
LIFO Workspace leases лишаються явними контрактами користувача.

Фінальний CI запускає C++20 GCC/Clang release, GCC null-check mode, Clang
sanitizers, Cortex-M7 та Qt — **шість jobs**. Поточний результат публікації
перевіряється за exact pushed SHA у [GitHub Actions](https://github.com/shpegun60/telemetry/actions/workflows/ci.yml).
Попередній CI `389c995` 9/9 є baseline evidence і не замінює цей gate.

Перший publication CI `df7d959` пройшов усі п'ять host/Qt jobs. ARM job
виявив у тестовому порівнянні unreachable alignment `nop` після terminal
branch. [Follow-up](evidence/StructuredArmSlotGateFollowup.json) виправляє
лише parser: reachable instructions і literal/relocation targets лишаються
перевіреними. Обидва повні ARM13 normal/null runs пройшли по 329 команд;
tracked control запускає п'ять mutations і дев'ять helper cases також у CI.
Код бібліотеки й усі captured hardware inputs цим виправленням не змінено.
