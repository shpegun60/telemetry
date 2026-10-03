# Structured telemetry v3: підготовка міграції 16–20

Автори: Ruslan Kovtun (shpegun60), codexAi. Дата: 2026-10-03.

**Статус: preparation only; Stage 16 не розпочато.** Інвентаризація виходить
із tracked source `d642b42b656df367784a8cc57a1fcaf10a301453`. Цей документ
не змінює core, не закриває Stage 14 і не оголошує Stage 15 завершеним.
Етапи 16–20 заблоковані до повного Stage 15 gate: прийнятого freeze,
необхідного MCU evidence та CI для відповідного SHA. Рішення про gate
належить [implementation plan](StructuredTelemetryV3ImplementationPlan.md)
і [freeze qualification](StructuredTelemetryV3FreezeQualification.md).

Мета після gate — один C++20 `telemetry`/`ts`, exact native values і wire
v3.0. Міграція не додає semantics, не розширює supported types і не
залишає scalar/v2.1 compatibility implementation. Якщо реальному consumer
бракує frozen behavior, Stage 15 відкривається знову; потребу закривають
до перенесення, з новою матрицею й повторним freeze.

## 1. Stage 16: підтвердження і source migration cases

Повторити вже наявні mixed Field/Command/Service fixtures, насамперед
[MixedFixture.hpp](../tests/structured/endpoints/MixedFixture.hpp) та
[traversal Fixture.hpp](../tests/structured/traversal/Fixture.hpp).
Умови залишаються ті самі: bool, signed/unsigned integers, float/double,
scoped enum, array, struct, read-only/RW, slots, local/global/runtime
access, exact structural identity і явний checked numeric As access.
Нових callbacks або metadata модель для проходження gate не потрібна.

| Старий контракт | Міграція на frozen core | Що перевірити до перенесення |
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
native codegen лишається порівняним із direct calls. Зберегти як baseline
wire goldens, linked sections, frames і normalized instruction streams.
У цьому етапі старе дерево й consumers ще не переміщуються.

## 2. Stage 17: виділити спільні низькорівневі контракти

Новий core зараз включає частину старих headers. Перед видаленням вони
мають отримати кінцеве нейтральне місце. Наведені цільові імена задають
мінімальну механічну розкладку; це не окрема shared library чи новий ABI.

| Поточне джерело | Що використовується новим core | Кінцеве місце / дія |
| --- | --- | --- |
| [TelemetrySetter.h](../lib/telemetry/field/TelemetrySetter.h) | `WriteResult`, `uint8_t`, codes Applied..Unavailable | Виділити тільки enum до `telemetry/result/EndpointStatus.hpp`; старий `Setter` не переносити |
| [TelemetryCommand.h](../lib/telemetry/command/TelemetryCommand.h) | `CommandResult`, `uint8_t`, codes Executed..Failed | Той самий `EndpointStatus.hpp`; `CommandParam`, Scalar arguments і metadata ops не переносити |
| [TelemetryId.h](../lib/telemetry/core/TelemetryId.h) | Packed IDs, `makeId`, `tryMakeId`, decomposition, bounds/template helpers | `telemetry/core/Id.hpp`, зі збереженими правилами packing та diagnostics |
| [TelemetryCompiler.h](../lib/telemetry/core/TelemetryCompiler.h) | Inline/compiler attributes у Workspace, codec, encoded paths і slots | `telemetry/core/Compiler.hpp`; зберегти compiler behavior |
| [TelemetryNumberConversion.h](../lib/telemetry/detail/TelemetryNumberConversion.h) | `convertNumberTo` і finite/bounds semantics для `readAs`/`writeAs` | `telemetry/detail/NumberConversion.hpp`; відокремити від Scalar, додати його прямі standard includes |
| [TelemetryOwner.h](../lib/telemetry/detail/TelemetryOwner.h) | Actual object borrowing, member owner і temporary rejection | `telemetry/detail/Owner.hpp`, той самий lifetime contract |
| [TelemetryTarget.h](../lib/telemetry/detail/TelemetryTarget.h) | `nonNullTarget`, `pointerPresent`, uncertainty і weak target behavior | `telemetry/detail/Target.hpp`, зберегти normal/null-check modes |
| [TelemetrySlotCallable.h](../lib/telemetry/detail/TelemetrySlotCallable.h) | Exact signature matching і delegate support | `telemetry/detail/SlotCallable.hpp`; зберегти `lib/delegate` dependency |
| [slot headers](../lib/telemetry/slot/README.md) | FunctionSlot, ContextFunctionSlot, DelegateRefSlot, DelegateSlot, OwnerSlot, recognition traits | Кінцеві `telemetry/slot/*` headers; без нового ownership або runtime modes |
| [StructuredAbi.hpp](../lib/telemetry_structured/abi/StructuredAbi.hpp) і `.cpp` | Exact new view/ops tag, revision 5, storage budget/layout facts | Кінцевий `telemetry/abi/` header/anchor; ті самі facts, wire не змінюється |

