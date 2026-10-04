# Від байтів UART/TCP до значення або файла

Автори: Ruslan Kovtun (shpegun60), codexAi. MIT.

Цей посібник проходить повний шлях: клієнт готує запит, транспорт збирає
його з порцій байтів, застосунок викликає потрібний handler і передає відповідь
назад. Оголошення Device та Model дивіться в [Native API](NativeApi.md).
Точні контракти окремих викликів зібрані в
[TransportAndResources.md](TransportAndResources.md).

Можна почати з одного з двох маршрутів:

| Що передає клієнт | Куди застосунок передає повний запит |
| --- | --- |
| Операцію над Field, Command або Service, packed ID та canonical payload | `telemetry::readFieldEncoded`, `writeFieldEncoded`, `executeCommandEncoded`, `callServiceEncoded` |
| LIST, STAT, READ або WRITE для файла | `resource::protocol::process` |

Перший маршрут достатній для читання налаштування, його зміни чи виклику
методу. Другий додає іменовані файли: готові байти, власне сховище,
descriptor Model або поточні Field values. Обидва handlers синхронні;
прийом, відправлення, стан підключення та порції UART/TCP організує застосунок.

## 1. Спочатку визначте межі повідомлення

Один виклик UART callback або TCP `recv()` повертає **chunk**, тобто порцію
байтів. Chunk може містити половину запиту, кінець одного й початок іншого
або кілька повних запитів. Його розмір не є довжиною packet.

```text
байти від UART/TCP
    -> збирач повного frame у застосунку
    -> один complete packet
    -> encoded Model handler або resource::protocol::process
    -> байти відповіді
    -> frame відповіді
    -> передача UART/TCP до завершення
```

Робочий application приклад із фіксованими buffers та окремим Api facade —
[examples/device_integration](../../examples/device_integration/README.md).
Його framing визначений у самому прикладі. Це код застосунку, який можна
адаптувати до свого callback; готовий UART/TCP driver бібліотека не створює.

Для цього runnable прикладу frame має точний вигляд:

```text
u16 LE bodyBytes | body[bodyBytes]
```

`bodyBytes` не включає двобайтовий prefix і має бути в діапазоні 1..128.
Перший byte body вибирає маршрут:

| Route | Request body після route byte | Reply body після route byte |
| --- | --- | --- |
| Direct=1 | `u8 operation, u32 LE packedId, canonical payload` | `u8 dispatch, u8 endpointStatus, response payload` |
| Resource=2 | Один generic LIST/STAT/READ/WRITE packet | Один generic resource reply packet |

Невідома route або неповний direct header у **вже повному** body одержує
application error `[route=0, InvalidPacket=1]`: frame `02 00 00 01`.
Це окремий error envelope прикладу, а не telemetry dispatch або resource status.

Відповідь також має `u16 LE bodyBytes` prefix. Route та цей prefix —
**application contract прикладу**, а canonical payload і resource packet
мають свої library contracts. Наприклад, framed LIST із cursor 0:

```text
0A 00 | 02 | 01 00 00 00 00 00 00 00 00
length   route   LIST + u64 cursor
```

Для читання Period із `FieldId::Period=0` у тому самому project frame такий:

```text
06 00 | 01 | 01 | 00 00 00 00
length   route   Read   packed ID
```

Коли поточний Period дорівнює 10, його повна framed відповідь:

```text
07 00 | 01 | 00 | 00 | 0A 00 00 00
length   route   dispatch   endpointStatus   u32 Period
```

Нуль endpointStatus для Field read тут є правилом application envelope;
encoded Field read повертає тільки dispatch та written.

Receiver зберігає неповний frame у `std::array`, приймає chunks через
`app::api::StreamReceiver::feed` і викликає sink лише для повної framed
відповіді. Sink синхронно споживає або копіює цю відповідь: його borrowed
span не можна зберегти для майбутнього UART DMA, бо receiver повторно
використає buffer після callback. Для асинхронної передачі sink копіює bytes
у свій TX slot, який живе до завершення send.

