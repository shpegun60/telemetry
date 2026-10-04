# Повні приклади посібника

Програми використовують поточний C++20 API без Qt, мережі та приладу. Вони
містять готові declarations і перевіряють результати викликів.

| Програма | Що показує | Compiled sources |
| --- | --- | --- |
| [QuickStart.cpp](QuickStart.cpp) | Мінімальна mixed Model з головної сторінки | `StructuredAbi.cpp`, `Adapter.cpp` |
| [Native.cpp](Native.cpp) | Field/Command/Service; типи, всі slots, owning/borrowed outputs, typed та erased traversal | `StructuredAbi.cpp`, `Adapter.cpp` |
| [Resources.cpp](Resources.cpp) | Flat filesystem, lazy FileView, custom R/W provider, порційний LIST/STAT/READ/WRITE | `Protocol.cpp` |
| [Encoded.cpp](Encoded.cpp) | Власний complete-frame adapter без файлів, business validation, великі/borrowed responses | `StructuredAbi.cpp`, `Adapter.cpp` |

```sh
python tests/docs/run.py --cxx g++ --build-dir /tmp/telemetry-doc-examples
```

Команда виконується з кореня репозиторію. Вкажіть встановлений compiler через
`--cxx`; на Windows це може бути повний шлях до Qt MinGW `g++.exe`.
Приклади не вимикають assertions. Для ARM `--arm` збирає їх без запуску на
приладі; це перевірка compiler/linker, а не апаратний вимір.

`Encoded.cpp` використовує простий **прикладний** envelope, щоб показати
зв'язок operation/ID/payload із library adapter. Цей envelope не оголошує
нового wire protocol бібліотеки. UART/TCP framing, таймаути, повтори й
синхронізація залишаються в транспорті застосунку.

Читайте [Native API](../../doc/user/NativeApi.md) і
[Transport and Resources](../../doc/user/TransportAndResources.md) паралельно
з цими програмами. Команди й CI integration описані в
[documentation checks](../../tests/docs/README.md).

Наступний крок після односторінкових програм —
[device integration](../device_integration/README.md). Він розносить business
class, metadata/facade та receive loop по різних файлах і показує приймання
split/coalesced frames для telemetry та files. Він теж перевіряється docs runner.