Окремі extraction details:

- `FieldAccess.hpp` зараз включає `TelemetryNumberConversion.h`, а той —
  `TelemetryScalar.h`. Залишити generic checked conversion й supported FP
  assumptions; прибрати лише Scalar-specific `storeConverted` і
  `convertNumber`. Не змінити truncation, NaN/infinity або alias/output rules.
- `Field.hpp`, `Command.hpp`, `EndpointResults.hpp` мають включати нейтральні
  statuses замість legacy Setter/Command headers. Новий Command не повинен
  транзитивно отримувати старі `FieldType`/`Scalar` заради enum status.
- `Binding.hpp`, `Service.hpp`, `Name.hpp`, CallableTraits adapter та slots
  перепідключити до нейтральних Owner/Target/Slot headers.
- Новий ABI header не включає старий `TelemetryAbi.h`. Старий ABI anchor
  потрібний лише старим consumers; його не переносити в новий compiled adapter.
- `TelemetryCacheline.h` та старий `TelemetryCallable.h` не входять у
  залежності нового core або збережених slots. Саме їх наявність у старому
  umbrella не є причиною залишати їх у фінальному core.
- Зберегти vendored delegate/PFR/magic_enum licenses і provenance.
  Reflection/type/codec/Model/Descriptor не повинні обходити facade;
  PFR і magic_enum calls залишаються в reflection backend.

## 3. Stage 17: шляхи, namespace і build integration

| Поточний шлях | Кінцевий шлях |
| --- | --- |
| `lib/telemetry_structured/Structured.hpp` | `lib/telemetry/Telemetry.hpp` |
| `lib/telemetry_structured/{reflection,type,codec,field,command,service,model,result,detail,abi}/` | Відповідні каталоги єдиного `lib/telemetry/` |
| `lib/telemetry_structured/structured.pri` | Єдиний `lib/telemetry/telemetry.pri` |
| `lib/resource/structured/` | `lib/resource/telemetry/v3/` |
| `web/telemetryStructured.js` | `web/telemetry.js` |
| `examples/structured_protocol/` | Зберегти як explicit optional protocol consumer |
| `examples/structured_client/` | Зберегти, оновити public includes, namespace, `.pri` та JS import |

Namespace нового core: `telemetry::structured` → `telemetry`; `ts` є
лише consumer alias. Оновити qualified reflection specializations, aliases,
compiled symbols і generated include probes, а не лише umbrella include.
Підкаталоги resource providers можуть мати власний узгоджений namespace;
перенесення provider files не повинне змінити wire або generic resource API.

Після поглиблення resource tree виправити relative includes: headers у
`v3/` знаходять `resource/Types.hpp` через `../../Types.hpp`, у `v3/detail/`
— через `../../../Types.hpp`. Перевірити всі headers standalone.

Фінальний `.pri` має include guard, PFR/magic_enum/delegate include paths,
C++20 і лише new ABI/Model sources. `delegate.pri` додає C++17, тому
кінцева language selection виконується після dependency inclusion.
Generic resource core/protocol не включає telemetry `.pri` назад і не
набуває PFR dependency.

