# Структура репозиторію та локальні artifacts

Поточний user guide розташований у [doc/user](user/README.md), повні виконувані
приклади — у [examples/user_guide](../examples/user_guide/README.md). Кожна
бібліотека має короткий власний README; тести мають [один індекс](../tests/README.md).
Review і transcripts зберігають оригінальні revision-specific твердження.

## Прибирання 2026-10-04

`build/` переміщено поза checkout, до локального архіву:

```text
C:/Users/admin/Documents/telemetry-artifacts/2026-10-04-cleanup-98b945f/
  generated-builds/
  retired-id-ranges/
  retired-qmake-targets/
  manifest.json
  README.md
```

Архів містить 99 712 звичайних файлів, 14 749 061 496 байтів (13,74 GiB).
Перевірено SHA-256 для 14 584 вибраних source/config/receipt файлів до й після
переносу. Це прибирання робочого дерева; воно **не звільнило** місце, зайняте
архівом. Чотири directory link nodes усередині candidate builds від'єднані;
їхні targets збережені, а початкові paths/targets записані в manifest.

Чотири непідключені fixtures `archive/id_ranges` належали старій моделі:
вони перенесені до `retired-id-ranges` та доступні в Git history. Python caches
і порожні папки, включно зі старими `resource/structured` і
`resource/telemetry/detail`, видалені. Tracked hardware receipts, vendor
sources/licenses, review transcripts і `.qtcreator` settings збережені.
Незакомічена правка `app/demo/DemoCatalog.h` та 92 файли
`tests/review-2026-09-26` перевірені за hashes і залишені без змін.

[Current image equivalence](../tests/resources/evidence/README.md) документує
перевірку архівованих images через явний `--path-map`. Старі receipts не
переписані й не перетворені на твердження про новий hardware run.

## Фінальний прохід

Окремо прибрано 14 старих `tests/telemetry_*check.pro`: усі вони посилалися на
вже видалені legacy `.cpp` і не використовувалися чинними runners. Перед
видаленням файли скопійовано до `retired-qmake-targets`, побайтово звірено й
записано SHA-256 у власний `manifest.json`. Активні qmake-проєкти
`tests/structured/` збережені.

Фінальний прохід також перевірив окреме включення всіх 50 публічних заголовків
і 132 локальні посилання в основному guide та README. Виправлено UTF-8 identifier
у reflection-пробі, приватність внутрішніх даних `FileEntry` та класифікацію
`const`-каталогів у явно заданих `Model`/`Descriptor`. ARM32 layout descriptor-а
залишився 16 байтів із вирівнюванням 4 та offsets 0/8/12; перевірки layout
збережені всередині friend-класу. JS runner тепер перевіряє повний звіт
`3057 checks / 0 failures / cppInterop=true`, включно з типами й унікальністю
полів. Нові перевірки звіту не додаються до числа interoperability checks.

Подальші generated outputs цього проходу зберігаються поза checkout.
Перезбірка дванадцяти історичних H7S images є offline compile/link-перевіркою
їхніх firmware bytes. Вона не означає новий запуск на платі й не змінює
оригінальні hardware receipts. Повний CI потрібний на SHA follow-up-коміту:
попередній CI `418a3fa` зупиняв release/ARM structured-перевірки через encoding
помилку в reflection-пробі.

## Завершення незакомічених матеріалів

Після окремого перегляду всіх 92 файлів `tests/review-2026-09-26` їх перенесено
до зовнішнього архіву:

```text
C:/Users/admin/Documents/telemetry-artifacts/2026-10-04-remaining-review-a199bd9/
  review-2026-09-26/
  original-DemoCatalog.h
  manifest.json
```

Це історичні репро для Scalar/C++17/старих JSON API та локальні порівняльні
скрипти з залежностями від старих extracted trees і конкретних tool paths.
Збережено 92 файли, 315 793 байти; SHA-256 кожного файла перевірено до й після
копіювання. Оригінальну незакомічену версію `DemoCatalog.h` також збережено.
Старі тексти й результати не переписано як докази поточної бібліотеки.

Троє reviewers звірили відповідні сценарії з активними ID, slot, lifetime,
null-check, weak-target та resource checks. Нового підтвердженого дефекту чи
необхідного унікального regression test у цих матеріалах не встановлено.
Архів залишається локальним; ці файли не підключені до поточного CI.

Корисну частину demo — read-only Field із вкладеною структурою — завершено
в `app/demo/DemoCatalog.h`: читабельні назви, явний `std::int32_t` та 32 canonical
wire bytes. Qt smoke перевіряє native/encoded значення, readonly статус і
відображення вкладених members. Рядки UI перевіряються за ID; додавання поля
не повинно ламати перевірку точного U64/S64 тексту через старі номери рядків.

## Наступні збірки

- Передавайте test runner окрему output папку через `--build-dir` / `--output`.
  Вона може бути поза checkout. Поточні результати цього проходу знаходяться
  у `C:/Users/admin/Documents/telemetry-validation/20261004-final`.
- Qt Creator після прибирання має заново виконати qmake для свого build
  directory; compilation database і generated UI headers відновляться.
  Глобальні editor settings та unsaved source buffers не змінювалися.
- Не видаляйте ignored directory тільки через те, що Git її ігнорує: там
  можуть бути єдині hardware artifacts, flash backups або helper scripts.
  Спочатку інвентар, потім збереження потрібного evidence.
- Directory links обробляйте як links; recursive cleanup не повинен заходити
  в їхні targets. Source, licenses, fixtures і historical review не є build
  outputs.
