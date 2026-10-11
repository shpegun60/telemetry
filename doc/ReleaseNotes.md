# Telemetry release notes

Автори: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.

[Документація](README.md) · [Migration guide](StructuredTelemetryV3MigrationGuide.md) ·
[API Map](user/APIOverview.md)

## 2026-10-11: owning result pointer lifetime

[`5d2bb93`](https://github.com/shpegun60/telemetry/commit/5d2bb937880cb5c47da5399420469803d5ecf8ac)
посилює compile-time контракт трьох owning wrappers:
`ServiceResult<T>`, `NativeCallResult<T>` і `FieldReadResult<T>`.
`valueOrNull()` тепер доступний лише для `&` і `const&`; `&&` та `const&&`
видалено. Те саме стосується `FieldReadResult::operator->()`.
Збережіть result у змінну перед отриманням pointer. Навіть короткий
`makeFieldResult()->member` тепер відхиляється: він використовує той самий
accessor, що й небезпечне збереження pointer з temporary.

`value() &&` залишається для move-out. Посилання, отримане з temporary
через `value()` чи `operator*()`, не подовжує lifetime wrapper; переносіть
саме значення або тримайте owning result живим. Borrowed wrappers посилаються
на зовнішній об'єкт і в цьому коміті не змінюються.
Повний [контракт результатів](user/NativeApi.md#16-borrowed-lifetime-і-узгоджені-дані)
та [тести](../tests/ergonomics/README.md#owning-result-pointer-lifetime)
описують ці відмінності.

### Повна чиста перебудова consumer

Після оновлення **повністю перезберіть усі translation units та static/shared
libraries consumer з поточними headers і однаковими configuration macros**.
Включіть dependent projects, precompiled headers і C++ plugins, якщо вони
використовують ці templates. Старі object files не слід змішувати з новими.
Стандартна clean/rebuild дія вашої build system має прибрати попередні objects;
ручний incremental link не є перевіркою сумісності.

Ref-qualifier входить у тип pointer-to-member-function. Для коду, який явно
задає цей тип, додайте `&` або `const&`, наприклад:

```cpp
using Result = telemetry::FieldReadResult<Config>;
using MutableAccessor = Config* (Result::*)() & noexcept;
using ConstAccessor = const Config* (Result::*)() const& noexcept;

const auto mutableAccessor = static_cast<MutableAccessor>(&Result::valueOrNull);
const auto constAccessor = static_cast<ConstAccessor>(&Result::valueOrNull);
```

Ref-qualifier також змінює C++ mangled symbol, коли compiler породжує
out-of-line method. Зміна не дає універсальної binary ABI compatibility зі
старими precompiled C++ objects, навіть якщо layout wrapper не змінився.
Незмінність перевірених ARM instruction/stack probes і дванадцяти firmware
`.bin` означає незмінність саме тих [перевірених образів](../tests/resources/evidence/README.md),
а не всіх можливих consumer builds.

Accessor bodies, member layout, canonical payload bytes і wire v3.0 не
змінено цим комітом; нових runtime перевірок не додано. Clean rebuild потрібний
через C++ source/type/symbol контракт. Після нього виконайте свої application
тести; qualification бібліотеки не замінює перевірку вашої прошивки.

## 2026-10-11: навігація публічним API

[`535e550`](https://github.com/shpegun60/telemetry/commit/535e550a5675b5dd4a995b2a933075dae056f653)
додає [API Map](user/APIOverview.md) і
[NativeOnly](../examples/user_guide/NativeOnly.cpp), а довідники розділяють
Native / Encoded / Codec / Resource сценарії без нового runtime API.
Documentation-only частина цього зрізу не змінює library headers.

## 2026-10-11: audit follow-up

Уточнено disjoint-output contract DescriptorFile та числовий/raw-bit контракт
JS для NaN. Перевірка Markdown file/heading links тепер запускається в CI;
додано bounded input corpora для resource, encoded payload і JS boundaries.
Production dispatch/codec не змінено. Повні
[результати та нові H7S receipts](evidence/audit-2026-10-11/README.md)
розділяють host checks, offline compiler gates і шість реально виконаних образів.
