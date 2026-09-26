# Закриття технічного review

Дата: 2026-09-26. Автори: Ruslan Kovtun (shpegun60), codexAi.
Початковий checkpoint: `23aca57147d893559c4212ad6311490cbb54d46e`.

Пізніші виправлення для overloaded слотів, явних шаблонних ID, `-Og` і ARM PIC
описані в [поточних regression checks](../../tests/regression/README.md). Таблиця
нижче зберігає рішення й виміри свого checkpoint; для поточного контракту
слотів див. [slot/README.md](../../lib/telemetry/slot/README.md).

Уточнення після `b19f1e9`: нові репро виявили регресію braced-owner binding
та додаткові lifetime випадки. Початкова оцінка нижче не означає, що захист
покрив усі форми виклику. Виправлення й докази — у
[LifetimeFollowup.md](LifetimeFollowup.md).

Опрацьовано шість технічних звітів із [reports.md](reports.md) та їхні
транскрипти `01`–`06`: підсумки, повідомлення про уточнення, відповідні репро
й результати інструментів. Зокрема враховано помилкові початкові висновки через
DLL/unused-variable diagnostics, виправлення некоректного default у parity
репро та потребу поділу sanitizer-збірки. Матеріали збережені без змін.
Архітектурні й продуктові пропозиції критика та його трьох помічників не входять
у цю реалізацію.

Перший прохід повідомляв про 17 виправлень і два обмеження. Повторний review
виявив неповне покриття та регресії; нижче уточнено відповідні пункти. Поточний
повний статус і нові докази наведено в [LifetimeFollowup.md](LifetimeFollowup.md).

| Знахідка | Рішення | Перевірка |
|---|---|---|
| NC-F1: окремі Clang no-honor flags | Уточнено межу: promoted warning не гарантує відмову під `-w`/system headers; fast-math callers не підтримуються | Звичайні diagnostic controls і позитивні IEEE тести; suppression cases описані у follow-up, не позначені як повністю виправлені |
| FL-F1: pointer/smart-pointer/refwrapper як owner | Прив’язка приймає справжній об’єкт, derived-об’єкт або явний OwnerSlot | 9 негативних pointer-owner репро, lvalue/dereference/derived controls |
| FL-F2: тимчасовий owner/closure через conversion | Тип фактичного аргументу виводиться незалежно від явно заданого типу | 18 lifetime cases та додаткові командні/slot/provider випадки |
| FL-F3: manual Field із native setter іншого типу | Follow-up узгодив checked conversion між pointer/NTTP/member/borrowed/slot setters; raw set не перевіряє Field limits | 138 checks, raw-policy та endpoint-rounding controls; actual thunk codegen перевіряється окремо |
| FL-F4: довільне поєднання FieldDefinition і Field | Конструктор доступний лише відповідному Access builder | Дві негативні конструкції, чинні фабрики й повна parity-матриця |
| FL-F5: Persistent для Null/unknown type | Потрібні обидві можливості доступу й підтримуваний числовий/Bool тип | Дві негативні constexpr декларації |
| FL-F6: різні inline attributes при O2/Os | Єдине оголошення Setter; рішення про inline залишено компілятору | Обидві ARM-збірки, читання disassembly, без зміни layout |
| CJ-F1: ABI anchor видалено linker GC | Follow-up утримує emitted reference; namespace `TELEMETRY_RETAIN_ABI()` окремо покриває compiler emission | Matching/mismatching GC/LTO links на host та ARM; header-only opt-in контракт збережено |
| CJ-F2: makeId звужує широкі компоненти | Перевірка до звуження; `tryMakeId` повертає optional, помилковий `makeId` порушує контракт | constexpr відмови, runtime abort, широкі ID на host та MCU |
| CJ-F3: fingerprint 0 неоднозначний | `trySchemaCrc` відокремлює nullopt від усіх u32, включно з нулем | Відомий каталог `!8pV5U` має успішний нульовий hash; invalid metadata відхиляється |
| CJ-F4: TU-local helper в inline template | JSON helpers мають inline linkage | Standalone headers, багатомодульні збірки й штатні serializer suites |
| CM-F1: view через const-reference helper | Документовано lifetime запозичення, збережено заборону прямих rvalue викликів | Штатні lifetime rejections; архівний репро пояснює обмеження |
| CM-F2: runtime ID/index звужується | Follow-up відділяє local enum від global integral ID; class/atomic wrappers потребують явного отримання числа початкової ширини | 122 checks, 104 відмови, ARM high-word gate з mutation control |
| CM-F3: null command name | Null у constexpr/runtime декларації відхиляється; literal 0/nullptr мають deleted overload | Негативний case 24 тепер перевіряє саме ім’я; runtime abort окремо |
| CM-F4: порожній commandArgs | Нормалізовано до NoCommandArgs | Виклик параметризованої команди й відсутність зайвого metadata storage |
| SL-F1: GCC no-delete-null-pointer-checks | Початковий identity workaround був неповним і приймав weak targets. Публічні NTTP знову перевіряють константну ненульову адресу | Slot-only control лишився; повна GCC no-delete/UBSan збірка не підтримується на перевірених компіляторах. Див. follow-up |
| SL-F2: generic auto копіює reference аргумент | Follow-up повернув звичайний `std::invoke` і перевіряє саме його overload semantics | 25/26 runtime checks, 15 точних відмов, cv/forwarding та valid-overload controls |
| RS-F1: LIST губить коректний prefix | Уже сформована сторінка повертається з Ok; помилка довгого path належить наступному запиту | Чотири розміри response, cursor/resume й in-place packets |
| RS-F2: custom metadata counts не збігаються | Follow-up додав sticky failure для traversal, який ігнорує sink false | 342 checks: count/order mismatch, callback failure, ignored stop, reserved rows |

Додатково відхилено ASCII DEL у path, виправлено причини provider compile-fail
тестів, закрито converting-provider lifetime loophole, збільшено буфер саме
ABI-тесту та перенесено runtime-abort перевірки у CI. Оновлено поточний ABI 8,
Command 20 B, кількість suites/probes й опис reserved-row dispatch. Історичні
вимірювання позначено як історичні; їхні числа не підмінено новими.

## Вимірювання першого проходу

Наведені далі числові результати належать checkpoint та першому проходу
`981e6be`/`b19f1e9`. Поточні зміни, додаткові перевірки й новий запуск плати
описані в [LifetimeFollowup.md](LifetimeFollowup.md).

Нові тести інтегровані в існуючі host, sanitized, ARM та resource CI jobs.
Повна карта репро й команд запуску: [tests/regression](../../tests/regression/README.md).
Матриця містить 77760 порівнянь native/dynamic field paths, 328 числових edge
перевірок, 52 нові compile-fail випадки з очікуваними причинами, 8 intentional
abort controls, 14 resource compile-fail випадків, JSON boundary та cursor tests.

Перший CI-прогін після виправлень виявив перевищення 180-секундного ліміту
компіляції parity-тесту з трьома типами. Його поділено на 18 збірок по одному
типу, по 4320 перевірок кожна. Сумарне покриття, timeout, оптимізацію й
sanitizer flags збережено; бібліотеку для цього не змінювали.

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
