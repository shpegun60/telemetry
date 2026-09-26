# Перевірка уточнень від 26 вересня

Дата: 2026-09-26. Автори: Ruslan Kovtun (shpegun60), codexAi.
База порівняння: `b19f1e938efac85f0614b465307468a3c03e6fe2`.

Цей документ уточнює [перший звіт](Resolution.md). Три незалежні review
перевіряли архів `981e6be`; це не був знімок тодішнього робочого дерева.
Їхні репро перечитано й повторено на виправлених джерелах. Оригінальні файли
review не переписувалися. Підтримувані перевірки розміщено в
[tests/regression](../../tests/regression/README.md) та
[tests/resources](../../tests/resources/README.md).

Номери D1–D7 у різних звітах означають різні речі. Нижче вказано зріз і
вихідний номер; повторні повідомлення з командами запуску не рахуються як
нові знахідки. Продуктові пропозиції критика, зміна архітектури й оновлення
vendored delegate не входили в роботу.

## Власники, шляхи та metadata

| Звіт / пункт | Зміна | Постійна перевірка |
|---|---|---|
| Resource D1, fields D1 / FL-F2, commands 2.1 | Окремі deleted-перевантаження закривають `{}`, `{temporary}` та braced conversion proxies: forwarding reference сам по собі не може вивести їхній тип. Живі cv/base lvalue та `{owner}` лишаються допустимими. | BorrowedBraceCompileFail: 72 відмови, 52 виконувані позитивні контролі; resource Negative: 34 відмови і 34 позитивні контролі. |
| Resource D2 | Тип провайдера й тип шляху виводяться незалежно; owning temporary string відхиляється також за явного provider type і всередині braces. | Negative.cpp: явний тип, derived/base, порожні braces, conversion wrappers, стабільні рядки/масиви/string_view. |
| Resource D5 / RS-F2 | Constructor має незворотну позначку відмови. Надлишкові, неупорядковані та неправильні entries відхиляються навіть коли custom traversal ігнорує `false` від sink. | MetadataContractCheck: 342 перевірки, включно з вкладеним enum traversal. |
| Resource D6 | Явні `std::ref(provider)` і proxy-lvalue не маскуються під справжній provider. Потрібно передати сам об'єкт або явно отримане посилання. | Позитивні й негативні binding controls; README описує зміну. |
| Resource M4/M5 | READ повторно обходить metadata для encoding, але не повторює validation/hash. LIST після InvalidData можна продовжити з `cursor + 1`; це пропуск одного невміщуваного path. | Уточнені README, існуючі LIST/resume перевірки збережено. |

Списки `{namedString}` для owning path теж відхиляються: після deduction
initializer_list уже немає надійної інформації про вихідну категорію
значення. Звичайний `file(pathString, provider)` працює, коли обидва об'єкти
живуть достатньо довго. Зберігання даних у FileEntry не змінено.

Неуспішний metadata provider має size 0, fingerprint 0 і READ з InvalidData,
незмінним cursor та нульовою кількістю записаних байтів. STAT як і раніше
повідомляє доступні можливості й size 0; зміни wire-формату немає.

## Поля та слоти

| Звіт / пункт | Зміна | Перевірка |
|---|---|---|
| Fields D3 / SL-F2 | Перевіряється й викликається звичайне перевантаження через `std::invoke`. Немає примусового `operator()<Args...>`, яке змінювало cv/reference поведінку. | SlotCallableCheck: 25 перевірок C++17 / 26 C++20; 15 відмов компіляції; оригінальні 15 допустимих і 5 недопустимих форм. |
| Fields D5, weak NTTP | Публічна NTTP-ціль повинна мати константну ненульову адресу. Тотожність template arguments цього не гарантує для weak symbols. | WeakTargetCompileFail: 11 відмов; NullChecksFlag виконує runtime weak/null controls. |
| Fields D7 / FL-F3 | Native setter однаково підтримує перевірене перетворення вказівника на функцію, NTTP, методу, borrowed callable та всіх видів слотів. Ручний Field може повторно використати setter з іншим declaredType. | SetterConversionCheck: 138 перевірок дев'яти способів прив'язки; відмова не викликає callback/getter. |
| Fields D6 | Конвертація іншого Scalar tag відділяється від звичайного виклику. Перевіряються самі erased thunks, а не лише зовнішні typed wrappers. | NativeSetterCodegen, BoundSetterCodegen та порівняння ARM disassembly. |