Під час переходу `resource_telemetry_v3` однозначно обирає v3;
`resource_telemetry` ще обирає v2. На Stage 19 v2 та тимчасові flags
видаляються, і `resource_telemetry` означає тільки v3. Фінальні consumers
не потребують `structured.pri`, `resource_structured` чи `telemetry_no_json`.
`examples/structured_protocol/protocol.pri` завжди обирається явно;
library manifests не підтягують Bind/Exchange.

**Старі й нові definitions у namespace `telemetry` не можуть бути
одночасно в одному TU або linked executable.** Це різні definitions
FieldTable/CommandTable/catalogs, а не два взаємозамінні implementations.
Після namespace migration old/new comparisons потребують окремих
old-only/new-only fixtures і binaries. Не лінкувати старі objects із
новими та не залишати compatibility aliases як обхід. Тимчасовий old
target існує лише до останнього переведеного consumer і видаляється на Stage 19.

## 4. Stage 18: tracked consumers

### Demo та Qt playground

| Файл | Потрібна зміна | Поведінка, яку зберегти |
| --- | --- | --- |
| [DemoCatalog.h](../app/demo/DemoCatalog.h) | Native field factories name+binding; mixed tables/catalogs/Model; Request aggregate для Configure | Meter/Sensor values, stable IDs/row ordering, exact integer extrema, threshold setter refusal |
| [DemoCatalog.cpp](../app/demo/DemoCatalog.cpp) | Перевірити відповідність declarations після request зміни | Simulated advance behavior |
| [mainwindow.cpp](../app/mainwindow.cpp) і [header](../app/mainwindow.h) | Typed ordered traversal/native reads; прибрати Scalar formatter і старі JSON serializers | Exact U64/S64 text, units, UI ranges/defaults, enum labels, commands і periodic refresh |
| [main.cpp](../app/main.cpp) | Request calls замість positional arguments; explicit native values/As access | Smoke verifies values and successful/refused application commands |
| [DeviceResources.cpp](../app/resources/DeviceResources.cpp) і [header](../app/resources/DeviceResources.hpp) | Descriptor+values providers замість schema+commands+values; оновити IDs/count | Generic resource facade і packet protocol contract |
| [telemetry.pro](../telemetry.pro) | Єдиний final `.pri`, v3 provider selection і JS path | Qt build/smoke без legacy sources |

У поточному Meter `configure(float, Mode)` спирається на стару `arg`
metadata для validation. Після переходу Request має містити native float
і scoped Mode; callable перевіряє finite threshold, діапазон 1..1000 і
допустимі Mode codes **до** зміни threshold/mode. Smoke вже вимагає
`InvalidValue` для 1001. Це збереження application behavior, а не
додавання semantic limits до TypeRegistry.

Units `V`, `A`, `kW`, `degC`, початкові UI values, spinbox range і
Off/Auto/Manual labels залишаються в application/UI. `Persistent` flag
у demo був metadata; він сам не реалізував persistence. Не переносити
його як вигадану гарантію storage. Якщо metadata прибирається з UI,
це має бути окреме явне consumer рішення, не тихий наслідок видалення core.

Зберегти scope enum `Mode : uint16_t`. Native enum codec допускає unknown
representable codes; application setter/Command має сам встановити свою
policy. Перевірити порівняння зі старою поведінкою Mode field/Configure
і refusal без mutation. Defaults залишаються initial Meter/UI state.

Resource paths після міграції: `/telemetry/descriptor.bin` і
`/telemetry/values.bin`. Old schema/commands file indices зникають;
[DeviceCheck.cpp](../tests/resources/DeviceCheck.cpp) має перевіряти новий
count, paths, reads і generic protocol разом із app facade.

### Browser, Qt example, optional protocol і MCU consumers

- [structured_client/Device.hpp](../examples/structured_client/Device.hpp),
  [QtSmoke.cpp](../examples/structured_client/QtSmoke.cpp), [qt.pro](../examples/structured_client/qt.pro):
  public includes/namespace, shared statuses/slots, final provider `.pri`.
- [app.js](../examples/structured_client/app.js),
  [client/Check.mjs](../tests/structured/client/Check.mjs), browser runner:
  import `web/telemetry.js`; зберегти descriptor/values decoding, BigInt,
  shape-driven request/response editors, ceilings і cache/fingerprint policy.
