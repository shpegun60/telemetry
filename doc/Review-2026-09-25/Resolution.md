# Закриття технічного review

Дата: 2026-09-26. Автори: Ruslan Kovtun (shpegun60), codexAi.
Початковий checkpoint: `23aca57147d893559c4212ad6311490cbb54d46e`.

Опрацьовано шість технічних звітів із [reports.md](reports.md) та їхні
транскрипти `01`–`06`: підсумки, повідомлення про уточнення, відповідні репро
й результати інструментів. Зокрема враховано помилкові початкові висновки через
DLL/unused-variable diagnostics, виправлення некоректного default у parity
репро та поділ sanitizer-збірки на шість груп. Матеріали збережені без змін.
Архітектурні й продуктові пропозиції критика та його трьох помічників не входять
у цю реалізацію.

17 знахідок виправлено в коді. Ще два пункти уточнюють межі запозичення й
лінкування: їх не подано як автоматично усунені можливості помилкового використання.

| Знахідка | Рішення | Перевірка |
|---|---|---|
| NC-F1: окремі Clang no-honor flags | Примусова діагностика constant evaluator; precise pragma не маскує небезпечні атрибути caller | Окремі flags при O1/O2, навіть із `-Wno-nan-infinity-disabled`; позитивні IEEE тести |
| FL-F1: pointer/smart-pointer/refwrapper як owner | Прив’язка приймає справжній об’єкт, derived-об’єкт або явний OwnerSlot | 9 негативних pointer-owner репро, lvalue/dereference/derived controls |
| FL-F2: тимчасовий owner/closure через conversion | Тип фактичного аргументу виводиться незалежно від явно заданого типу | 18 lifetime cases та додаткові командні/slot/provider випадки |
| FL-F3: manual Field із native setter іншого типу | declaredType нормалізує значення, після чого native setter робить перевірене перетворення до своєї сигнатури | F64→float, U16 із дробового входу, unsigned long; відмова без side effect |
| FL-F4: довільне поєднання FieldDefinition і Field | Конструктор доступний лише відповідному Access builder | Дві негативні конструкції, чинні фабрики й повна parity-матриця |
| FL-F5: Persistent для Null/unknown type | Потрібні обидві можливості доступу й підтримуваний числовий/Bool тип | Дві негативні constexpr декларації |
| FL-F6: різні inline attributes при O2/Os | Єдине оголошення Setter; рішення про inline залишено компілятору | Обидві ARM-збірки, читання disassembly, без зміни layout |
| CJ-F1: ABI anchor видалено linker GC | Уточнено вимогу живого посилання; автоматичний захист кожного header-only TU не обіцяється | Live mismatch не лінкується; discarded ELF reference демонструє межу гарантії |
| CJ-F2: makeId звужує широкі компоненти | Перевірка до звуження; `tryMakeId` повертає optional, помилковий `makeId` порушує контракт | constexpr відмови, runtime abort, широкі ID на host та MCU |
| CJ-F3: fingerprint 0 неоднозначний | `trySchemaCrc` відокремлює nullopt від усіх u32, включно з нулем | Відомий каталог `!8pV5U` має успішний нульовий hash; invalid metadata відхиляється |
| CJ-F4: TU-local helper в inline template | JSON helpers мають inline linkage | Standalone headers, багатомодульні збірки й штатні serializer suites |
| CM-F1: view через const-reference helper | Документовано lifetime запозичення, збережено заборону прямих rvalue викликів | Штатні lifetime rejections; архівний репро пояснює обмеження |
| CM-F2: runtime ID/index звужується | Ціле/enum значення перевіряється на початковій ширині | Host і реальний ARM32: `2^32` не виконує команду 0 |
| CM-F3: null command name | Null у constexpr/runtime декларації відхиляється; literal 0/nullptr мають deleted overload | Негативний case 24 тепер перевіряє саме ім’я; runtime abort окремо |
| CM-F4: порожній commandArgs | Нормалізовано до NoCommandArgs | Виклик параметризованої команди й відсутність зайвого metadata storage |
| SL-F1: GCC no-delete-null-pointer-checks | Перевірка NTTP через тотожність integral_constant, без порівняння адрес | GCC/Clang/ARM compile controls |
| SL-F2: generic auto копіює reference аргумент | Перевірено reference декларацію та викликається саме перевірена specialization/overload | auto&/auto&& positive; auto-by-value і throwing selected specialization negative |
| RS-F1: LIST губить коректний prefix | Уже сформована сторінка повертається з Ok; помилка довгого path належить наступному запиту | Чотири розміри response, cursor/resume й in-place packets |
| RS-F2: custom metadata counts не збігаються | Constructor звіряє кількість відвіданих entries; reserved command не може мати params | 110 перевірок: обидва напрями mismatch, callback failure, reserved rows |

Додатково відхилено ASCII DEL у path, виправлено причини provider compile-fail
тестів, закрито converting-provider lifetime loophole, збільшено буфер саме
ABI-тесту та перенесено runtime-abort перевірки у CI. Оновлено поточний ABI 8,
Command 20 B, кількість suites/probes й опис reserved-row dispatch. Історичні
вимірювання позначено як історичні; їхні числа не підмінено новими.

## Верифікація

Нові тести інтегровані в існуючі host, sanitized, ARM та resource CI jobs.
Повна карта репро й команд запуску: [tests/regression](../../tests/regression/README.md).
Матриця містить 77760 порівнянь native/dynamic field paths, 328 числових edge
перевірок, 52 нові compile-fail випадки з очікуваними причинами, 8 intentional
abort controls, 14 resource compile-fail випадків, JSON boundary та cursor tests.

На NUCLEO-H7S3L8, 600 МГц, CubeIDE GCC 14.3.1 пройшли **3328/3328** перевірок
при `-O2` і `-Os`. Перевірено кожний байт schema.bin, commands.bin і values.bin
**2.1** за незалежними goldens, маленькі chunks, getter counts, EOF і фактичні
64-бітні перетворення libgcc. Прошивку відновлено з повного 64-КіБ backup;
повторне зчитування має той самий SHA-256. Це correctness run, не новий cycle benchmark.

ARM layout лишився Field **96 B / alignment 32**, Getter/Setter **8 B**,
Command **20 B**, Catalog **12 B**. Усі штатні codegen gates проходять, зокрема
прямий/local/global typed виклик і enum positions. Порівняння з checkpoint
дає 23/32 повністю однакові потоки коду probe-об’єктів; решта містять зміни
setter dispatch, callback adapters або розміщення коду. Не весь машинний код
ідентичний. Вибрані wrappers: 62/66 normalized streams однакові, зокрема
native local/global field paths. Відомий inferred write при `-Os` став
**162 → 50 B**; read wrappers лишилися 12/4/2376 B при O2 і 12/4/2206 B при Os.
Ці розміри не доводять пропорційного прискорення в циклах.

Межі доказів: MSVC/libc++ не запускалися; довільні compiler flags поза
документованим IEEE контрактом не підтримуються. Клієнт відповідає за lifetime
borrowed objects, незмінність metadata, синхронізацію slots і узгоджений ABI
усіх модулів. Цей review закриває перелічені знахідки, а не доводить відсутність
будь-якої можливої помилки в усіх майбутніх застосуваннях.
