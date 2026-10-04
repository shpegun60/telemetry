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
