# API: швидка шпаргалка

[User guide](README.md) · [Documentation index](../README.md) · [API cheatsheet](API-CHEATSHEET.md)

[Fields](Fields.md) · [Commands](Commands.md) · [Services](Services.md) ·
[Tables and catalogs](TablesAndCatalogs.md) · [Model](Model.md) ·
[Codec and Workspace](CodecAndWorkspace.md) · [Resources](Resources.md) ·
[Descriptor and Values](DescriptorAndValues.md) · [Flat runtime API](FlatNative.md)

`namespace ts = telemetry;` далі лише скорочення. Фрагменти використовують
named objects із [повного прикладу](../../examples/user_guide/Native.cpp).
Усі callbacks — `noexcept`; owners, names і slots живуть довше за таблиці.

## Навігація

- [Створити](#створити)
- [A. Compile-time exact — рекомендовано для application code](#a-compile-time-exact--рекомендовано-для-application-code)
- [B. Typed traversal — усі definitions](#b-typed-traversal--усі-definitions)
- [C. Runtime ID, typed definition](#c-runtime-id-typed-definition)
- [D. Runtime ID + bytes — transport/backend](#d-runtime-id--bytes--transportbackend)
- [Який обхід](#який-обхід)
- [Результати й пам'ять](#результати-й-память)
- [Files](#files)

## Створити

```cpp
ts::field<&Device::readVoltage, &Device::writeVoltage>("Voltage", device);
ts::field<&Device::borrowSettings>("Settings", device); // const T& output
ts::command<&Device::reset>("Reset", device);
ts::service<&Device::query>("Query", device);

// Definitions -> named local tables -> catalog groups -> Model.
ts::FieldTable localFields{ /* field(...) */ };
ts::FieldCatalogTable fields{ts::group("device", localFields)};
ts::Model model{fields, commands, services};
```

Field Value: bool / number / enum / `std::array` / supported aggregate.
Command і Service Request: відсутній або один aggregate `T` / `const T&`.
Service Response: aggregate або void. Business limits перевіряє callback.

## A. Compile-time exact — рекомендовано для application code

```cpp
localFields.read<Position::Voltage>();
localFields.write<Position::Voltage>(240.0f);
localCommands.call<0>();
localServices.call<0>();

constexpr auto id = ts::makeId<0, Position::Voltage>();
fields.read<id>();
fields.write<id>(240.0f);
commands.call<ts::makeId<0, 0>()>();
services.call<ts::makeId<0, 1>()>(Query{0});
```

Local Position — індекс у table; global ID — packed u32 group/entry (16/16).
`get<Position>()` / `get<Id>()` повертає concrete definition за const reference.

## B. Typed traversal — усі definitions

```cpp
localFields.forEach([]<std::size_t I>(const auto& field) {
    using T = typename std::remove_cvref_t<decltype(field)>::Value;
    // I і T відомі компілятору; field.read() викликайте явно.
});
fields.forEach([]<std::size_t G, std::size_t I>(
                   std::string_view group, const auto& field) {
    constexpr auto id = ts::makeId<G, I>();
    // Конкретна definition, group name і compile-time ID.
});
```

**`forEach` сам не викликає getter, Command або Service.**

## C. Runtime ID, typed definition

Рекомендовані runtime calls із одним outcome:

```cpp
const auto configured = commands.call(ts::makeId<0, 1>(), Configure{device.settings});
const auto reset = localCommands.call(0); // CommandCallStatus; no request.
const auto snapshot = services.callAs<Snapshot>(ts::makeId<0, 1>(), Query{0});
if (snapshot) {
    const Snapshot& response = snapshot.value();
    (void)response;
}
const auto settings = services.callBorrowed<Settings>(ts::makeId<0, 3>(), Query{0});
const auto ping = services.callAs<void>(ts::makeId<0, 4>());
```

`CommandCallStatus` охоплює application outcomes та runtime `NotFound` /
`SignatureMismatch`; `Accepted` не означає завершення. Service result має
один `hasValue()` / explicit `bool`, true лише при routing та application
success, і один `value()` для payload. `status()` — `ServiceCallStatus`;
nonvoid `valueOrNull()` повертає null при будь-якій відмові. Borrowed response
доступний лише як `const T&`; для `void` є success-only `value()` без payload,
але немає `valueOrNull()`. [Повні mapping та lifetime rules](FlatNative.md).

Typed visitors, Field access та наступні low-level calls збережено без змін:

```cpp
fields.visit(id, [](const auto& field) { /* concrete definition */ });
commands.visit(id, [](const auto& command) { /* typed branch */ });
services.visit(id, [](const auto& service) { /* typed branch */ });

// Field: відомий To або exact structural type, без visitor.
fields.readAs<double>(id);
fields.writeAs(id, 240);
localFields.readAs<double, Position::Voltage>();

// Low-level Command: routing та CommandResult окремо.
const auto configured = commands.callAs(ts::makeId<0, 1>(), Configure{device.settings});
const auto reset = localCommands.callAs(0); // No request.

// Low-level Service: exact result wrapper та exact request type.
const auto snapshot = services.callAs<ts::ServiceResult<Snapshot>>(ts::makeId<0, 1>(), Query{0});
const auto settings = services.callAs<ts::BorrowedServiceResult<Settings>>(ts::makeId<0, 3>(), Query{0});
const auto ping = services.callAs<ts::ServiceResult<void>>(ts::makeId<0, 4>()); // No request/payload.
```

Numeric `As` conversions checked; struct/array потребують exact C++ type.
Low-level Command `callAs` повертає `NativeCallResult<CommandResult>`; Service
`callAs<Result>` — `NativeCallResult<Result>`. Request і Service result wrapper
мають точно збігатися з declaration; conversions не виконуються. Input width
перевіряється до narrowing, table/catalog має бути lvalue.
`NativeCallStatus` — `Ok`, `NotFound`, `SignatureMismatch`. Останні дві відмови
не викликають endpoint. `Ok` доставляє application result, включно з
`Busy`/`Unavailable`. Typed `visit` із `if constexpr` лишається альтернативою
для різних shapes; byte payload обробляє encoded API. Повне runnable coverage:
[NativeCalls.cpp](../../tests/ergonomics/NativeCalls.cpp).

## D. Runtime ID + bytes — transport/backend

```cpp
ts::readFieldEncoded(model.view(), id, output, workspace);
ts::writeFieldEncoded(model.view(), id, input, workspace);
ts::executeCommandEncoded(model.view(), id, input, workspace);
ts::callServiceEncoded(model.view(), id, input, output, workspace);
```

Альтернатива — `model.fieldIndex()/commandIndex()/serviceIndex()` та їхні
`readEncoded/writeEncoded/executeEncoded/callEncoded`. Перевіряйте dispatch,
endpoint status і committed byte count. Transport спочатку виділяє повний
packet із UART/TCP chunks. [Encoded.cpp](../../examples/user_guide/Encoded.cpp)
показує готовий adapter без файлів.

## Який обхід

| Потреба | API | Що отримуєте |
| --- | --- | --- |
| Runtime metadata усіх endpoints | range-for | `FieldEntry` / `CommandEntry` / `ServiceEntry` |
| Concrete definitions усіх endpoints | `forEach` | Типізовану definition |
| Одну concrete definition за runtime ID | `visit` | Typed callback, bool знайдено/не знайдено |
| Одну concrete definition compile-time | `get` | Const reference на definition |
| Файли | range-for | Lazy borrowed `FileView` |

## Результати й пам'ять

| Getter / callback / call form | Native результат |
| --- | --- |
| Field `T` | `optional<T>` |
| Field `const T&` | `BorrowedValue<T>` |
| Command | `CommandResult` |
| Service `Response` / `ServiceResult<Response>` | `ServiceResult<Response>` |
| Service `const Response&` / `BorrowedServiceResult<Response>` | `BorrowedServiceResult<Response>` |
| Service void | `ServiceResult<void>` |
| Command runtime `call` | `CommandCallStatus` |
| Owning Service runtime `callAs<Response>` | `ServiceCallResult<Response>` |
| Borrowed Service runtime `callBorrowed<Response>` | `BorrowedServiceCallResult<Response>` |
| Low-level Command runtime `callAs` | `NativeCallResult<CommandResult>` |
| Low-level Service runtime `callAs<Result>` | `NativeCallResult<Result>` |

`readAs<T>` завжди owning copy. Borrowed result позичає existing const object;
він має лишатися живим і стабільним. Callback/static owning `ServiceResult`
перевіряйте через `hasValue()/status()`; цей wrapper не має optional-style
`bool/*/->`. Flat `ServiceCallResult` підтримує explicit `bool` та один `value()`.

У low-level API `NativeCallResult::hasValue()` означає доставку exact endpoint result.
Для Service це не означає inner `hasValue()` чи application success:
перевірте `result.value().status()` та `result.value().hasValue()` перед payload.
`valueOrNull()` safe також на selection failure. Final-storage
`successFrom(factory)` не додає move/copy wrapper/response; owning result
містить payload bytes, storage обирає caller. Borrowed lifetime rules ті самі.

Encoded default local budget — 32 B; larger objects використовують caller-owned
`Workspace`. Достатній bound однієї операції — `model.maxScratch()`. Синхронізація
і LIFO lifetime leases належать caller. Library core не виділяє heap.

## Files

```cpp
constinit const auto fs = resource::filesystem(
    resource::file("/settings.bin", settingsProvider));
for (auto file : fs) {
    const auto path = file.path(); // Немає provider call.
    const bool readable = file.readable(); // Також без provider call.
    const auto info = file.stat(); // Explicit size() call.
}
auto result = fs[0].read(cursor, output);
auto reply = resource::protocol::process(fs.view(), packet, response);
```

Provider: exact `size() const noexcept` та `read(cursor, Output) const noexcept`
і/або `write(cursor, Input, final) noexcept`. Paths і providers позичені.
`BytesFile` позичає stable byte array/span. Index — позиція в array; cursor —
opaque u64 provider state. Filesystem не створює directories/storage автоматично.

**Views позичають:** `FileSystemView` — descriptor array, `FileView` — один
descriptor, `ModelView` — runtime catalogs/types, `BorrowedValue` — const T.
`FileSystem` володіє тільки своїм descriptor array; `Model` позичає named
catalogs і формує structural registry, а не зберігає всі live values.

Повний cookbook: [Native API](NativeApi.md) ·
[Транспорт і ресурси](TransportAndResources.md) · [Всі приклади](../../examples/user_guide/README.md).