`app::api::onCompletePacket` приймає вже повний body **без** length prefix.
`StreamReceiver::feed` приймає raw chunks **із** prefix. Не знімайте prefix
двічі й не передавайте незавершений chunk прямо в facade. Перевіряйте
`ReceiveStatus`, а sink не повинен рекурсивно викликати `feed()` на тому
самому receiver.

Збирач має виконувати такі кроки:

1. Зберегти неповний prefix між викликами прийому.
2. Прочитати оголошену довжину та перевірити її до копіювання body.
3. Накопичити рівно стільки байтів у buffer з фіксованою capacity.
4. Передати один повний body у handler. У prefix/body не повинно лишитися
   невикористаного хвоста buffer.
5. Почати наступний prefix і продовжити обробку залишку того самого chunk.

Наприклад, прийшли тільки prefix та перші три байти body: handler ще не
викликається. Наступний chunk завершує body: handler викликається один раз.
Якщо після нього в тому самому chunk уже є другий frame, збирач обробляє його
окремо. Збережений стан прийому належить конкретному підключенню.

Оголошена довжина більша за capacity є помилкою framing. Застосунок скидає
незавершений frame і відновлює відому межу потоку: для TCP це може бути нове
підключення; для UART потрібне визначене ним правило відновлення межі.
Просто прийняти наступний байт як новий prefix недостатньо: він може бути
частиною відкинутого body. Таймаут неповного frame також обробляє транспорт.

У `StreamReceiver` нульова або більша за 128 довжина переводить receiver
у failed стан. Подальші chunks відхиляються до `reset()` на новій відомій
межі потоку. `reset()` після disconnect також прибирає старий неповний frame;
receiver не шукає довільно наступний можливий prefix.

Помилка **вмісту вже повного packet** обробляється окремо. Наприклад, невідома
resource операція або `final=2` дає `InvalidData`; межа наступного frame
залишається відомою, тому нове підключення для цієї помилки не потрібне.

## 2. Маршрут без файлів: читання та зміна Field

Відкрийте [Encoded.cpp](../../examples/user_guide/Encoded.cpp). Там є Device,
Model, Workspace, побайтове кодування ID та повний `onCompletePacket`.
Нижче bytes показані для його окремої Model з Config, Increment та Report.
Власний header цього прикладу має вигляд:

```text
u8 operation | u32 LE packedId | canonical payload
Read=1         Write=2           Command=3        Service=4
```

Цей header належить **прикладу**. Telemetry визначає canonical payload та
encoded API, а header, status envelope і framing вибирає застосунок.
Не змішуйте ці коди з resource LIST/STAT/READ/WRITE.

У прикладі Field `Config` має тип:

```cpp
struct Config {
    std::uint32_t limit;
    bool enabled;
};
```

Його payload має п'ять байтів: u32 LE та один bool byte. C++ padding у payload
не потрапляє. Packed ID `telemetry::makeId<0, 0>()` дорівнює нулю: catalog 0,
entry 0 у родині Fields. Command 0 та Service 0 мають окремі адресні простори,
тому операція потрібна для вибору таблиці.

Для читання Config клієнт готує повний body `01 00 00 00 00`. Handler
перевіряє header, відхиляє зайвий payload для Read і викликає:

```cpp
namespace ts = telemetry;

// view, workspace та buffers належать застосунку й уже живі.
const auto result = ts::readFieldEncoded(view, id, output, workspace);
if (result.dispatch == ts::DispatchStatus::Ok) {
    const auto payload = output.first(result.written);
    // Закодувати status envelope і передати саме цей payload.
    (void)payload;
}
```

