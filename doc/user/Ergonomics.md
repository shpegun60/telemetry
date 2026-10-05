# Runtime native API та зручний обхід

[Посібник](README.md) · [Fields](Fields.md) · [Commands](Commands.md) ·
[Services](Services.md) · [Каталоги](TablesAndCatalogs.md) ·
[Повна runnable програма](../../examples/user_guide/Ergonomics.cpp)

Цей розділ збирає типові операції, для яких не потрібно писати власний typed
visitor. Всі доповнення працюють із поточними таблицями, positional IDs і
bindings; encoded wire v3 та правила володіння залишаються спільними.

## Вибрати API за завданням

| Потрібно | API | Результат |
| --- | --- | --- |
| Прочитати й явно скопіювати/перетворити | `fields.readAs<T>(id)` | `optional<T>` |
| Те саме з причиною відмови | `fields.readAsResult<T>(id)` | `FieldReadResult<T>` |
| Отримати const view існуючого значення | `fields.readBorrowed<T>(id)` | `BorrowedValue<T>` |
| Виконати Command із native Request | `commands.callAs(id, request)` | `NativeCallResult<CommandResult>` |
| Викликати Service із відомим exact Result | `services.callAs<Result>(id, request)` | `NativeCallResult<Result>` |
| Обійти typed definitions до зупинки | `table.forEachWhile(visitor)` | `bool`: повний обхід |
| Обійти плоскі erased rows із ID | `catalogs.forEachEntry(visitor)` | `void` |
| Те саме до зупинки | `catalogs.forEachEntryWhile(visitor)` | `bool`: повний обхід |

Для local table замість packed `id` передається local position, включно зі
scoped position enum. Existing `call<Position/Id>()`, `read<Position/Id>()`,
`readAs`, `writeAs`, `forEach` і `visit` зберігають свої контракти.

## Native routing та application result

```cpp
auto r = commands.callAs(id, Configure{25});
if (r.status() == telemetry::NativeCallStatus::Ok) {
    const telemetry::CommandResult outcome = r.value();
    // outcome може бути Executed, Accepted, Busy, Unavailable тощо.
}
```

`NativeCallStatus` має три значення:

| Status | Значення | Callback |
| --- | --- | --- |
| `Ok` | Exact endpoint result доставлений | Розв'язання binding виконано |
| `NotFound` | Неприпустима/відсутня позиція чи packed ID | Не викликається |
| `SignatureMismatch` | Row існує, але Request/Result відрізняється | Не викликається |

`hasValue()` зовнішнього result означає доставлений **результат endpoint**, а
не успішну прикладну дію. Порожній slot повертає outer `Ok` з inner
`CommandResult::Unavailable` або `ServiceStatus::Unavailable`.

Для Service Result задається явно, включно з ownership:

```cpp
auto owning = services.callAs<telemetry::ServiceResult<Reply>>(id, request);
auto borrowed = services.callAs<telemetry::BorrowedServiceResult<Big>>(id, request);
auto noPayload = services.callAs<telemetry::ServiceResult<void>>(notifyId);
```

Це три окремі сценарії; потрібна row має відповідати exact Request та Result.
Різні structs однакової форми не взаємозамінні; числових конверсій у callAs
немає. Для Command без Request використайте `commands.callAs(id)`.

`NativeCallResult<Result>::successFrom(factory)` конструює Result безпосередньо
у кінцевому union storage. Failure не конструює Result. Великий owning result
все одно займає свій розмір у вибраному caller storage: native виклик не
приховує heap або Workspace. Для великих відповідей callback зберігає
`ServiceResult<T>::successFrom(factory)`; encoded storage budget не змінено.
Explicit copy/move result користувачем залишається explicit copy/move.

## Borrowed Field та причина невдалого owning read

```cpp
auto config = fields.readBorrowed<Config>(id);
if (config)
    use(config.value());

auto result = fields.readAsResult<std::uint16_t>(id);
if (!result.hasValue())
    report(result.status());
```

`readBorrowed<T>` вимагає exact declared `T` і getter з `const T&`. Runtime
невідповідність чи owning getter дає empty view **до getter**; static selection
має diagnostic. Конверсій, object copy та Workspace lease немає. View не
подовжує lifetime і не фіксує snapshot: зміна owner змінює видимі дані.

`readAsResult<T>` повертає owning result; status:

| Status | Причина |
| --- | --- |
| `Ok` | Значення отримано |
| `NotFound` | Позиції/ID немає |
| `TypeMismatch` | Structural types несумісні |
| `Unavailable` | Getter/binding недоступний |
| `ConversionFailed` | Checked numeric conversion відхилена |

Getter викликається один раз. Type mismatch перевіряється до availability;
для failure `T` не конструюється. Wrapper резервує місце для owning `T`, тому
для великого існуючого об'єкта використайте borrowed form, якщо копія не потрібна.

## Обхід із ID та зупинка

```cpp
fields.forEachEntry([](telemetry::PackedId id, std::string_view group,
                       const telemetry::FieldEntry& row) {
    show(id, group, row.name);
});

bool completed = fields.forEachWhile([](std::string_view group,
                                        const auto& definition) -> bool {
    return inspect(group, definition); // false завершує весь обхід.
});
```

Flat callbacks отримують metadata rows, не typed definitions чи live values.
Ті самі `forEachEntry`/`forEachEntryWhile` є у `FieldIndex`, `CommandIndex` та
`ServiceIndex`: наприклад, `model.view().fields.forEachEntry(visitor)`.
Порядок — group, потім row; packed ID формується із цих позицій. Empty groups
пропускаються. Сам обхід не викликає getter/Command/Service.

`forEachWhile` зберігає звичайні callbacks та optional `<Position>` або
`<Group, Position>` форми typed visitor. Callback має повертати саме `bool`.
`true` result означає повний обхід, `false` — requested stop; empty table
дає `true`. Зупинка припиняє runtime invocation, але всі typed branches все
одно інстанціюються. Existing `forEach` продовжує ігнорувати callback result.

## Розміри caller-owned буферів

```cpp
constexpr auto commandRx = model.maxCommandRequestWireSize();
constexpr auto serviceRx = model.maxServiceRequestWireSize();
constexpr auto serviceTx = model.maxServiceResponseWireSize();
constexpr auto scratchBytes = model.maxScratch();
```

Це максимуми canonical payload однієї операції, без transport headers,
framing чи одночасних/nested operations. При constexpr використанні розміри
обчислюються під час компіляції; runtime query обходить metadata без endpoint
callbacks. Для resource envelopes використовуйте constants та builders із
[resource client API](../../lib/resource/protocol/README.md#bounded-client-api).

## Перевірка

[Ergonomic runner](../../tests/ergonomics/README.md) перевіряє native selection,
borrowing, числові відмови, strict traversal callbacks, resource client parsing,
128/256 targets та individual ARM stack frames для великих response. ARM
compile/disassembly не є виміром циклів на платі.
