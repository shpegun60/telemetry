# СКАСОВАНИЙ план: рефактор metadata старого scalar core

Дата: 2026-09-26. Автори: Ruslan Kovtun (shpegun60), codexAi.

Статус: **SUPERSEDED / CANCELLED. Не реалізовувати M0–M9.**

Раніше цей документ пропонував оптимізувати metadata старого C++17
`Scalar`/`FieldType`/`FieldTable` після structured Stage 15. Кінцева ціль
змінилася: [основний план](StructuredTelemetryV3ImplementationPlan.md)
тепер задає Stage 16–20, які переводять споживачів на один C++20 typed
core і **видаляють** стару scalar implementation разом із wire v2.1 та
JSON adapter. Тому рефактор зберігання об'єктів, призначених для видалення,
не є окремим етапом робіт.

Решта цього тексту збережена **лише як архів інженерних ідей і вимірювальних
критеріїв**. Його наказовий спосіб викладу, матриця тестів і Definition of
Done нижче не мають чинної сили. Якщо compact optional metadata потрібна
новому `TypeRegistry`/descriptor, її проектують і вимірюють для **нового**
core без старого ABI, `Scalar` або runtime wrapper. Service при цьому
залишається тільки name+binding, без limits/defaults/units.

Мета: винести з гарячого descriptor ті metadata, які не потрібні під час
виконання, і не створювати payload для відсутніх limits/defaults.
Typed-шлях має знати відсутність обмежень із типу definition й усувати
відповідну роботу через `if constexpr`.

Останні уточнення користувача:

- Для Field назва і unit існують завжди; порожній unit — теж заданий рядок.
  Command має назву; labels/unit його аргументів лишаються необов'язковими.
- Limits і default можуть бути відсутні незалежно один від одного.
- Виносимо всі metadata, які можна винести **без сповільнення виконання**.
- Цей refactor робиться тільки після завершення попереднього structured плану.

Отже обов'язкові labels і необов'язкові числові payloads — різні частини.
Якщо name/unit перенесені за загальний metadata pointer, у named Field
цей pointer не буде null. Null позначає відсутність саме optional частини.
Додатковий доступ до limits під час запису приймається лише після
перевірки швидкості, а не як автоматична ціна економії Flash.

## Навігація