Config `{400, true}` повертається canonical bytes `90 01 00 00 01`.
Клієнт декодує їх через [`telemetry::decode`](NativeApi.md#codec-окремо-від-endpoints)
із caller-owned Lease або відповідний клієнтський codec у `Config`.
Він перевіряє dispatch status та точну довжину перед decode.

Щоб установити `{900, false}`, клієнт кодує payload `84 03 00 00 00` і надсилає
body `02 00 00 00 00 84 03 00 00 00`. Handler передає тільки payload:

```cpp
const auto result = ts::writeFieldEncoded(view, id, input, workspace);
if (result.dispatch == ts::DispatchStatus::Ok) {
    const auto setterResult = result.endpointStatus;
    // Applied, InvalidValue, Busy, ReadOnly тощо — результат endpoint.
    (void)setterResult;
}
```

У `Encoded.cpp` setter дозволяє `limit` від 1 до 10000. Тому `{0, false}`
проходить wire decode, викликає setter і повертає
`dispatch=Ok, endpointStatus=InvalidValue`. Заміна bool byte на `02` дає
`dispatch=InvalidPayload` під час decode і setter не викликає. Політику
діапазону реалізує setter; descriptor цього діапазону не містить.

## 3. Command та Service через той самий transport

Після виділення operation/ID/payload handler викликає відповідний API:

```cpp
const auto command = ts::executeCommandEncoded(view, id, input, workspace);
const auto service = ts::callServiceEncoded(view, id, input, output, workspace);
```

Це два окремі приклади виклику, для різних operation. Один packet викликає
лише свій handler.

У `Encoded.cpp` Command `Increment` з ID 0 приймає `{uint32_t amount;}`.
Body `03 00 00 00 00 07 00 00 00` збільшує counter на 7. Повторне надсилання
того самого body збільшує його ще на 7. Command `Reset` має ID 1 та порожній
request: після його п'ятибайтового header payload немає.

Service `CopyReport` з ID 0 приймає `Query{true}` як byte `01` і повертає
64 canonical bytes Report. `ViewReport` з ID 1 повертає той самий формат,
кодуючи живий borrowed об'єкт. Клієнт одержує status окремо від response
payload. За `ServiceStatus::Busy` або іншої service відмови `written=0`:
слід показати відмову, а не декодувати старий вміст output.

| Результат handler | Що перевірити перш ніж вважати операцію успішною |
| --- | --- |
| Field read | `dispatch==Ok`, потім використати рівно `written` bytes |
| Field write | `dispatch==Ok`, потім `endpointStatus==WriteResult::Applied` |
| Command | `dispatch==Ok`, потім відповідний `CommandResult`; `Accepted` означає прийняття owner у його чергу |
| Service | `dispatch==Ok`, потім `endpointStatus==ServiceStatus::Ok`, потім payload `[0, written)` |

`NotFound`, `InvalidPayload`, `BufferTooSmall`, `WorkspaceTooSmall`,
`InternalError` та dispatch `Unavailable` описують routing/codec/storage
виклику. Endpoint status має зміст лише за dispatch `Ok`. Наприклад,
відсутній target дає dispatch `Unavailable`; callback, який сам відмовив,
може повернути endpoint `Unavailable` після успішного dispatch.

Output capacity перевіряється для **повного можливого успішного response**
до callback. 63 bytes недостатньо для 64-byte Report, навіть якщо Device
зараз Busy. Додатковий byte у input також не дозволений: canonical request
має точну довжину. No-request Command/Service одержує порожній input.

## 4. Buffers і Workspace тримає застосунок

Для одного serialized виклику достатній bound scratch можна взяти з Model:

```cpp
inline std::array<std::byte, model.maxScratch()> scratch{};
inline telemetry::Workspace workspace{scratch};
```

Це місце для decoded C++ об'єктів та великих owning results. Воно відокремлене
від receive buffer, packet output і buffer, який читає UART DMA/socket send.
`model.maxScratch()` не задає packet capacity; request/response розміри
визначайте окремо за власним header і `telemetry::wireSize<T>`.

Звичайна схема для одного підключення:

```text
RX array       збирає повний frame
TX array       містить закодовану відповідь до завершення її передачі
scratch array  позичений Workspace тільки на час encoded handler
```

Handler закінчився — його operation spans більше не використовуються
бібліотекою. Проте transport send може ще читати TX array. Зберігайте цей
array живим і не перезаписуйте його до завершення передачі всіх bytes.
TCP `send()` також може прийняти тільки prefix: транспорт повторює передачу
непереданого suffix. Якщо наступний request уже готовий, потрібен вільний
TX slot, обмежена черга або затримка його обробки.

UART receive callback часто позичає DMA buffer, який скоро заповнять знову.
Збирач копіює потрібні bytes у свій RX array або закінчує їх обробку до такого
повторного використання. Він не зберігає span callback для пізнішої роботи.
Результати бібліотеки також не є wire structs: status header кодується явно,
інакше C++ layout/padding випадково стануть частиною application protocol.

Один Workspace не можна використовувати одночасно двома handlers.
Серіалізуйте доступ або дайте незалежним активним handlers окремі arrays та
Workspaces. Власний Device також потребує відповідної синхронізації.
Borrowed response живе та лишається стабільним, поки encoder читає його.
Request/output/scratch не розміщуйте поверх цього об'єкта. Об'єкти Model,
catalogs, bindings та paths також повинні залишатися живими за своїми
позиченими посиланнями; копія view не подовжує їх життя.

## 5. Маршрут із файлами: задайте path і provider

Повний приклад тільки resource API —
[Resources.cpp](../../examples/user_guide/Resources.cpp). У ньому є
read-only version, custom read/write settings та JSON bytes:

```cpp
#include <resource/Resource.hpp>

inline constexpr std::array versionBytes{
    std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0}};
inline constexpr resource::BytesFile versionFile{versionBytes};

inline constinit const auto files = resource::filesystem(
    resource::file("/device/version.bin", versionFile));
```

Це вже таблиця з одним файлом, index 0. Додайте наступний
`resource::file("/settings.bin", settingsProvider)` у цей самий
`filesystem(...)`, і він одержить index 1. `init()`, реєстрації каталогів та
пошуку через рядковий path тут немає. `/device/version.bin` є flat іменем;
storage і bytes визначає provider. Розширення `.json` також тільки ім'я:
provider сам готує JSON, а HTTP adapter за потреби задає Content-Type.

Таблиця володіє descriptors; provider, текст path та byte storage залишаються
у застосунку. String literal — простий стабільний path. Provider і його дані
не повинні зникнути чи переміститися під час доступу. `BytesFile` позичає
масив, має `size()` та `read()`, тому зареєстрований файл має тільки читання.
Для зміни storage потрібен власний provider із `write()`.

Для локального перегляду можна обійти таблицю без читання самих файлів:

```cpp
for (const auto file : files) {
    // index/path/capabilities читають descriptor, без provider callbacks.
    const auto index = file.index();
    const auto path = file.path();
    const bool canWrite = file.writable();
    (void)index;
    (void)path;
    (void)canWrite;
}

const auto selected = files.view()[0];
const auto info = selected.stat(); // Тільки тут викликається provider.size().
```

`files.size()` — кількість файлів. `info.size` — byte size конкретного файла.
`files.view()` та FileView позичають таблицю. Iteration не викликає
`size/read/write`; зокрема не звертається до SD/storage або telemetry getter.
`writable()` перевіряє наявність callback, а поточну готовність запису
визначить сам callback. Невірний index дає invalid FileView та `InvalidFile`
для операцій.

## 6. Обробіть один повний resource packet

Після framing layer передайте handler тільки generic request body:

```cpp
#include <resource/protocol/Protocol.hpp>

const auto reply = resource::protocol::process(
    files.view(), completeRequest, responseBuffer);
const resource::Input encodedReply{responseBuffer.data(), reply.written};
// Додати application frame і передати encodedReply.
(void)encodedReply;
```

`process` уже закодував response packet у buffer. `Reply{status, written}`
є локальним результатом для transport; його C++ bytes не передаються.
За `written=0` response packet для відправлення немає. Зокрема,
`BufferTooSmall, 0` означає, що envelope не помістився; provider не викликано.

Це точний **generic resource wire contract** із
[protocol README](../../lib/resource/protocol/README.md). Зовнішній application
frame, наведений вище, не входить до жодного рядка:

| Op | Request body | Response body |
| --- | --- | --- |
| LIST=1 | `u8 op, u64 cursor` | `u8 status, u64 next, u8 eof, u16 dataSize, data[]` |
| STAT=2 | `u8 op, u32 index` | `u8 status, u32 size, u8 flags` |
| READ=3 | `u8 op, u32 index, u64 cursor` | `u8 status, u64 next, u8 eof, u16 dataSize, data[]` |
| WRITE=4 | `u8 op, u32 index, u64 cursor, u8 final, u16 dataSize, data[]` | `u8 status, u64 next, u32 consumed, u8 complete` |

Числа little-endian; bool bytes мають бути 0/1. Paths/data не мають кінцевого
NUL, а packet не може мати зайвого хвоста. LIST request займає 9 bytes,
STAT — 5, READ — 13, WRITE — `16 + dataSize`. LIST/READ response header
займає 12 bytes, STAT — 6, WRITE — 14.

Resource status codes: `Ok=0`, `InvalidFile=1`, `NotReadable=2`,
`NotWritable=3`, `InvalidCursor=4`, `CursorExpired=5`, `BufferTooSmall=6`,
`InvalidData=7`, `InternalError=8`. STAT flags: `Readable=1`, `Writable=2`.
Custom read/write settings має flags 3; `BytesFile` має flags 1.

Malformed packet, unknown op або final byte 2 можуть одержати тільки один
status byte `InvalidData`. Клієнт спочатку перевіряє довжину response та
status, потім розбирає envelope очікуваної операції. Provider відмова для
правильно сформованого запиту має звичайний envelope цієї операції.

## 7. Як клієнт знаходить файл і читає його порціями

Клієнт хоче `/device/version.bin`. Він не надсилає цей рядок у READ:

1. Надсилає LIST із `cursor=0`: дев'ять bytes `01 00 00 00 00 00 00 00 00`.
2. За `status=Ok` розбирає `data[]` як повторення
   `[u16 pathLength][path bytes]`. Перший запис має index, який дорівнює
   **cursor цього LIST request**; кожен наступний збільшує index на один.
3. Зберігає index потрібного path. Якщо `eof=0`, продовжує LIST із поверненим
   `next`, доки знайде файл або одержить EOF. Список можна читати сторінками.
4. Надсилає STAT із знайденим index. Перевіряє `Ok`, `Readable` та size.
5. Надсилає READ із цим index та provider `cursor=0`.
6. За `Ok` додає тільки `dataSize` bytes до отриманого файла. Наступний READ
   використовує **повернений `next`**, а завершення визначається `eof=1`.

LIST cursor — index файла. READ/WRITE cursor — u64 стан, який тлумачить
provider. Це різні значення, хоча обидва передаються як u64. Універсальний
клієнт не робить `cursor += dataSize` і не порівнює його із STAT size: для
власного provider це може бути token стану. Перевірка progress для
незавершеного успішного READ: є data або змінився cursor. Без progress
потрібно завершити операцію з помилкою, а не повторювати нескінченно.

Для `BytesFile` cursor є byte offset. Наведені чотири version bytes із
двобайтовим output читаються так:

| Request cursor | Data | Returned next | EOF |
| --- | --- | --- | --- |
| 0 | `01 00` | 2 | false |
| 2 | `00 00` | 4 | true |
| 4 | порожні | 4 | true |

Cursor 5 дає `InvalidCursor`. Порожній output до EOF дає `BufferTooSmall`.
Для такої двобайтової порції generic READ потрібен response buffer
щонайменше `12 + 2` bytes. READ request не містить бажаної кількості bytes;
server задає її через доступну response capacity.

LIST повертає тільки цілі paths. Response має вмістити header та хоча б
один потрібний запис `[u16 length][path]`. `BufferTooSmall` із незмінним
cursor означає потребу в більшій response capacity на server. Protocol
payload обмежений u16: до 65535 bytes, один LIST path — до 65533 bytes.
Докладне правило для довшого локального path є в protocol README.

Зміна порядку `file(...)` змінює індекси. Після зміни firmware/table клієнт
повторює LIST. Filename index не можна використовувати як telemetry packed
ID: файловий WRITE вибирає provider, encoded Field write вибирає setter.

## 8. Custom settings provider та частковий WRITE

Для власного storage достатньо звичайного класу з точними сигнатурами:

```cpp
class SettingsFile {
public:
    resource::FileSize size() const noexcept;
    resource::ReadResult read(resource::Cursor cursor,
                              resource::Output output) const noexcept;
    resource::WriteResult write(resource::Cursor cursor,
                                resource::Input input, bool final) noexcept;
};
```

`size()` обов'язковий; хоча б `read()` або `write()` має існувати.
Provider сам визначає RAM/Flash/SD, формат, перевірку даних, синхронізацію та
збереження після reset. `ReadResult` має status, next, written, eof;
`WriteResult` — status, next, consumed, complete. Обидва будуються як
`{status, cursor, count, finished}`; їхня внутрішня C++ layout не є packet.

У [Resources.cpp](../../examples/user_guide/Resources.cpp) SettingsFile має
8 RAM bytes і за один write споживає максимум два. Клієнт уже знайшов
`/settings.bin` через LIST і передає три bytes `0A 14 1E` як фінальний input:

| Запит | Відповідь | Наступна дія клієнта |
| --- | --- | --- |
| `cursor=0, data=0A 14 1E, final=1` | `Ok, next=2, consumed=2, complete=0` | Залишити тільки suffix `1E` |
| `cursor=2, data=1E, final=1` | `Ok, next=3, consumed=1, complete=1` | Завершити transfer |

`final=true` означає, що **цей input містить кінець submitted stream**.
Якщо provider прийняв тільки prefix, нез'їдений suffix також містить кінець,
тому клієнт повторно передає його з тим самим `final=true`, новим `next` і
новим `dataSize`. Він не повторює вже consumed bytes. `complete=true` —
окреме підтвердження provider, що transfer завершений; воно не випливає
автоматично з final.

Якщо дані передаються кількома chunks, проміжні input мають `final=false`,
останній — `true`. Empty final input може завершити transfer, якщо provider
це підтримує. Для SettingsFile з прикладу `write(3, {}, true)` повертає
`Ok, consumed=0, complete=true`; empty nonfinal input повертає
`BufferTooSmall`. READ/WRITE spans дійсні тільки протягом синхронного callback.
Provider їх не зберігає для відкладеної роботи.

Клієнт перевіряє статус, `consumed <= submittedDataSize` та progress на
кожній відповіді. Provider може просувати cursor без consumed bytes, але
`Ok, complete=false, consumed=0, next=oldCursor` не є прогресом.
Generic protocol перевіряє суперечливі results і повертає `InternalError`.
На error provider повертає початковий cursor, нуль bytes та false completion;
власні side effects і їх відновлення визначає provider. Ядро не додає
transaction або відновлення попередніх settings. Якщо read callback встиг
записати щось у output перед відмовою, ці bytes не підтверджені й клієнт
відкидає їх.

## 9. Descriptor та Values — також read-only файли

До тієї самої таблиці можна додати providers поточної Model:

```cpp
#include <resource/telemetry/v3/DescriptorFile.hpp>
#include <resource/telemetry/v3/ValuesFile.hpp>

namespace rs = resource::telemetry::v3;

inline constexpr rs::Descriptor descriptor{model};
inline constexpr auto descriptorBytes = rs::packDescriptor<descriptor>();
inline constexpr rs::DescriptorFile descriptorFile{descriptorBytes};
inline constexpr rs::ValuesFile valuesFile{descriptor, workspace};

inline constinit const auto telemetryFiles = resource::filesystem(
    resource::file("/telemetry/descriptor.bin", descriptorFile),
    resource::file("/telemetry/values.bin", valuesFile));
```

Клієнт знаходить обидва paths через LIST, читає descriptor повністю та
розбирає його structural types, catalogs, endpoint positions і names.
Після цього читає Values. Його 24-byte `TVL3` header містить fingerprint
descriptor; клієнт перевіряє його **перед** декодуванням values за завантаженою
схемою. Приклад незалежного JS reader —
[structured_client](../../examples/structured_client/README.md).

Descriptor cursor є byte offset і допускає поділ запису між READ chunks.
Values читає цілі tokens `u8 status + canonical Field payload`. Поставте
READ payload capacity щонайменше `valuesFile.maxTokenSize()` для гарантованого
progress на кожному Field; generic response потребує ще 12 bytes header.
Values `next` передається назад без змін. Довільний offset усередині Field
payload дає `InvalidCursor`. Token понад 65535 bytes не поміститься в generic
protocol READ; повторення запиту цього не виправить.

Values викликає getter, тільки коли його token поміщається. Це живі значення,
і chunks можуть прочитати різні Fields у різні моменти. Спільний snapshot
усієї Model або freeze конфігурації має забезпечити застосунок. Unavailable
Field має свій token status та zero payload; клієнт не декодує ці zeros як
реальне значення. Повний формат і read-scratch bound — у
[v3 provider README](../../lib/resource/telemetry/v3/README.md).

`DescriptorFile` та `ValuesFile` не мають `write()`, тому WRITE до них дає
`NotWritable`. Зміна Config виконується через encoded Field write або через
**власний** SettingsFile, якщо застосунок явно реалізував його перетворення
bytes у конфігурацію. Шлях `/telemetry/values.bin` сам не створює setter.

## 10. Коли додати Bind/Exchange та як поводитися після timeout

Якщо клієнт і Device повинні погодити схему перед control викликами,
є окремий [structured_protocol приклад](../../examples/structured_protocol/README.md).
У ньому transport тримає `Binding` і Workspace на підключення. Bind узгоджує
version/fingerprint один раз для поточної сесії; Exchange далі передає
operation, packed ID, canonical payload та requestId для зіставлення відповіді.

```cpp
// modelView має власне стабільне ім'я й живе довше binding.
inline constexpr auto modelView = model.view();
example::structured_protocol::Binding binding;

const auto agreement = example::structured_protocol::Bind::process(
    binding, modelView, descriptor.fingerprint(), completeBind, bindOutput);
const auto reply = example::structured_protocol::Exchange::process(
    binding, completeExchange, exchangeOutput, workspace);
```

Ці виклики показують дві послідовні операції: Exchange дозволений після
успішного Bind. Native та прямий encoded API, generic files і resource
protocol самі сесії не створюють і Bind не вимагають. На disconnect або зміні
Model transport завершує активну роботу, очищає старі frames/queues та
викликає `binding.reset()`. Temporary `model.view()` у Bind передавати не
можна: Binding позичає названий ModelView.

Bind/Exchange не придушує повторних викликів. Generic WRITE та encoded
Command також викликають provider/callback повторно. Timeout означає, що
клієнт не отримав підтвердження: попередній виклик міг уже змінити Device.
Для Increment це могло б додати 7 ще раз; для файлового WRITE повторне
надсилання має наслідки, визначені provider. Політику повторів і перевірку
фактичного результату задавайте відповідно до своєї операції.

У Exchange requestId є correlation, тому його не перевикористовують, поки
пізня відповідь може надійти для старого запиту. Він не є deduplication key.
Приклади перевіряються на host без UART/TCP і приладу; команди запуску
та межі цього підтвердження — у
[user_guide README](../../examples/user_guide/README.md) та
[device_integration README](../../examples/device_integration/README.md).
