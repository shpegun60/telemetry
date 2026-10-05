# Документація Telemetry

[Репозиторій](../README.md) · [Посібник](user/README.md) ·
[API шпаргалка](user/API-CHEATSHEET.md) · [Архітектура](Architecture.md) ·
[Runtime native та обхід](user/Ergonomics.md) ·
[Wire v3.0](WireV3.md) · [Тести](../tests/README.md)

Цей індекс описує чинний C++20 `telemetry` і wire v3.0. Для першої
інтеграції почніть із [GettingStarted](user/GettingStarted.md); повні
програми мають окремі runnable source files. Таблиці нижче ведуть до
документа, який володіє конкретною темою.

## Навігація

- [Підключення за кроками](#підключення-за-кроками)
- [Довідник за завданням](#довідник-за-завданням)
- [Повні приклади](#повні-приклади)
- [Модулі та контракти](#модулі-та-контракти)
- [Перевірки та вимірювання](#перевірки-та-вимірювання)
- [Підтримка документації й архів](#підтримка-документації-й-архів)

## Підключення за кроками

| Крок | Документ | Результат |
| --- | --- | --- |
| 1 | [Перша інтеграція](user/GettingStarted.md) | Source/include settings, свій Device, declarations і native виклик |
| 2 | [Application integration](user/ApplicationIntegration.md) | Валідація, facade, lifetimes, slots і узгоджений доступ до state |
| 3 | [Transport walkthrough](user/TransportWalkthrough.md) | Receive chunks → complete packet → operation → reply |
| 4 | [Device integration](../examples/device_integration/README.md) | Multi-file проєкт із CMake та host перевірками |
| 5 | [Діагностика](user/Troubleshooting.md) | Рішення compiler/linker, type, ID, status, Workspace і framing питань |

Для local typed доступу достатньо кроків 1–2. Для віддаленого доступу
додайте transport framing і encoded adapter; file interface — окремий вибір.
[User guide](user/README.md) порівнює ці шляхи на одній схемі.

## Довідник за завданням

| Потрібно | Документ |
| --- | --- |
| Згадати declarations та назви викликів | [API шпаргалка](user/API-CHEATSHEET.md) |
| Getter/setter і конкретні Field operations | [Fields](user/Fields.md) |
| Request та результат application дії | [Commands](user/Commands.md) |
| Request/Response, Service statuses і reply payload | [Services](user/Services.md) |
| Local tables, named groups, packed IDs та runtime indexes | [Tables and catalogs](user/TablesAndCatalogs.md) |
| Structural TypeRegistry, ModelView та всі runtime indexes | [Model](user/Model.md) |
| Великі catalogs, unique DTOs, visitor code size та module dependencies | [Масштабування](Scalability.md), [reproducible checks](../tests/scalability/README.md) |
| Canonical codec, Workspace/Lease, scratch та overlap | [Codec and Workspace](user/CodecAndWorkspace.md) |
| Всі callback/binding forms, exact/As доступ, enums, iteration і slots | [Native API](user/NativeApi.md) |
| Custom files, providers і cursors | [Resource files](user/Resources.md) |
| Model descriptor, Values, fingerprint і whole-token READ | [Descriptor and Values](user/DescriptorAndValues.md) |
| LIST/STAT/READ/WRITE, framing та transport integration | [Transport and resources](user/TransportAndResources.md) |
| З'єднати telemetry resources і реальний COBS endpoint | [COBS integration](user/COBSIntegration.md) |
| Повернути `const T&` без owning copy і забезпечити lifetime | [Borrowed native values](BorrowedNativeValues.md) |
| Зрозуміти межі Model, codec, providers і transport | [Архітектура](Architecture.md) |
| Реалізувати незалежний descriptor/Values decoder | [Wire v3.0](WireV3.md) |
| Перенести consumer з попереднього API | [Migration guide](StructuredTelemetryV3MigrationGuide.md) |
| Зібрати або перевірити конкретний приклад | [Example checks](../tests/docs/README.md), [test index](../tests/README.md) |

## Повні приклади

| Приклад | Що демонструє |
| --- | --- |
| [User-guide programs](../examples/user_guide/README.md) | QuickStart, Native, custom Resources і encoded доступ без файлів |
| [Device integration](../examples/device_integration/README.md) | Business class, public facade, tables у `.cpp` та bounded stream receiver |
| [COBS integration](../examples/cobs_integration/README.md) | Реальний sibling COBS endpoint, client/server resource exchange, TX backpressure і recovery на host |
| [Custom resources](../examples/resources/README.md) | Додати providers із власними paths |
| [Structured client](../examples/structured_client/README.md) | JS parser/codec, browser та Qt consumer |
| [Optional Bind/Exchange](../examples/structured_protocol/README.md) | Descriptor agreement і control envelope, якими володіє application |

Fenced fragments пояснюють окремий крок; повну програму для збирання
беріть із linked translation unit чи example project. Host receiver
і simulated Device не є виконанням HAL або transport на MCU.

## Модулі та контракти

| Модуль/контракт | Технічний довідник |
| --- | --- |
| Native endpoints, TypeRegistry і Model | [Telemetry](../lib/telemetry/README.md) |
| Late binding | [Slots](../lib/telemetry/slot/README.md) |
| Незалежний filesystem/providers | [Resource](../lib/resource/README.md) |
| Packet processor | [Resource protocol](../lib/resource/protocol/README.md) |
| Descriptor/Values provider lifetime і storage | [V3 providers](../lib/resource/telemetry/v3/README.md) |
| Exact wire constants, default ceilings і goldens | [Wire v3.0](WireV3.md), [freeze manifest](../tests/structured/freeze/contract.json) |
| JS native payload codec та parser | [Client API](../tests/structured/client/README.md) |

## Перевірки та вимірювання

Почніть із [test index](../tests/README.md): host execution, sanitizers,
ARM compile/link/disassembly та H7S execution — окремі види доказів.
Кожний receipt/report має власні captured inputs і source identity.
Успіх старого checkpoint не доводить статус нового commit.

| Доказ | Що в ньому шукати |
| --- | --- |
| [Qualification suite index](../tests/structured/README.md) | Підтримувані suites та команда для потрібного контракту |
| [Borrowed software/ARM checks](../tests/structured/borrowed/README.md) | Result/lifetime refusals, storage budgets, ABI та codegen gates |
| [Borrowed H7S harness](../tests/structured/borrowed/h7s/README.md), [results](../tests/structured/borrowed/h7s/RESULTS.md) | Фактичні owning/borrowed comparison profiles, cycles, observed stack і їхні межі |
| [MCU harness](../tests/structured/mcu/README.md), [H7S contract](../tests/structured/mcu/h7s/README.md), [results](../tests/structured/mcu/h7s/RESULTS.md) | Mixed/Scale scenarios, captured source, image bounds і повне Flash restore/readback |
| [Descriptor](../tests/structured/descriptor/h7s/README.md), [Values](../tests/structured/resources/h7s/README.md), [Exchange](../tests/structured/exchange/h7s/README.md) | Окремі H7S receipts для відповідних consumers |
| [Resource image equivalence](../tests/resources/evidence/README.md) | File-only звірка з retained images; це не новий hardware run |
| [Stage 15 freeze checkpoint](StructuredTelemetryV3FreezeQualification.md) | Історичний baseline contract і його exact-SHA verification |
| [Stage 20 qualification checkpoint](StructuredTelemetryV3FinalQualification.md) | Закритий migration baseline, тодішні software/H7S inputs і publication gate |
| [Pre-borrowed receipt archive](evidence/pre-borrowed/README.md) | Exact попередні receipts/validators, відокремлені від оновлених input sets |
| [Pre-unification evidence](evidence/pre-unification/README.md) | Збережені попередні measurements із незмінними source identities |

## Підтримка документації й архів

README репозиторію є landing page; цей індекс маршрутизує читача;
user guide пояснює використання; module guides і wire reference володіють
контрактами. При зміні API оновіть relevant guide та runnable example
разом і виконайте відповідний runner із [test index](../tests/README.md).

Скасований scalar-план, завершений implementation plan, старі review,
транскрипти та exploratory probes зберігаються поза активним checkout.
Точний inventory, архів і hash verification описані у
[Repository maintenance](RepositoryMaintenance.md). Збережені receipts
і вимірювання залишаються у своїх evidence directories з початковими
source identities. Вони доступні для перевірки походження результатів;
чинний API визначають довідники вище.
