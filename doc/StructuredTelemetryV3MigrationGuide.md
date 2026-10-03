# Telemetry v3: міграція 16–20

Автори: Ruslan Kovtun (shpegun60), codexAi. Дата: 2026-10-03.

**Статус: Stage 16–19 реалізовано; Stage 20 local/software/hardware qualification
завершено. Publication exact-SHA CI ще очікується.**
Інвентаризація починалась на `d642b42`; baseline повторено на `389c995`.
Exact-SHA [CI 37145176280](https://github.com/shpegun60/telemetry/actions/runs/37145176280)
пройшов 9/9 jobs для історичного baseline; Stage 14 MCU receipt прийнято. Рішення про gate
зафіксовано в [implementation plan](StructuredTelemetryV3ImplementationPlan.md)
і [freeze qualification](StructuredTelemetryV3FreezeQualification.md).
Поточний результат кінцевого дерева, чотири нові H7S receipts і межі
publication gate наведено у [final qualification](StructuredTelemetryV3FinalQualification.md).
Captured code HEAD — `01fd180ff3678d7c47066f8a728dc1e02f0f3973`.

Кінцеве дерево має один C++20 `telemetry`/`ts`, exact native values і wire
v3.0. Міграція не додає semantics, не розширює supported types і не
залишає scalar/v2.1 compatibility implementation. Якщо реальному consumer
бракує frozen behavior, Stage 15 відкривається знову; потребу закривають
до перенесення, з новою матрицею й повторним freeze.

## 1. Stage 16: підтвердження і source migration cases

Baseline: mixed endpoints MinGW 13.1 і CubeIDE ARM 14.3.1; Service/Model ARM;
Clang 18 ASan/UBSan traversal та multi-TU qualification; сім freeze/offline
compiler roles; JS і Chromium. Старі source trees під час цих прогонів
не змінювались. Попереднє порівняння міграційної копії на CubeIDE ARM дало
69/69 побайтово однакових native code/layout artifacts; layouts та великі
individual frames O2/Os/Og збігаються. Матрицю повторено в кінцевому дереві;
свіжі MCU receipts отримано. Локальні runs не замінюють publication exact-SHA CI.

Повторено наявні mixed Field/Command/Service fixtures, зокрема
[MixedFixture.hpp](../tests/structured/endpoints/MixedFixture.hpp) та
[traversal Fixture.hpp](../tests/structured/traversal/Fixture.hpp).
Умови залишаються ті самі: bool, signed/unsigned integers, float/double,
scoped enum, array, struct, read-only/RW, slots, local/global/runtime
access, exact structural identity і явний checked numeric As access.
Нових callbacks або metadata модель для проходження gate не потрібна.

| Старий контракт | Міграція на frozen core | Збережений контракт |
| --- | --- | --- |
| Getter/Setter через `Scalar` | Native `T`; setter приймає `T` або `const T&`, повертає `WriteResult` | Exact return/input type; getter/setter не викликається при структурному mismatch |
| Неявне перетворення numeric arguments | Exact typed API або явні `readAs`/`writeAs`; application casts лише там, де рішення свідоме | U64/S64 extrema без double intermediate; narrowing, finite/NaN, bool, output unchanged on refusal |
| Unscoped enum | Scoped enum з явною underlying width | Width/code representation; representable unknown codes у codec не стають відомими application states |
| `enumSpec`/enum default | Explicit `EnumReflection`, тільки якщо потрібні точні codes/names | Alias selection, explicit dictionary precedence; default лишається application state/UI policy |
| `limits`, `arg`, units, defaults, steps, flags | Application validation і UI policy | Зберегти реально потрібну поведінку; не додавати ці дані в TypeRegistry/descriptor |
| Command із кількома arguments | Один aggregate Request або void | Complete request validation до mutation; збережені status codes і значення `Accepted` |
| Service | Name+binding, void або aggregate Request/Response, `ServiceResult<Response>` | Response status/payload invariant, exact native type, large-object storage |
| Local position / packed global ID | Local template position і `makeId(group, entry)` | Bounds до narrowing, negative/high-word rejection, separate endpoint spaces |
| Late-bound owner/callable | Збережені slot families | Borrowed lifetime, weak/null target, exact signature, rebind/reset synchronization application-owned |
| Віддалений read/write/command/service | Existing encoded Model operations; framing і agreement у consumer | Fingerprint/reconnect policy на application boundary; malformed payload не викликає callback |

`TypeKind::Scalar` і `ScalarCode` залишаються категорією та кодами leaf
types. Вони не означають збереження старого `telemetry::Scalar` container.
Не застосовувати широкий текстовий пошук `Scalar` як вимогу видалити
leaf codec або його перевірки.

Gate 16: змістовно незмінні endpoint, service, model, traversal і freeze
suites проходять host/sanitizer/ARM normal/null configurations; local
native codegen лишається порівняним із direct calls. Збережено як baseline
wire goldens, linked sections, frames і normalized instruction streams.
На Stage 16 старе дерево й consumers ще не переміщувались; переносили їх
після прийняття baseline gate.

## 2. Stage 17: виділені спільні низькорівневі контракти

Спільні IDs, statuses, conversion, owner/target і slot helpers виділено
до кінцевих нейтральних headers. Core не включає старі Scalar/Setter/Command
headers. Карта зберігає історичні джерела та виконану механічну розкладку;
окрема shared library або новий ABI не додавались.

| Історичне джерело `389c995` | Що збережено в core | Кінцеве місце / контракт |
| --- | --- | --- |
| [TelemetrySetter.h](https://github.com/shpegun60/telemetry/blob/389c995083b4dc0a39cd8775b211ef765494240f/lib/telemetry/field/TelemetrySetter.h) | `WriteResult`, `uint8_t`, codes Applied..Unavailable | Enum виділено до `telemetry/result/EndpointStatus.hpp`; старий `Setter` видалено |
| [TelemetryCommand.h](https://github.com/shpegun60/telemetry/blob/389c995083b4dc0a39cd8775b211ef765494240f/lib/telemetry/command/TelemetryCommand.h) | `CommandResult`, `uint8_t`, codes Executed..Failed | Той самий `EndpointStatus.hpp`; `CommandParam`, Scalar arguments і metadata ops видалено |
| [TelemetryId.h](https://github.com/shpegun60/telemetry/blob/389c995083b4dc0a39cd8775b211ef765494240f/lib/telemetry/core/TelemetryId.h) | Packed IDs, `makeId`, `tryMakeId`, decomposition, bounds/template helpers | `telemetry/core/Id.hpp`, зі збереженими правилами packing та diagnostics |
| [TelemetryCompiler.h](https://github.com/shpegun60/telemetry/blob/389c995083b4dc0a39cd8775b211ef765494240f/lib/telemetry/core/TelemetryCompiler.h) | Inline/compiler attributes у Workspace, codec, encoded paths і slots | `telemetry/core/Compiler.hpp`; compiler behavior збережено |
| [TelemetryNumberConversion.h](https://github.com/shpegun60/telemetry/blob/389c995083b4dc0a39cd8775b211ef765494240f/lib/telemetry/detail/TelemetryNumberConversion.h) | `convertNumberTo` і finite/bounds semantics для `readAs`/`writeAs` | `telemetry/detail/NumberConversion.hpp`; Scalar dependency прибрано, прямі standard includes додано |
| [TelemetryOwner.h](https://github.com/shpegun60/telemetry/blob/389c995083b4dc0a39cd8775b211ef765494240f/lib/telemetry/detail/TelemetryOwner.h) | Actual object borrowing, member owner і temporary rejection | `telemetry/detail/Owner.hpp`, той самий lifetime contract |
| [TelemetryTarget.h](https://github.com/shpegun60/telemetry/blob/389c995083b4dc0a39cd8775b211ef765494240f/lib/telemetry/detail/TelemetryTarget.h) | `nonNullTarget`, `pointerPresent`, uncertainty і weak target behavior | `telemetry/detail/Target.hpp`; normal/null-check modes збережено |
| [TelemetrySlotCallable.h](https://github.com/shpegun60/telemetry/blob/389c995083b4dc0a39cd8775b211ef765494240f/lib/telemetry/detail/TelemetrySlotCallable.h) | Exact signature matching і delegate support | `telemetry/detail/SlotCallable.hpp`; `lib/delegate` dependency збережено |
| [slot headers](../lib/telemetry/slot/README.md) | FunctionSlot, ContextFunctionSlot, DelegateRefSlot, DelegateSlot, OwnerSlot, recognition traits | Кінцеві `telemetry/slot/*` headers; без нового ownership або runtime modes |
| [StructuredAbi.hpp](https://github.com/shpegun60/telemetry/blob/389c995083b4dc0a39cd8775b211ef765494240f/lib/telemetry_structured/abi/StructuredAbi.hpp) і `.cpp` | Exact new view/ops tag, revision 5, storage budget/layout facts | Кінцевий `telemetry/abi/` header/anchor; ті самі facts, wire не змінюється |

Виконані extraction details:

- `FieldAccess.hpp` включає нейтральний `NumberConversion.hpp` без Scalar
  dependency. Generic checked conversion, FP assumptions, truncation,
  NaN/infinity та alias/output rules збережено; Scalar-specific
  `storeConverted` і `convertNumber` прибрано.
- `Field.hpp`, `Command.hpp`, `EndpointResults.hpp` включають нейтральні
  statuses. `FieldType`/`Scalar` не надходять транзитивно заради enum status.
- `Binding.hpp`, `Service.hpp`, `Name.hpp`, CallableTraits adapter та slots
  перепідключено до нейтральних Owner/Target/Slot headers.
- Новий ABI header не включає старий `TelemetryAbi.h`. Старий ABI anchor
  був потрібний лише старим consumers і видалений разом із ними.
- `TelemetryCacheline.h` та старий `TelemetryCallable.h` не входять у
  залежності нового core або збережених slots. Саме їх наявність у старому
  umbrella не є причиною залишати їх у фінальному core.
- Vendored delegate/PFR/magic_enum licenses і provenance збережено.
  Reflection/type/codec/Model/Descriptor не повинні обходити facade;
  PFR і magic_enum calls залишаються в reflection backend.
Назва `StructuredAbi.hpp` і macro `TELEMETRY_STRUCTURED_LOCAL_BYTES`
залишились частиною frozen ABI/configuration contract.

## 3. Stage 17: шляхи, namespace і build integration

| Історичний шлях | Кінцевий шлях |
| --- | --- |
| `lib/telemetry_structured/Structured.hpp` | `lib/telemetry/Telemetry.hpp` |
| `lib/telemetry_structured/{reflection,type,codec,field,command,service,model,result,detail,abi}/` | Відповідні каталоги єдиного `lib/telemetry/` |
| `lib/telemetry_structured/structured.pri` | Єдиний `lib/telemetry/telemetry.pri` |
| `lib/resource/structured/` | `lib/resource/telemetry/v3/` |
| `web/telemetryStructured.js` | `web/telemetry.js` |
| `examples/structured_protocol/` | Збережено як explicit optional protocol consumer |
| `examples/structured_client/` | Public includes, namespace, `.pri` та JS import оновлено |

Namespace нового core: `telemetry::structured` → `telemetry`; `ts` є
лише consumer alias. Qualified reflection specializations, aliases,
compiled symbols і generated include probes оновлено.
Resource providers мають namespace `resource::telemetry::v3`;
wire і generic resource API при перенесенні не змінено.

Relative includes після поглиблення resource tree виправлено: headers у
`v3/` знаходять `resource/Types.hpp` через `../../Types.hpp`, у `v3/detail/`
— через `../../../Types.hpp`. Standalone header checks пройшли.

Фінальний `.pri` має include guard, PFR/magic_enum/delegate include paths,
C++20 і лише new ABI/Model sources. `delegate.pri` додає C++17, тому
кінцева language selection виконується після dependency inclusion.
Generic resource core/protocol не включає telemetry `.pri` назад і не
набуває PFR dependency.

Після Stage 19 `resource_telemetry` означає тільки v3. V2 implementation
та тимчасовий `resource_telemetry_v3` flag видалено. Фінальні consumers
не потребують `structured.pri`, `resource_structured` чи `telemetry_no_json`.
`examples/structured_protocol/protocol.pri` завжди обирається явно;
library manifests не підтягують Bind/Exchange.

**Старі й нові definitions у namespace `telemetry` не можуть бути
одночасно в одному TU або linked executable.** Це різні definitions
FieldTable/CommandTable/catalogs, а не два взаємозамінні implementations.
Після namespace migration old/new comparisons потребують окремих
old-only/new-only fixtures і binaries. Не лінкувати старі objects із
новими та не залишати compatibility aliases як обхід. Тимчасовий old
target існував до останнього переведеного consumer і видалений на Stage 19.

## 4. Stage 18: tracked consumers

### Demo та Qt playground

| Файл | Виконана зміна | Збережена поведінка |
| --- | --- | --- |
| [DemoCatalog.h](../app/demo/DemoCatalog.h) | Native field factories name+binding; mixed tables/catalogs/Model; Request aggregate для Configure | Meter/Sensor values, stable IDs/row ordering, exact integer extrema, threshold setter refusal |
| [DemoCatalog.cpp](../app/demo/DemoCatalog.cpp) | Declarations узгоджено після request зміни | Simulated advance behavior |
| [mainwindow.cpp](../app/mainwindow.cpp) і [header](../app/mainwindow.h) | Typed ordered traversal/native reads; Scalar formatter і старі JSON serializers прибрано | Exact U64/S64 text, units, UI ranges/defaults, enum labels, commands і periodic refresh |
| [main.cpp](../app/main.cpp) | Request calls замість positional arguments; explicit native values/As access | Smoke verifies values and successful/refused application commands |
| [DeviceResources.cpp](../app/resources/DeviceResources.cpp) і [header](../app/resources/DeviceResources.hpp) | Descriptor+values providers замість schema+commands+values; IDs/count оновлено | Generic resource facade і packet protocol contract |
| [telemetry.pro](../telemetry.pro) | Єдиний final `.pri`, v3 provider selection і JS path | Qt build/smoke без legacy sources |

У Meter старий `configure(float, Mode)` замінено на
`configure(const ConfigureRequest&)`. Request містить native float
і scoped Mode; callable перевіряє finite threshold, діапазон 1..1000 і
допустимі Mode codes **до** зміни threshold/mode. Smoke вже вимагає
`InvalidValue` для 1001. Це збереження application behavior, а не
додавання semantic limits до TypeRegistry.

Units `V`, `A`, `kW`, `degC`, початкові UI values, spinbox range і
Off/Auto/Manual labels залишаються в application/UI. `Persistent` flag
у demo був metadata; він сам не реалізував persistence. Не переносити
його як вигадану гарантію storage. Якщо metadata прибирається з UI,
це має бути окреме явне consumer рішення, не тихий наслідок видалення core.

Scoped enum `Mode : uint16_t` збережено. Native enum codec допускає unknown
representable codes; application setter/Command встановлює свою policy.
Mode field/Configure відхиляє недопустимі codes без mutation.
Defaults залишаються initial Meter/UI state.

Resource paths після міграції: `/telemetry/descriptor.bin` і
`/telemetry/values.bin`. Old schema/commands file indices зникають;
[DeviceCheck.cpp](../tests/resources/DeviceCheck.cpp) перевіряє новий
count, paths, reads і generic protocol разом із app facade.

### Browser, Qt example, optional protocol і MCU consumers

- [structured_client/Device.hpp](../examples/structured_client/Device.hpp),
  [QtSmoke.cpp](../examples/structured_client/QtSmoke.cpp), [qt.pro](../examples/structured_client/qt.pro):
  public includes/namespace, shared statuses/slots, final provider `.pri`.
- [app.js](../examples/structured_client/app.js),
  [client/Check.mjs](../tests/structured/client/Check.mjs), browser runner:
  import `web/telemetry.js`; зберегти descriptor/values decoding, BigInt,
  shape-driven request/response editors, ceilings і cache/fingerprint policy.
- [structured_protocol](../examples/structured_protocol/README.md):
  core/resource includes і aliases оновлено; packet magic/status codes, validation,
  caller Workspace і routing не змінюються. Transport agreement є прикладом,
  а не новою умовою native або encoded core calls.
- [MCU Fixture.hpp](../tests/structured/mcu/Fixture.hpp), `Mixed.cpp`,
  `Scale.cpp`, Descriptor/Values/Exchange H7S fixtures: tracked MCU test
  consumers, їхні build source lists та evidence manifests переведено.
  Старі endpoint H7S branches збережено як історичні докази.
  Окремого firmware application
  consumer всередині цього telemetry repository не виявлено.

## 5. Збережені перевірки та закриті migration risks

| Місце | Початковий ризик | Виконана дія / збережений gate |
| --- | --- | --- |
| [mcu/run.py](../tests/structured/mcu/run.py) | Path-based legacy exemption міг пропустити new-core frames | Exemption прибрано; final core frames перевіряються |
| [facade/run.py](../tests/structured/facade/run.py) | Старий directory scan міг стати порожнім | Scan final directory; нуль headers відхиляється |
| [structured resources/run.py](../tests/structured/resources/run.py) | Standalone header scan старого resource tree | Scan `lib/resource/telemetry/v3/` з правильними includes |
| [resources/run.py](../tests/resources/run.py) | Generic scan міг захопити v3 без backend includes | Generic scan обмежено власними headers; v3 має окремий runner |
| [qualification/ScalarComparison.cpp](https://github.com/shpegun60/telemetry/blob/389c995083b4dc0a39cd8775b211ef765494240f/tests/structured/qualification/ScalarComparison.cpp) | New+old same-name tables в одному TU | Old controls retired після baseline; direct/new codegen збережено |
| [mcu/Scale.cpp](../tests/structured/mcu/Scale.cpp) | oldLocal/oldScalar та operation index arithmetic | Old probes retired після baseline; indices/checks оновлено, direct/native/visitor/As/encoded збережено |
| Історичні `endpoints/h7s/*` із `ENDPOINT_LEGACY` | Legacy branch бачив new fixture | Old branch/target retired; докази збережено окремо |
| [resources.pro](../tests/structured/resources/resources.pro), [run_qmake.py](../tests/structured/resources/run_qmake.py) | `MODE=both` після namespace move | Фінально тільки core-only і v3 selections |
| Freeze generated Manifest, source lists, header probes, JS imports | Hardcoded structured paths і namespaces | Runnable inputs оновлено; wire constants/goldens незмінні |
| [CI workflow](../.github/workflows/ci.yml) | Старі targets/scans або втрачені test steps | Final C++20 host/ARM/Qt matrix та artifacts збережено; publication CI ще pending |

Retained summaries/receipts прив'язані до своїх source/input manifests.
Перенесення не робить старий receipt доказом current-source build.
Нові receipts генеруються з нових runs; історичні залишаються історичними.
Ні однаковий ELF, ні individual `.su` frame не доводить MCU cycles або
максимальний live stack усього call chain.

## 6. Stage 19–20: видалення та фінальний gate

Зберегти всі new typed/wire suites: reflection, facade, types, codec,
registry, service, model, endpoints, traversal, descriptor, resources,
exchange, client, qualification, mcu і freeze. Зберегти exact goldens,
negative diagnostics, ABI mismatch/GC/LTO reachability controls,
no-heap/symbol controls, null-check modes, large-object checks, codegen,
static frames та потрібні MCU scenarios.

Legacy-only Scalar/FieldType/limits/arg/JSON/v2.1 tests, old ABI guards,
old codegen probes і build targets видалено після останнього
переведеного consumer та збереження baseline. Shared IDs/slot/callable/
numeric edge coverage перенесено на кінцеві headers.
Зокрема SlotCallable/SlotEdges/SlotOverload, owner/null/
weak targets, ID boundary/explicit-template-argument cases і FP mode
refusals залишаються релевантними збереженим contracts.

Generic resource `CoreCheck.cpp`, `Negative.cpp` і `stack_check.py` лишаються;
старий невикористовуваний v2 `TestSupport.hpp` видалено після збереження baseline;
DeviceCheck перевіряє v3 app files.
Legacy Binary/TelemetryFiles/Metadata/decoder/goldens перевіряли v2
layout і видалені після архівування. Generic cursor/protocol/error cases
збережено на generic provider або v3.

Локальний final gate перевірив такі умови; пункт 5 окремо потребує
publication exact-SHA CI:

1. У runnable tracked production/demo/test includes і manifests немає
   old Scalar/FieldType/JSON/v2 adapters, `telemetry_structured` target,
   migration target або compatibility aliases. Archive/review/release
   history не є production dependency.
2. Один public umbrella/namespace, один Model/Registry та набір typed
   tables; resource-only program збирається без telemetry/PFR; selected
   v3 consumer не лінкує JSON/v2 або optional protocol неявно.
3. Mixed-table public example, Qt/demo smoke, JS/browser/Qt interoperability,
   generic resource/protocol, sanitizer і ARM normal/null checks проходять.
4. Canonical descriptor/values bytes збігаються з frozen goldens; section,
   stack/codegen differences звірені з baseline й оцінені за реальним diff.
5. MCU evidence і CI стосуються потрібного source/input set та final SHA;
   docs відокремлюють measured results від inferred claims.
6. README/include/qmake examples показують кінцевий C++20 API й прямо
   вказують відсутність source/ABI/wire compatibility із v2.1.

Карта вище також зберігає початкову інвентаризацію. Stage 16–19 виконано:
нейтральні shared contracts виділено, нове ядро стало єдиним `telemetry`,
consumers/tests/CI переведено, Scalar/v2 executable implementation видалено.
Stage 20 local/software/hardware qualification завершено. Чотири current
H7S receipts зафіксували code HEAD `01fd180`, повне відновлення Flash і
незмінні captured LF inputs: MCU — 140, Descriptor — 133, Values/resources
— 133, Bind/Exchange — 140. Їхній глобальний `source_dirty=true` зберігає
наявність unrelated untracked review tree; captured code відповідає Git
цього commit, це не clean-tree claim. Деталі — у
[final qualification](StructuredTelemetryV3FinalQualification.md).
Publication exact-SHA CI ще очікується; історичний `389c995` 9/9 його не замінює.

Фінальний ARM byte comparison: [69/69 artifacts](evidence/StructuredMigrationCodegen.json).
Retirement sources та Git baseline: [перелік](evidence/StructuredLegacyTestRetirement.json).
Поточні [тести](../tests/README.md), [API](../lib/telemetry/README.md) й
[історичні докази](evidence/pre-unification/README.md) мають окремі ролі.