`Field::write()` спочатку приводить число до declaredType і перевіряє межі
цього типу. Потім native setter може потребувати ще одного перетворення.
Наприклад, навмисно звужений callback `float` після межі F64 `0.1` отримує
`0.10000000149f`. Це округлення типу callback, а не обхід первинної перевірки.
Звичайна inferred фабрика має однакові типи поля й setter. Власник даних
може додатково перевіряти фізичне обмеження у своїй сигнатурі.

Прямий `field.set(Scalar)` є викликом callback, а не policy API: він не має
доступу до меж Field. Для перевірки metadata потрібен `field.write(value)`.
Цю різницю, включно з ручним повторним використанням setter, закріплено тестами.
Scalar callbacks залишаються явно типізованим escape hatch.

У CubeIDE GCC 14.3.1 всі дев'ять перевірених erased adapters мають шлях точного
типу без стека та нетермінальних викликів при O2 і Os. Чотири invokers звичайних
native function pointers перевіряються окремо. Для GCC 13 при Os лишилася ціна:
сім адаптерів виносять `std::get_if` в окремий виклик і мають frame 16 B; у
чотирьох із них до додавання checked conversion було 8 B. При O2 GCC 13 теж
генерує всі дев'ять без стека. Цю різницю не приховано загальною заявою про
«незмінні інструкції»: runners мають окремі межі для цих шляхів. Перестановка
аргументів внутрішнього fallback зберігає єдиний snapshot слота; додаткового
читання slot перед викликом немає. Загальний Flash може зрости через нове
перетворення навіть коли звичайний dispatch короткий.

## ID, назви, JSON та ABI

| Звіт / пункт | Результат |
|---|---|
| Commands 2.2 | Локальні enum-позиції приймаються локальними таблицями. Global packed ID приймає лише integral типи, зокрема у template API. Enum спершу поєднується з групою через makeId. |
| Commands 2.3/2.4 | Class/atomic ID більше не потрапляє через неявне перетворення в неперевірене вузьке перевантаження. Усі глобальні точки входу узгоджені; виклик використовує `.load()` або явне отримання integral значення без звуження. |
| Commands 2.5 | Додано `makeId<Group, Entry>()` з гарантованою compile-time перевіркою. Ordinary call з літералами не обов'язково constant-evaluated: неправильний `makeId(0, 65537)` має runtime abort-контракт. Для fallible input є tryMakeId. |
| Commands 2.6 | High-level field/group factories відхиляють null name/unit під час constexpr construction або завершують runtime construction через abort. Порожні рядки допустимі. Низькорівневі descriptors лишаються доступними для fallible validation. |
| Commands 2.7 | groupOf/indexOf перевіряють початкову ширину перед extraction. tryGroupOf/tryIndexOf повертають optional. Звичайні u32 extraction лишилися двома ARM-інструкціями. |
| CJ-F1 | Emitted ABI references утримуються при linker GC. Namespace `TELEMETRY_RETAIN_ABI()` забезпечує compiler emission незалежно від викликів функцій. Перевіряються matching/mismatching links, LTO і compiler-omission controls. |
| CJ-F3 | До maintained ReviewCheck додано успішні нульові local/grouped command fingerprints. Нуль не означає помилку; optional відрізняє відмову. |
| CJ-F4 | Окремо скомпільовані TU перевіряють однакові адреси чотирьох inline JSON helpers. Контроль зі штучно поверненим TU-local helper завершується відмовою. |

IdBoundaryCheck має 122 runtime перевірки, 104 compile-fail cases і шість
abort-контролів, включно з ініціалізацією до main. ARM gate перевіряє старше
слово u64 до доступу/виклику; навмисно змінений регістр у disassembly має
провалити gate. Це закриває прогалину, яку host з 64-бітним size_t не бачив.

ABI лишається **8**; розміри й розміщення descriptor не змінювалися.
Header-only include сам по собі не створює ABI dependency. Compiler-omitted
код без namespace opt-in та невитягнуті archive members не перевіряються.
ARM retention зараз призначено для non-PIC збірок. Читання/запис Field не
викликає цей механізм і не отримує перевірки ABI під час виконання.

## Межі, які не можна називати повністю виправленими