- [structured_protocol](../examples/structured_protocol/README.md): оновити
  core/resource includes і aliases; packet magic/status codes, validation,
  caller Workspace і routing не змінюються. Transport agreement є прикладом,
  а не новою умовою native або encoded core calls.
- [MCU Fixture.hpp](../tests/structured/mcu/Fixture.hpp), `Mixed.cpp`,
  `Scale.cpp`, qualification/endpoints/descriptor/resource/exchange H7S
  fixtures: це tracked MCU test consumers, їхні build source lists та
  evidence manifests також переходять. Окремого firmware application
  consumer всередині цього telemetry repository не виявлено.

## 5. Перевірки, які не можна втратити при перейменуванні

| Місце | Ризик | Потрібна дія |
| --- | --- | --- |
| [mcu/run.py](../tests/structured/mcu/run.py) | Усі frames під `lib/telemetry/` зараз вважаються legacy й пропускають limits | Прибрати path-based legacy exemption; після move new core frames мають перевірятись |
| [facade/run.py](../tests/structured/facade/run.py) | Scan старого `lib/telemetry_structured` може стати порожнім | Scan final directory; не дозволити нуль headers як успішний boundary check |
| [structured resources/run.py](../tests/structured/resources/run.py) | Standalone header scan старого resource tree | Scan `lib/resource/telemetry/v3/` з правильними includes |
| [resources/run.py](../tests/resources/run.py) | Виключає тільки root component `structured`; після move захопить v3 без backend includes | Generic core scan звузити до власних headers; v3 перевіряється своїм runner |
| [qualification/ScalarComparison.cpp](../tests/structured/qualification/ScalarComparison.cpp) | Один TU містить new+old same-name tables після namespace move | Розділити binaries до Stage 19; потім прибрати old controls і зберегти direct/new codegen |
| [mcu/Scale.cpp](../tests/structured/mcu/Scale.cpp) | oldLocal/oldScalar та operation index arithmetic | Видаляти old probes лише після baseline; оновити indices/expected/refusal checks, зберегти direct/native/visitor/As/encoded |
| `endpoints/h7s/*` із `ENDPOINT_LEGACY` | Legacy branch зараз також бачить new fixture | Old-only/new-only TU boundaries; після Stage 19 old branch/target зникає |
| [resources.pro](../tests/structured/resources/resources.pro), [run_qmake.py](../tests/structured/resources/run_qmake.py) | `MODE=both` стане ODR conflict | Після Stage 17 окремі old/new targets; фінально тільки core-only і v3 selections |
| Freeze generated Manifest, source lists, header probes, JS imports | Hardcoded structured paths і namespaces | Оновити всі tracked runnable inputs; wire constants/golden bytes не оновлювати для проходження |
| [CI workflow](../.github/workflows/ci.yml) | Застарілі compile targets, symbol/path scans або зниклі test steps | Зберегти semantic matrix та artifacts; оновити тільки відповідні paths/targets |

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
old codegen probes і build targets видаляються **після** останнього
переведеного consumer. Не видаляти весь `tests/regression/` за назвою:
shared IDs/slot/callable/numeric edge coverage спершу перенести на
кінцеві headers. Зокрема SlotCallable/SlotEdges/SlotOverload, owner/null/
weak targets, ID boundary/explicit-template-argument cases і FP mode
refusals залишаються релевантними збереженим contracts.

Generic resource `CoreCheck.cpp`, `Negative.cpp`, `TestSupport.hpp` і
`stack_check.py` лишаються; DeviceCheck переходить на v3 app files.
Legacy Binary/TelemetryFiles/Metadata/decoder/goldens перевіряли v2
layout і видаляються або архівуються. Якщо old fixture також перевіряв
generic cursor/protocol/error поведінку, зберегти ці cases на generic
provider або v3 перед його видаленням.

Фінальний gate має встановити:

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

Цей список є preparation map. Він не означає, що Stage 16 або будь-який
етап перенесення вже виконано.
