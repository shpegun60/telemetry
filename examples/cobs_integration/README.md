# Telemetry та файли через COBS

Автори: Ruslan Kovtun (shpegun60), codexAi. [MIT](../../LICENSE).

Повна host програма використовує **справжній**
[`cobs::Endpoint`](https://github.com/shpegun60/cobs), fixed `wire::Pool`,
CRC16 та наявний [device facade](../device_integration/README.md).
Fake transport лише передає bytes між двома endpoints; він не замінює
COBS encoder, decoder, CRC, storage або telemetry codec.

| Файл | Для чого |
| --- | --- |
| [main.cpp](main.cpp) | Два endpoints, sender/busy callbacks, порції RX, Direct та Resource запити, ownership перевірки |
| [CMakeLists.txt](CMakeLists.txt) | Збірка з явно заданим COBS checkout; залежність не додається до telemetry core |
| [Api.cpp](../device_integration/Api.cpp) | Device, Model, файли та обробник одного complete application body |
| [Device.cpp](../device_integration/Device.cpp) | Прикладні правила й зміна стану |
| [Транспортний посібник](../../doc/user/COBSIntegration.md) | Покрокове підключення до своєї програми та UART |

## Зібрати окремо

Перевірювана upstream ревізія:
[`2e0abf260848fcb74e4a57b37532188047759643`](https://github.com/shpegun60/cobs/commit/2e0abf260848fcb74e4a57b37532188047759643).
COBS береться як окремий checkout. Його вихідні файли не копіюються до
`lib/telemetry`, а репозиторій COBS не змінюється.

```sh
git clone https://github.com/shpegun60/cobs.git /path/to/cobs
git -C /path/to/cobs checkout 2e0abf260848fcb74e4a57b37532188047759643
cmake -S examples/cobs_integration -B /path/to/output/cobs \
  -DCOBS_ROOT=/path/to/cobs -DCMAKE_BUILD_TYPE=Release
cmake --build /path/to/output/cobs
ctest --test-dir /path/to/output/cobs --output-on-failure
```

Для MinGW оберіть свій installed compiler і CMake generator. Qt та
підключена плата не потрібні. `COBS_ROOT` — шлях до репозиторію, де є `src/`,
а не шлях до `src/cobs/`. Приклад використовує pinned `tiny_delegate` з
telemetry checkout; для нього окремі COBS submodules не потрібні.

Runner з compiler/input/image hashes і логами:

```sh
python tests/cobs/run.py --cobs-root /path/to/cobs \
  --cxx g++ --build-dir /path/to/output/cobs-checks
python tests/cobs/run.py --cobs-root /path/to/cobs \
  --cxx clang++-18 --sanitize --build-dir /path/to/output/cobs-san
```

ARM `--arm --cxx /path/to/arm-none-eabi-g++` компілює/лінкує O2/Os/Og,
не запускає firmware та не відкриває ST-LINK/COM. Host transport також не
є підтвердженням UART DMA або Ethernet на апаратурі.

## Які байти передаються

```text
COBS(length_le | application body | CRC16(body)) | 00

application body:
  route=1 | operation | u32 LE packed ID | canonical native payload
  route=2 | generic resource packet
```

COBS уже задає frame boundary. Додатковий `u16 length` із
`device_integration::StreamReceiver` тут **не використовується**.
`Packet::data()` передається безпосередньо в `app::api::onCompletePacket()`.
Reply body так само проходить через COBS builder та endpoint.

Приклад вибрав `cobs::Format<crc::Crc16Bitwise, 128>`: корисний application
body до 128 bytes, CRC trailer 2 bytes та length 1 byte. Peers погоджують
однакові Format/CRC; автоматичного визначення конфігурації немає.
Resource READ response включає application route і свій 12-byte header,
тому тут має щонайбільше 115 bytes data. Для великих telemetry values
збільшіть Format та application buffers згідно зі своїми wire/scratch bounds.

## Перевірені сценарії

368 виконаних умов перевіряють:

- Direct Read/Write одного Field, підтвердження Applied та InvalidValue;
- Command без Request та Service з Request/Response;
- неправильний bool byte: dispatch InvalidPayload, setter не викликається;
- LIST/STAT/READ та partial WRITE із повторним надсиланням лише suffix;
- COBS frames із нульовими bytes, порції RX по три bytes;
- прийнятий TX borrow, повернення блока через `poll()` та збереження Message на Busy;
- physical gap, відкидання неповного frame до delimiter та наступний повний frame.
- пошкоджений payload byte при незмінних COBS code/length/CRC/delimiter:
  `crc_errors` збільшується, packet не з'являється, application callbacks не
  викликаються, наступний правильний запит одразу працює;
- echo усіх 128 application bytes в обидва боки, включно з нулями, та
  повернення TX/RX блоків після Busy і завершення borrow; спроба 129 bytes
  відхиляється без зміни вже побудованого payload.

Публічні операції й статуси описані в
[TransportWalkthrough](../../doc/user/TransportWalkthrough.md).
COBS не додає telemetry ID, sessions, прикладних limits або автоматичних повторів.