- [1. Межі й незмінні контракти](#scope)
- [2. Що є в поточній реалізації](#current)
- [3. Значення nullptr і види metadata](#presence)
- [4. Зберігання та час життя](#storage)
- [5. Compile-time і runtime виконання](#execution)
- [6. Commands](#commands)
- [7. Схеми, сумісність і ABI](#compatibility)
- [8. Flash, кеш і критерії виграшу](#measurement)
- [9. Послідовність реалізації](#steps)
- [10. Перевірки та умови завершення](#checks)

<a id="scope"></a>
## 1. Межі й незмінні контракти

Рефактор стосується **наявних scalar Field/Command і способу зберігання
їхньої metadata**. Він не додає limits до structured Service, не додає
metadata в TypeRegistry і не змінює протокол початкового узгодження.

Потрібно зберегти основний декларативний API:

```cpp
constexpr telemetry::FieldTable fields{
    telemetry::field<&Meter::readVoltage>("Ua", "V", meter),

    telemetry::field<&Meter::readThreshold, &Meter::setThreshold>(
        "VoltageLimit", "V", meter,
        telemetry::limits(250.0f, 1.0f, 1000.0f))
};
```

У першого числового поля немає окремого блоку default/min/max; у другого
є один блок із заданими значеннями. Користувач не оголошує вручну
додатковий static limits object для кожного звичайного `field(...)`.

Інваріанти:

1. Getter/setter/command прив'язки, ID, доступність slots і порядок
   перевірок зберігають чинну поведінку.
2. Read не перевіряє min/max/default — незалежно від наявності metadata.
3. Write/command спочатку виконують чинне checked conversion, потім
   потрібні обмеження, потім один callback.
4. Відсутність custom limits не скасовує безпечне числове перетворення,
   поточні FP/enum правила або перевірку доступності цілі.
5. `limits(initial, min, max)` зберігає порядок аргументів і семантику.
6. Default не ініціалізує owner і не підставляє пропущений аргумент.
7. Schema output, binary v2.1 та їхні fingerprints не змінюються тільки
   через нове розміщення C++ даних.
8. Внутрішній ABI може змінитися; змішування старих і нових object files
   повинно відхилятися під час лінкування.
9. C++17 scalar consumer зберігається; новий structured C++20 модуль
   не стає обов'язковою залежністю scalar core.
10. Немає heap allocation, lazy allocation або нового `init()`.
11. Виміряне сповільнення read/write/command не приймаємо заради меншої
    Flash. Layout допрацьовується або конкретні робочі дані лишаються
    у швидкому поданні.

## 1.1. Зв'язок із попереднім freeze

Під час реалізації structured v3 старі scalar layouts/codegen лишаються
незмінними. **Після завершення structured v3** цей окремий етап свідомо
відкриває scalar in-memory layout для виміряного рефактору.

Baseline тут — майбутній фактичний SHA після structured freeze. Поточні
читання коду нижче служать орієнтиром; перед початком їх потрібно
перевірити ще раз. Не порівнювати майбутній результат лише з давнім SHA.

<a id="current"></a>
## 2. Що є в поточній реалізації

Огляд зроблено по вихідному коду; новий layout у цьому етапі не будувався
і його performance ще не виміряно.

| Частина | Фактичне зберігання зараз | Наслідок для плану |
| --- | --- | --- |
| [FieldType](../lib/telemetry/field/TelemetryFieldType.h) | Scalar tag, restricted flag, union bounds, initial Scalar, EnumOps pointer | Навіть звичайний numeric type має inline native bounds/default |
| [Field](../lib/telemetry/field/TelemetryField.h) | Inline `const FieldType declaredType`, окремий `readType` у read prefix | Головний кандидат на винесення cold metadata |
| [FieldTable](../lib/telemetry/field/TelemetryFieldTable.h) | Один масив Field; definitions лишаються типами | Треба додати ownership тільки реально потрібних payloads |
| [FieldDefinition](../lib/telemetry/field/TelemetryFieldFactory.h) | Typed read обходить metadata; typed write перевіряє FieldType | Read не потребує нового metadata test; write можна спеціалізувати точніше |
| [Limits specs](../lib/telemetry/field/TelemetryLimits.h) | `NoLimits`, `ValueLimits<T,false>`, `ValueLimits<T,true>` | Presence частково вже виражена типами |
| [Command](../lib/telemetry/command/TelemetryCommand.h) | Уже є `const void* metadata`, invoke і shared params ops | Другий «новий metadata pointer» не потрібний |
| [CommandTable](../lib/telemetry/command/TelemetryCommandTable.h) | Володіє tuple metadata; `NoCommandArgs` дає nullptr | Треба зберегти sparse ownership і не створити зайвий масив параметрів |
| [CommandContract](../lib/telemetry/detail/TelemetryCommandBinding.h) | `hasMetadata<I>`/`hasBoundedMetadata<I>`, `if constexpr` | Частина бажаної compile-time оптимізації вже існує |

У [LimitsCodegen.cpp](../tests/LimitsCodegen.cpp) для ARM32 є assertions
`sizeof(FieldType)==48`, `sizeof(Field)==96`, `alignof(Field)==32`.
Це зафіксовані очікування поточного тесту, а не нове вимірювання під час
написання документа.

Важлива відмінність: `CommandParam` із повним FieldType створюється під
час опису параметра для синхронного visitor. Це **не постійний масив
FieldType усередині кожної Command**. Не треба «оптимізувати» його
додаванням такого масиву в іншому місці.

<a id="presence"></a>
## 3. Значення nullptr і види metadata

### 3.1. Обов'язкова й необов'язкова частини

Цільове логічне розділення, не остаточний C++ layout:

```cpp
struct FieldLabels {
    const char* name; // mandatory
    const char* unit; // mandatory; "" is allowed
};

struct OptionalNumericInfo {
    // Logical views: each may be absent independently.
    const BoundsMetadata* bounds = nullptr;
    const DefaultMetadata* initial = nullptr;
};
```

Цей приклад не вимагає двох додаткових фізичних pointers на кожен рядок.
Bounds/default presence можуть бути закодовані policy/kind і compact
storage, а view матеріалізуватися лише за запитом schema. M2 обирає
фактичний layout за сумарною Flash і cycles. Окремого нового рядка
`description` не додаємо: йдеться саме про name та unit.

Якщо pointer описує **всі** metadata разом із labels, для валідного named
Field він non-null навіть без limits. Перевіряти його на null для
визначення наявності bounds неправильно. Nullable є optional numeric
information; для неї не створюємо `emptyLimits` із native min/max/zero
заради спільного вказівника.

Будь-який non-null pointer веде на незмінний живий об'єкт. Numeric tag,
callback та потрібні факти write policy доступні без обходу labels і
schema metadata. Це може вимагати невеликого immutable tag або прямого
range handle у hot descriptor; економія pointer не важливіша за швидкість.

### 3.2. Що можна винести з hot descriptor

| Дані | Цільове місце | Причина |
| --- | --- | --- |
| Назва, unit, labels аргументів | Cold metadata, якщо це реально зменшує total storage | Звичайний read/write/invoke їх не читає |
| Custom default/initial | Optional cold payload | Це schema hint, не runtime initialization |
| Enum dictionary і description ops | Shared cold metadata | Назви enum не потрібні при виклику |
| FieldFlags/persistence hints | Cold metadata, якщо конкретна операція їх не використовує | Чинні read/write не перевіряють Persistent |
| Getter/Setter/invoker і context | Гарячий descriptor | Потрібні для виконання |
| Numeric type, capability, необхідний write-policy tag | Гаряче подання або compile-time тип | Без додаткового description lookup |
| Bounds активного write validation | Визначає A/B gate | Це робочі дані перевірки; pointer може додати memory access |

За потреби cold metadata є джерелом істини при construction, а
маленький hot tag — її незмінною проєкцією. Factory встановлює обидва
один раз. Не переносимо value type за pointer перед кожним getter і
не додаємо metadata access у no-range write лише заради однієї
універсальної структури.

### 3.3. Presence bounds і default

| Оголошення | Додатковий value payload понад обов'язкові labels | Чи потрібний custom range check |
| --- | --- | --- |
| Звичайний U16/F32 без numeric extras | Ні; optional view nullptr | Ні; базові conversion/FP правила зберігаються |
| Bool без numeric extras | Ні; optional view nullptr | Немає додаткового user range |
| `limits(250.0f)` | Initial/default | Ні |
| `limits(250.0f, 1.0f, 1000.0f)` | Initial і bounds | Так |
| Enum без явного `limits(...)` | Відомості про enum та чинний inferred interval/default | Так, якщо цього вимагає поточний scalar enum контракт |
| `limits(Mode::Auto)` | Enum metadata плюс обраний default | Inferred enum interval зберігається |
| `enumSpec<...>()` | Вибраний dictionary/interval/default | За поточним scalar enum контрактом |
| Command `arg<I>("Name", "unit")` | Labels, без range payload | Ні |

Тому не можна написати `if (field.metadata) checkRange(...)` і вважати це
повним правилом. Metadata може містити лише labels/default. І навпаки,
відсутній явний `limits` у enum не означає відсутності inferred bounds.

Відсутній optional payload для plain numeric type не означає «забути native межі».
`minimum()/maximum()/defaultValue()` для schema можуть обчислити їх
із tag: native lowest/max, zero initial. Для Bool schema лишається
false/true/false, для Null — її чинне null-подання.

Потрібні чотири логічні стани: без extras, default-only, bounds-only,
bounds+default. Наявний helper `limits(initial, min, max)` задає останній,
`limits(initial)` — default-only. Новий public helper для bounds-only
не є необхідною частиною цього storage refactor; внутрішнє подання не
має примушувати такий стан зберігати фіктивний default.

Відсутність stored default не змінює чинну schema: native zero або enum
default можна отримати з типу. Це derived schema value, а не окремий
payload на кожен row і не автоматичний запис у власника.

Bounds-only тут означає відсутність **окремо збереженого** initial,
коли його точне чинне значення можна відновити. Це не дозвіл повертати
zero поза заданим range або приймати сьогодні невалідний FieldType.
Чинне правило узгодженості initial/min/max зберігається. Якщо default
неможливо точно відновити, його payload потрібний. Умовне ущільнення за
конкретними значеннями не обіцяється лише через constexpr construction.

### 3.4. Нативні й автоматичні властивості

Не зберігаємо окремий scalar block заради стандартних min/max/zero.
Вони відомі з типу і матеріалізуються лише коли їх запитує холодний
schema/description шлях.

Enum — інший випадок: після стирання enum C++ типу одного `ScalarType::U16`
недостатньо, щоб відновити dictionary. Потрібне посилання на enum metadata.
Автоматичний опис одного enum можна розділити між багатьма fields/arguments,
але не видалити як «не задано limits».

Не переносимо unknown-enum policy structured v3 на поточний scalar core.
У scalar named extrema визначають допустимий числовий інтервал; interior
unnamed codes можуть бути дозволені. Це зберігається побітово й поведінково.

### 3.5. Явно задані native bounds

`limits(initial, lowest, max)` не має додаткового restricted interval,
але може мати інший default. Не губити default або знак `-0.0`.
Збереження двох bounds у такому випадку можна прибрати після
канонізації, якщо відтворення schema і validation лишається точним.

Не обіцяємо, що звичайний параметр constexpr-функції автоматично стає
template argument. Тип `ValueLimits<T,true>` знає, що bounds передані,
але не обов'язково знає їхні значення як частину типу. Статично доведена
відсутність metadata і усунення порівнянь із конкретними константами —
дві різні оптимізації. Друга потребує codegen доказу для конкретного
construction path.

<a id="storage"></a>
## 4. Зберігання та час життя

### 4.1. Таблиця володіє payloads

Безпечна цільова послідовність FieldTable construction:

```text
field(...) definitions own construction specs by value
    -> keep mandatory labels; select only required optional value payloads
    -> initialize immutable storage in the final FieldTable
    -> materialize Field descriptors pointing into that storage
    -> publish views only after construction
```

`limits(...)` повертає тимчасову value-specification. **Не можна зберегти
її адресу у Field.** Так само не можна послатися на `FieldDefinition`,
який був тимчасовим аргументом конструктора таблиці.

Metadata members ініціалізуються раніше за descriptor array. `FieldTable`
вже забороняє copy/move; це правило лишається потрібним для self-references.
Конструювання відбувається одразу за остаточною адресою. Повернення
prvalue table допускається лише коли стандартна copy-elision семантика
справді зберігає цю адресу; return named local не стає новим обхідним API.

`constexpr` таблиці зі статичним часом життя можуть містити metadata
в `.rodata`. Таблиця автоматичного часу життя має storage там, де
створена вона сама; pointer не переносить її payload у Flash.
Owners/slots можуть лишатися в RAM, не роблячи immutable metadata
динамічною. Розміщення перевіряється ELF/map.

### 4.2. Не резервувати limits/default block на кожен рядок

Storage необов'язкових значень формується за filtered typelist/index
sequence тільки для definitions, які справді мають payload. Mandatory
labels records можуть існувати для всіх named rows; їхня вартість
рахується окремо. Не створювати:

```cpp
LimitsBlock limits[FieldCount]; // would reserve bounds even for unbounded rows
```

Проста tuple усіх `NoLimits` теж не доводить нульової вартості:
empty objects можуть вимагати адрес/bytes та padding. Потрібна явна
спеціалізація порожнього storage і відсутність **окремого limits/default
record на кожен unconfigured row**. Один службовий empty subobject або
alignment padding таблиці рахується окремо у вимірюванні.

C++17 шлях не покладається на `[[no_unique_address]]`. Якщо C++20
consumer користується додатковою optimization, scalar C++17 build
зберігає коректний і виміряний fallback.

### 4.3. Фізичний формат блока

Вибір робиться прототипом, а не переписуванням усього core одразу:

| Варіант | Перевага | Вартість/умова |
| --- | --- | --- |
| Один tagged metadata block із чинними numeric representations | Найпростіше зберегти точні conversion/enum/schema semantics | Default-only block може мати невикористане місце; необхідно порахувати |
| Typed storage для default-only / bounded / enum refinements | Зберігаються лише потрібні native значення | Потрібні точний type-erasure контракт та ABI/schema adapters |

Ціль після прототипу — не тримати dummy min/max навіть у default-only
payload, якщо це не погіршує розмір коду й виконання більше, ніж економить.
Uniform block допустимий як проміжний виміряний прототип, а не як
прихована обіцянка оптимальної щільності.

Typed storage може використовувати спільні ops для холодного доступу
до min/max/default/dictionary. Відомий typed write має звертатися до
свого точного payload без загального switch або virtual interface.
Runtime erased write може мати bounded dispatch, але його ціна вимірюється.

Заборонені: читання payload як іншого типу, function-pointer casts,
вказівник на bounds union з неактивною alternative, невирівняний доступ.
Factory має доводити відповідність numeric tag, payload kind і typed
Access. Ручний runtime descriptor перевіряє її при побудові/імпорті.

### 4.4. Sharing і deduplication

Обов'язково дозволити спільні immutable enum descriptors. Додатково
можна повторно використовувати явно спільні static metadata blocks.
Автоматична дедуплікація однакових `limits(...)` не є передумовою першого
рефактору й не виправдовує runtime hash map або heap.

Повторення однакових чисел у різних definitions саме собою не гарантує,
що linker об'єднає storage. Якщо optimization спирається на sharing,
вона має бути явною і перевіреною map/symbols, а не випадковим constant merging.

### 4.5. Ручні низькорівневі Field

Поточний low-level `Field{..., FieldType value, ...}` міг безпечно
копіювати limits усередину себе. Компактний borrowed descriptor не може
зберегти таку семантику простим pointer на цей by-value аргумент.

До зміни public constructors треба визначити окреме правило:

- основний `field(...) + FieldTable` зберігає синтаксис і володіє metadata;
- borrowed ручний descriptor приймає лише metadata з достатнім lifetime;
- тимчасові metadata, `{}` та explicit-template обходи відхиляються;
- якщо потрібний ручний owning case, він отримує явний owning object,
  а не приховане allocation у компактному Field.

Бажано зберегти читання `field.declaredType.minimum()` через легкий view.
Але точний тип `declaredType`, `sizeof(FieldType)`, address identity і
ручні owning constructors можуть бути source migration. Це треба
перелічити в migration notes і тестах; не заявляти повну source/ABI
сумісність лише тому, що звичайний `field(...)` не змінився.

<a id="execution"></a>
## 5. Compile-time і runtime виконання

### 5.1. Read

Поточний Field::read уже працює з getter та `readType`, а typed read —
із native Access. Custom range/default не бере участі в читанні.

Після рефактору read також:

```text
resolve getter / check availability where required
    -> read once
    -> preserve declared-type conversion contract
    -> return value
```

Немає `if (metadata != nullptr)`, завантаження metadata pointer,
range check або читання default. Це стосується і полів, у яких limits є.
Метадані потрібні serializer-у schema, а не звичайному read.

Отже прибрати limits-check із read як нову optimization не можна —
його вже немає. Можливий виграш читання від меншого Field stride/кращого
розміщення кешу є гіпотезою до вимірювання. Однакова кількість інструкцій
не означає автоматично однакові або менші цикли.

### 5.2. Traits для typed write

Потрібно зберегти у Definition/Access точну metadata specification для
**всіх** форм: NTTP functions/methods, function parameters, []/+[],
borrowed callable, slots, manual Scalar fallback.

Зараз, наприклад, DirectFieldAccess зберігає Read/Write типи, але Limits
є параметром його make-функції. Для `if constexpr` інформація про
metadata має лишатися в definition type, а не губитися на materialize.

Логічні traits:

```text
hasOptionalValueStorage
hasCustomDefault
hasExplicitBounds
hasEnumSemantics
hasEffectiveWriteRange
```

Це compile-time факти, не п'ять нових runtime полів у Field.
Exact перелік traits можна скоротити, якщо один виводиться з іншого.
Ключове — не прирівнювати `hasOptionalValueStorage` до `hasEffectiveWriteRange`.
Mandatory labels не впливають на жоден із цих двох traits.

Схематичний typed write:

```cpp
// Pseudocode: helper names do not prescribe a new public API.
auto converted = checkedNativeConversion<Declared>(input);
if (!converted) return WriteResult::InvalidValue;

if constexpr (Policy::hasEffectiveWriteRange) {
    if (!acceptsRange(*converted, policyStorage)) {
        return WriteResult::InvalidValue;
    }
}

// Preserve native FP rules and safe enum construction as required.
return invokeSetter(*converted);
```

У no-custom-range numeric specialization взагалі немає metadata load
або перевірки її pointer. Float finite і enum representability правила
не треба помилково видалити разом із custom range.

### 5.3. Які перевірки залишаються

| Сценарій | Що не можна прибрати |
| --- | --- |
| U16 → U16, без custom range | Лише інші чинні правила binding/capability; custom bounds немає |
| Double → U16, без custom range | Перевірка представимості та чинне відкидання дробової частини |
| F32 запис, без custom range | Чинна scalar-вимога finite; відсутні bounds не дозволяють NaN/Inf |
| Читання F32 зі спеціальним FP значенням | Чинне read/conversion правило; write-only limits не застосовуються |
| Enum без явних limits | Чинний inferred interval і safe cast, де він потрібний |
| Порожній OwnerSlot/FunctionSlot | Чинний статус відсутності та callback count 0 |
| Read-only field | ReadOnly у чинному порядку, без спроби setter |

Structured Service v3 за своїм контрактом може приймати інші представимі
enum/FP values. Цей refactor не робить scalar і structured правила однаковими.

### 5.4. Runtime ID

Для `fieldIndex.write(id, input)` id справді динамічний. У загальному
descriptor path наявність range може визначатися hot policy tag або
прямим nullable range handle, без обходу name/unit/default metadata:

```text
find Field with checked bounds
    -> check writable
    -> normalize to declared type
    -> apply base scalar validity
    -> if the hot policy/handle requires a range: apply that range
    -> invoke setter once
```

Не обіцяємо, що compile-time магія усуне перевірку для довільного
runtime id. Вона зникає у specialization, де Policy відома, або після
доведеного compiler folding конкретної constant table.

Можлива майбутня реалізація через specialized validation thunk, але
не слід додавати ще один indirect call на кожен write лише заради
усунення простого null branch. Порівнюються обидва generated code paths.

<a id="commands"></a>
## 6. Commands

Command уже зберігає metadata pointer, а CommandTable володіє optional
metadata. Потрібно покращити/узгодити цей механізм, а не створити його копію.

Вимоги:

1. `NoCommandArgs` лишає argument-metadata view порожнім; відсутні user
   metadata не створюють bounds/default block на кожен параметр.
   Якщо name перенесено у спільний cold block, його pointer non-null
   не означає, що command arguments мають constraints.
2. Labels-only `arg<I>(...)` може потребувати storage labels, але не
   payload default/min/max і не range check при виклику.
3. Sparse metadata існують лише для заданих arguments. Не вводити
   arity-sized масив `FieldType` заради спільного Field/Command API.
4. Custom default не читається на execution path; це schema information.
5. Typed command використовує parameter policy types і `if constexpr`.
   Поточні `hasMetadata<I>/hasBoundedMetadata<I>` не можна замінити
   загальним pointer switch на кожен argument.
6. Runtime execute продовжує конвертувати й перевіряти всі аргументи
   до одного callback. Нове storage не змінює failure ordering.
7. `forEachParameter()` і indexed `visitParameter()` зберігають контракт
   синхронного опису; descriptor не зберігає адреси локальних projections.

CommandParam::type може стати view або залишитися тимчасовою owning
projection. Вибір визначають API сумісність і stack measurements.
Не варто міняти це лише заради однакового імені класу: транзитний
schema object і постійний Field storage мають різну вартість.

Якщо Command уже не зберігає зайвих numeric limits, етап може закінчитися
перевірками й спільними adapters без змін його layout. Це прийнятний
результат; не потрібно штучно забезпечити однаковий diff для обох сімейств.

<a id="compatibility"></a>
## 7. Схеми, сумісність і ABI

### 7.1. Lazy projection, без додаткового validation

Schema-only запити `minimum`, `maximum`, `defaultValue`, enum count/entry
мають повертати ті самі значення. За відсутності optional numeric payload
native властивості обчислюються з tag; за наявності читається потрібний
payload. Name/unit доступні окремо незалежно від optional чисел.

Потрібно зберегти:

- Numeric native min/max як null у чинному JSON-поданні.
- Bool та enum extrema як явні значення.
- Default-only metadata, enum default і exact U64/S64 значення.
- Знак нуля default там, де його зберігає чинний контракт.
- Поточні binary bytes/counts/fingerprints і resource size/cursor behavior.

Wire не має отримати новий «metadata absent» flag тільки тому, що в
C++ тепер nullptr. Це внутрішня економія, а не нова семантика схеми.
Адреса payload, спосіб sharing і layout не входять у wire fingerprint.
Жодних нових fingerprint checks у data packets через цей рефактор.

### 7.2. In-memory ABI

Перенесення inline FieldType за pointer змінює розмір, offsets та
семантику descriptor. На фактичному стартовому SHA треба збільшити
scalar ABI revision й оновити
[exact ABI guard](../lib/telemetry/abi/TelemetryAbi.h).
Не фіксуємо номер наступної revision зараз: до цього етапу можуть бути інші зміни.

Guard включає всі нові views/payload headers/ops, що перетинають TU
boundary. Навіть коли sizeof випадково збігся, нова семантика потребує
іншого revision. Old/new consumer mismatch має дати link failure при
звичайному linking, LTO/GC і підтримуваних PIC/PIE конфігураціях.

Якщо structured ABI використовуватиме змінені scalar views, його tag
також оновлюється. Якщо не використовує — його wire і ABI не змінюють
«за компанію». Guard не додається до runtime read/write.

### 7.3. Міграція

Зберігаємо primary factory syntax та всі прив'язки. Окремо перевіряємо
low-level manual Field, FieldType construction/copies, visitor views,
`declaredType` access та constexpr usage у кількох TU.

Source breaking зміни ручного owning API описуються до merge разом із
коротким шляхом міграції. Не допускаємо сумісного на вигляд коду,
який після цього почав зберігати dangling metadata pointer.

<a id="measurement"></a>
## 8. Flash, кеш і критерії виграшу

### 8.1. Вказівник не є автоматично безкоштовним

Для N fields і K матеріалізованих optional value blocks:

```text
before = N * oldFieldStride + oldAuxiliaryStorage
after  = N * newFieldStride
       + mandatoryColdLabelsAndFlags
       + sum(actualMetadataBlockSizesWithPadding)
       + newSharedOpsAndIndexStorage
```

Порівнюється **весь linked image**, включно з `.text`, `.rodata`,
relocations у відповідному build, `.data/.bss` і table padding.
Не оголошуємо виграш лише за `sizeof(Field)`.

Наприклад, якщо прототип реально отримає Field 64 bytes замість 96,
але sidecar лишиться 48 bytes:

```text
table-only difference = -32*N + 48*K
```

При K=N це вже більше storage, ніж baseline. Це арифметичний приклад,
**не вимір і не обіцянка** розмірів нового layout. Typed payloads,
sharing і мала частка обмежених полів можуть змінити результат.
Приклад припускає, що name/unit уже враховані в Field або окремо не
змінили вартість. Якщо їх винесли в додаткові cold records, цю вартість
також додаємо — приховувати її за меншим `sizeof(Field)` не можна.

Один pointer на ARM32 зазвичай займає 4 bytes, але видимий виграш stride
визначають alignas/cache-line rounding. Вивільнення 10 bytes усередині
96-byte рядка не обов'язково зменшить рядок узагалі.

Зокрема name/unit/flags можуть зараз займати проміжок до aligned setter.
Винесення цих полів, яке не зменшило stride, але додало cold pointer і
окремий labels record, може **збільшити** Flash. Тому M2 порівнює не
лише payload formats, а й такі placements:

| Прототип | Mandatory labels | Optional values | Для чого |
| --- | --- | --- | --- |
| Baseline | Чинне inline подання | Чинне inline FieldType | Контроль |
| A | Лишаються в descriptor/padding | Sparse pointer storage | Перевірити власне економію limits |
| B | Окремий cold record | Sparse/typed cold storage | Перевірити максимум зменшення hot stride |
| C, якщо B повільніший | Як у кращому A/B | Direct hot range handle або інше розміщення активних bounds | Зберегти швидкість restricted writes |

Немає вимоги будь-що прийняти B: «винести всю можливу metadata» означає
всю ту, винесення якої покращує результат і не погіршує швидкість.

### 8.2. Вплив на cache access

Read prefix має залишитися компактним і незалежним від cold payload.
Restricted write може отримати додатковий memory access до payload,
який раніше лежав поруч із setter у тому самому Field.

Тому вимірюємо:

- Field read Scalar/native, локальний/global typed і runtime ID.
- Unrestricted integer write, finite F32 write, custom bounds write.
- Enum/default-only paths і Command з різними metadata density.
- Flash/RAM tables, hot/sequential/pseudorandom IDs, 16/128/1024 entries.
- Metadata density 0%, 1%, 10%, 50%, 100%; unique і shared blocks окремо.
- O2/Os, а Og — як обов'язковий working debug build.

Не припускаємо, що менша Flash автоматично означає меншу latency.
Не повертати 128-byte Field або необов'язковий indirect call без
окремого виміру, який пояснює ціну.

### 8.3. Умови прийняття layout

Обов'язкові: точна поведінка, no dangling pointers, незмінний read
contract, відсутні metadata loads на known no-range typed write,
відсутні optional value blocks для звичайних unconfigured numeric rows.

Основна performance ціль: менше Flash у реальних каталогах без
погіршення їхніх важливих read/write paths. На наявних codegen fixtures
спочатку вимагаємо незмінності інструкцій; відмінності аналізуємо
окремо й підтверджуємо MCU-виміром. Зміни size multipliers/offsets через
новий stride самі собою не називаються регресією або виграшем.

Якщо pointer layout погіршує restricted writes, це не приховується
середнім результатом переважно read-only fixture. Layout допрацьовується
або конкретні active bounds лишаються в швидкому поданні. За цим планом
regression не приймається. «Усі тести зелені» не є доказом достатньої
швидкості; потрібні repeated A/B samples і заздалегідь описаний спосіб
відрізнення regression від вимірювального розкиду.

Абсолютної гарантії однакових cycles для всіх майбутніх compilers,
розташувань Flash і станів cache документ не дає. Потрібні визначена
матриця profiles, однакові compiler/link flags та показаний розкид.
Немає статистично підтвердженого сповільнення на цій матриці — умова
adoption; відомий повільніший profile не можна списати як «майже те саме».

<a id="steps"></a>
## 9. Скасована послідовність реалізації (архів)

### M0. Вхідний gate після structured v3

Перевірити завершення етапів 00–15 попереднього плану, exact-SHA CI,
потрібні MCU receipts і чистий scoped checkpoint. Перечитати фактичні
Field/Command/serialization/ABI implementations. Зберегти новий baseline
sizes/codegen/stack і representative catalogs.

До цього gate робота обмежується цим документом.

### M1. Presence policy і поведінкові fixtures

Визначити NoNumericExtras/DefaultOnly/BoundsOnly/BoundedWithDefault/Enum
cases, незалежно від обов'язкових name/unit. Перевірити повну
матрицю factory forms; зафіксувати golden behavior для conversion,
NaN/Inf, enum extrema/gaps, default-only, schema і callback counts.
Не змінювати storage до появи цих controls.

Результат: які саме payloads не потрібні, відомо із типів; native base
checks відділені від custom range.

### M2. Ізольовані layout прототипи

Порівняти placements A/B/C із розділу 8 і uniform/typed sparse
payloads на однакових definitions. Побудувати sizeof/alignof/offsetof,
повний storage budget, O2/Os codegen і pointer-access diagram.
Жодний прототип не стає main layout лише через менший `sizeof(Field)`.

Результат: обраний storage/type-erasure варіант і low-level API migration.

### M3. Ownership у FieldTable

Definitions володіють construction specs; FieldTable переносить потрібні
значення у власний sparse storage і лише потім створює Field pointers.
Порожні policy types не створюють по payload на рядок. Direct construction,
constexpr tables, static/global instances і runtime local tables перевірені.

Результат: немає посилань на `limits()` temporary або тимчасову definition.

### M4. Read/write specialization

Зберегти metadata policy у всіх Access types. Read не торкається metadata.
Typed no-range write не завантажує metadata pointer. Restricted/enum
writes зберігають точну precision і порядок помилок. Runtime fallback
перевіряє nullable metadata коректно, без нового зайвого indirect dispatch.

Результат: behavior parity та перший ARM codegen звіт.

### M5. Command metadata і schema projections

Повторно використати чинний Command.metadata та ownership tuple.
Labels/default-only не стають range blocks. Зберегти sequential/indexed
parameter visitors, enum dictionaries і exact schema bytes. Не вводити
постійний full-arity descriptor array.

Результат: команди не отримали зайвої пам'яті чи перевірок через уніфікацію.

### M6. ABI й міграція

Оновити фактичну scalar ABI revision/tag, адаптери й ті structured
boundaries, яких зміна справді стосується. Додати mismatch link tests,
перенести manual Field examples на безпечне ownership правило,
зберегти primary user declarations і перевірити C++17 consumers.

Результат: несумісні binary layouts не змішуються, source migration явна.

### M7. Повна software перевірка

Host GCC/Clang C++17/20, sanitizer, GCC null-check modes, Og,
ARM O2/Os codegen/stack, linked no-heap, v2.1 goldens, structured
regressions і qmake matrix. Окремо перевірити emitted symbols:
NoNumericExtras fixture не містить per-row bounds/default blocks,
але зберігає всі name/unit strings і потрібні pointers на них.

Результат: виміряні повні footprints, а не лише C++ type sizes.

### M8. MCU A/B і рішення про adoption

На підключеній платі виконати той самий benchmark fixture для baseline
та pointer variant із потрібними density/cache profiles. Дотриматися
backup/restore порядку; записати source/compiler/flags/hashes.
Порівняти цикли, Flash, RAM, stack і codegen.

Результат: прийняття виміряного layout або повернення до M2/M4 із
конкретною причиною. Без плати цей acceptance gate не закритий.

### M9. Публікація завершеного рефактору

Оновити README/layout diagrams/ABI notes й приклади. Зробити scoped
commit, перевірити exact-SHA remote CI, local/remote SHA та worktree.
Зафіксувати чесно, де виграно bytes/cycles і де залишилися costs.

<a id="checks"></a>
## 10. Архівні перевірки та умови завершення (неактивні)

### 10.1. Обов'язкова матриця

| Зона | Що перевірити |
| --- | --- |
| Mandatory labels | Назва й unit збережені навіть коли немає optional values |
| No numeric extras | Optional view порожній; немає per-row min/max/default symbols |
| Default-only | Default зберігається; write не завантажує його і не отримує range check |
| Restricted numeric | Inclusive bounds, exact integers, F32 без проміжного double |
| Full native explicit bounds | Schema/default parity; фактична відсутність payload — тільки якщо доведена |
| Float | NaN/Inf write rejection і read behavior не змінені; signed-zero default |
| Enum | Auto/explicit dictionaries, extrema, gaps, large signed/unsigned codes |
| Factories | NTTP, functions arguments, []/+[], methods, closures, всі slot families |
| Lifetime | `limits()` temporary безпечна через table ownership; manual borrowed temporary rejected |
| Table movement | Copy/move/view-from-temporary заборони збережені |
| Commands | NoCommandArgs empty view, labels-only, sparse parameters, default-only, bounded |
| Schema | JSON/v2.1 bytes, sizes, fingerprints, visitors і cursor behavior збігаються |
| Read codegen | Жодного metadata pointer load/null test незалежно від limits presence |
| Typed write codegen | No-range specialization без metadata load; обов'язкові conversion/FP checks збережені |
| Runtime write | No metadata, restricted metadata та помилки availability у чинному порядку |
| ABI | Revision/layout mismatch не лінкується, зокрема LTO/GC/PIC |
| Flash placement | Const tables/metadata в очікуваних ELF sections; local storage не називається Flash |
| MCU | Density/cache profiles; отримані результати прив'язані до конкретного SHA |

Основні наявні fixtures для розширення:
[TelemetryLimitsCheck.cpp](../tests/TelemetryLimitsCheck.cpp),
[LimitsCodegen.cpp](../tests/LimitsCodegen.cpp),
[FieldTableCodegen.cpp](../tests/FieldTableCodegen.cpp),
[CommandTableCodegen.cpp](../tests/CommandTableCodegen.cpp),
[IndexCodegen.cpp](../tests/IndexCodegen.cpp).

### 10.2. Definition of done

- [ ] Попередній structured v3 план повністю завершено до implementation цього етапу.
- [ ] Name/unit присутні завжди; їх не губить відсутність numeric extras.
- [ ] Plain numeric/Bool без numeric extras не мають per-row limits/default payload.
- [ ] Default-only і bounds-only presence незалежні; schema не отримує фіктивних stored values.
- [ ] Bounds/default/enum data не зникли там, де вони потрібні за чинним контрактом.
- [ ] Основні `field(...)`/`command(...)` declarations не потребують ручного static storage.
- [ ] Відсутні dangling pointers із temporary limits/definitions/manual rows.
- [ ] Read не читає metadata; typed no-range write не перевіряє її pointer.
- [ ] Command повторно використовує чинний nullable metadata mechanism.
- [ ] Conversion, finite, enum, status ordering, callback counts та default semantics незмінні.
- [ ] Wire bytes/fingerprints збережені, нових per-packet checks немає.
- [ ] ABI revision/guards і source migration актуальні.
- [ ] Flash/RAM/stack/cycles виміряні; full-density metadata cost не прихована.
- [ ] Жоден прийнятий layout не сповільнює перевірені read/write/command profiles.
- [ ] Host/CI/ARM/MCU gates пройдено на фактичному підсумковому коді.
- [ ] Structured Service і TypeRegistry не отримали limits як побічний ефект рефактору.

Ціль — **не зберігати того, що можна точно відновити з типу, і не
виконувати роботу, відсутність якої доведена типом definition**. Наявність
pointer сама по собі не доводить ані меншого Flash, ані швидшого доступу;
це має підтвердити implementation і вимір після завершення structured v3.