- **SL-F1 / resource D4 / fields D2:** GCC з
  `-fno-delete-null-pointer-checks` досі відхиляє частину правильних constexpr
  function-pointer rows, у тому числі Persistent. Повна GCC UBSan збірка теж
  не проходить; явний `-fdelete-null-pointer-checks` не ремонтує всі випадки.
  Успіх кількох runtime suites не доводить успіх усієї збірки. Перевірки пам’яті й UB
  виконуються повністю через Clang ASan/UBSan; категорії не вимикалися.
  Див. [обговорення GCC PR71962](https://gcc.gnu.org/pipermail/gcc-patches/2025-July/688787.html).
- **NC-F1 / fields D4:** Clang no-honor guard є best-effort діагностикою.
  `-w`, `-isystem` і system_header можуть її приховати. Per-function fast-math
  теж не гарантовано виявляється. Ці режими не підтримуються в бібліотеці та
  її callers; precise pragma не виправляє вже змінені припущення caller.
- **CM-F1:** borrowed views не подовжують lifetime. `std::data`, `std::begin`
  або user helper з `const T&` можуть приховати temporary від direct-rvalue
  overload guards. Потрібні іменований живий owner/table і належна зовнішня
  синхронізація слотів. Це не доведена відсутність будь-якого майбутнього UB.

## Верифікація та відтворення

Постійні runners містять 262 review compile-fail cases з очікуваною причиною,
19 regression abort cases, 34 resource відмови з позитивними controls,
77760 native/dynamic parity порівнянь і багатомодульні ABI/JSON перевірки.
Final rejection total runner рахує зі справді виконаних перевірок. Попередній
timeout `981e6be` виправлено в `b19f1e9` поділом parity на 18 builds; ліміт
180 секунд і повну матрицю збережено. CI саме `b19f1e9` пройшов 7/7 jobs.

Node запущено локально: реальний decoder пройшов 1667 truncations і 6000
deterministic mutations. Qt 6.10.1 MinGW playground пройшов offscreen smoke,
окремий qmake no_json consumer теж зібрався й виконався.

Для command READ стековий gate тепер **176 B при O2 / 152 B при Os**,
замість загального 192 B. Мутації +1/+8/+16 B повинні відхилятися. Це межі
окремих frame, не сума всього call chain чи IRQ stack.

Повний ARM runner компілює 42 джерела й перевіряє 19 read-only probes при
кожному рівні оптимізації, включно з новими actual-setter та wide-ID probes.
Окремі typed field/command gates і старі межі не послаблено. Для 262 спільних
exported wrappers із дев'яти перевірених старих probes normalized instruction
streams збігаються з `b19f1e9`. Це не стосується всіх внутрішніх helpers:
конвертації та захист ABI закономірно додають код.

Повний resource demo, CubeIDE GCC 14.3.1:

| Режим | `.text` до / після | `.rodata` | `.data` | `.bss` |
|---|---:|---:|---:|---:|
| O2 | 32992 / 33848 B | 2336 B | 116 B | 444 B |
| Os | 24840 / 25400 B | 2272 B | 116 B | 444 B |

Це +856/+560 B коду відносно `b19f1e9`, без росту таблиць і RAM цього
linked fixture. Розмір усього образу не доводить швидкість окремого виклику.

Новий [H7S receipt](../../tests/regression/h7s/followup-receipt.json) зберігає
compiler, source/object/image hashes, 3328/3328 результатів при O2/Os і точне
відновлення 64 КіБ Flash. [Старий receipt](../../tests/regression/h7s/receipt.json)
залишено архівним. Offline verifier перевіряє структуру receipt і мутації;
зелений CI сам по собі не доводить новий запуск плати чи актуальність усіх
архівних вимірювань. Під час нового прогону хеші всіх 88 файлів бібліотек
окремо звірено з робочим деревом. Це correctness run, не новий cycle benchmark.

Команди запускаються з кореня telemetry, виходи — тільки в build:

```text
python tests/run_checks.py --cxx <MinGW-g++> --ar <MinGW-ar> --std c++17 --build-dir build/followup-host17
python3 tests/run_checks.py --cxx clang++-18 --std c++17 --sanitize --build-dir build/followup-clang-san
python tests/run_arm_checks.py --cxx <CubeIDE-g++> --build-dir build/followup-arm14
python tests/resources/run.py --cxx <MinGW-g++> --node <node> --build-dir build/followup-resources-host
python3 tests/resources/run.py --cxx clang++-18 --sanitize --build-dir build/followup-resources-san
python tests/resources/run.py --arm --cxx <CubeIDE-g++> --objdump <matching-objdump> --size <matching-size> --build-dir build/followup-resources-arm14
python tests/regression/h7s/run.py --cube <copied-scaffold> --arm-cxx <CubeIDE-g++> --output <new-build-directory> --run
```
