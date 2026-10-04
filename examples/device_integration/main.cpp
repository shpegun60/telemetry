// A host feeder and checked client. It uses only the application's runtime API.
// Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
#include "Api.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
using namespace app;
namespace api = app::api;
using Bytes = std::vector<std::byte>;
unsigned checks = 0;
void check(bool condition) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "Device integration condition %u failed\n", checks);
        std::abort();
    }
}
template <class U>
void put(Bytes& bytes, std::size_t offset, U value) {
    for (std::size_t i = 0; i < sizeof(U); ++i)
        bytes[offset + i] = static_cast<std::byte>(value >> (8 * i));
}
template <class U>
U get(api::Input bytes, std::size_t offset) {
    U value = 0;
    for (std::size_t i = 0; i < sizeof(U); ++i)
        value |= U{std::to_integer<unsigned char>(bytes[offset + i])} << (8 * i);
    return value;
}
template <class Id>
Bytes direct(api::Operation operation, Id id, api::Input payload = {}) {
    Bytes bytes(6 + payload.size());
    bytes[0] = static_cast<std::byte>(api::Route::Direct);
    bytes[1] = static_cast<std::byte>(operation);
    put(bytes, 2, static_cast<std::uint32_t>(id));
    std::copy(payload.begin(), payload.end(), bytes.begin() + 6);
    return bytes;
}
Bytes u32(std::uint32_t value) { Bytes bytes(4); put(bytes, 0, value); return bytes; }
Bytes u16(std::uint16_t value) { Bytes bytes(2); put(bytes, 0, value); return bytes; }
Bytes gainsPayload(const Gains& gains) {
    Bytes bytes(6);
    for (std::size_t i = 0; i < gains.size(); ++i) put(bytes, i * 2, gains[i]);
    return bytes;
}
Bytes configuration(const Configure& request) {
    Bytes bytes(13);
    put(bytes, 0, request.sampling.periodMs);
    bytes[4] = static_cast<std::byte>(request.sampling.mode);
    for (std::size_t i = 0; i < request.sampling.gains.size(); ++i)
        put(bytes, 5 + i * 2, request.sampling.gains[i]);
    bytes[11] = static_cast<std::byte>(request.display.brightness);
    bytes[12] = static_cast<std::byte>(request.display.enabled);
    return bytes;
}
Bytes resourceStat(std::uint32_t index) {
    Bytes bytes(6);
    bytes[0] = static_cast<std::byte>(api::Route::Resource);
    bytes[1] = std::byte{2}; // resource STAT
    put(bytes, 2, index);
    return bytes;
}
Bytes resourceRead(std::uint32_t index, std::uint64_t cursor = 0) {
    Bytes bytes(14);
    bytes[0] = static_cast<std::byte>(api::Route::Resource);
    bytes[1] = std::byte{3}; // resource READ
    put(bytes, 2, index);
    put(bytes, 6, cursor);
    return bytes;
}
Bytes resourceWrite(std::uint64_t cursor, api::Input data) {
    Bytes bytes(17 + data.size());
    bytes[0] = static_cast<std::byte>(api::Route::Resource);
    bytes[1] = std::byte{4}; // resource WRITE, discovered index 1
    put(bytes, 2, std::uint32_t{1});
    put(bytes, 6, cursor);
    bytes[14] = std::byte{1};
    put(bytes, 15, static_cast<std::uint16_t>(data.size()));
    std::copy(data.begin(), data.end(), bytes.begin() + 17);
    return bytes;
}
Bytes invoke(api::Input body) {
    std::array<std::byte, api::MaxBodyBytes> output{};
    const auto reply = api::onCompletePacket(body, output);
    check(reply.status == api::PacketStatus::Replied && reply.written <= output.size());
    return Bytes(output.begin(), output.begin() + reply.written);
}
void directStatus(api::Input reply, api::Dispatch dispatch, std::uint8_t endpoint = 0) {
    check(reply.size() >= 3 && reply[0] == static_cast<std::byte>(api::Route::Direct));
    check(reply[1] == static_cast<std::byte>(dispatch) && reply[2] == static_cast<std::byte>(endpoint));
}
Bytes framed(api::Input body) {
    Bytes bytes(body.size() + 2);
    put(bytes, 0, static_cast<std::uint16_t>(body.size()));
    std::copy(body.begin(), body.end(), bytes.begin() + 2);
    return bytes;
}
struct Captured {
    std::vector<Bytes> frames;
    static void accept(void* context, api::Input frame) noexcept {
        // The host harness copies the borrowed span before the callback ends.
        static_cast<Captured*>(context)->frames.emplace_back(frame.begin(), frame.end());
    }
};
} // namespace

int main() {
    using api::Operation;
    using api::FieldId;
    using api::CommandId;
    using api::ServiceId;
    using api::Dispatch;

    // Native application calls cross readAs/writeAs inside Api.cpp.
    std::uint32_t period = 0;
    Mode mode{};
    Gains gains{};
    DisplayConfig display{};
    check(api::readPeriod(period) && period == 10);
    check(api::readMode(mode) && mode == Mode::Measuring);
    check(api::readGains(gains) && gains == Gains{100, 100, 100});
    check(api::readDisplay(display) && display.brightness == 50 && display.enabled);
    check(api::writePeriod(42) == WriteStatus::Applied);
    check(api::writePeriod(0) == WriteStatus::InvalidValue);
    check(api::readPeriod(period) && period == 42);
    check(api::writeMode(Mode::Off) == WriteStatus::Applied);
    check(api::writeGains({111, 222, 333}) == WriteStatus::Applied);
    check(api::writeGains({0, 222, 333}) == WriteStatus::InvalidValue);
    check(api::writeDisplay({75, false}) == WriteStatus::Applied);
    check(api::writeDisplay({101, true}) == WriteStatus::InvalidValue);

    auto reply = invoke(direct(Operation::Read, FieldId::Period));
    directStatus(reply, Dispatch::Ok);
    check(reply.size() == 7 && get<std::uint32_t>(reply, 3) == 42);
    reply = invoke(direct(Operation::Read, FieldId::Mode));
    directStatus(reply, Dispatch::Ok);
    check(reply.size() == 4 && reply[3] == std::byte{0});
    reply = invoke(direct(Operation::Read, FieldId::Gains));
    directStatus(reply, Dispatch::Ok);
    check(reply.size() == 9 && get<std::uint16_t>(reply, 3) == 111 &&
          get<std::uint16_t>(reply, 5) == 222 && get<std::uint16_t>(reply, 7) == 333);
    reply = invoke(direct(Operation::Read, FieldId::Display));
    directStatus(reply, Dispatch::Ok);
    check(reply == Bytes{std::byte{1}, std::byte{0}, std::byte{0}, std::byte{75}, std::byte{0}});

    const Bytes enabled{std::byte{60}, std::byte{1}};
    reply = invoke(direct(Operation::Write, FieldId::Display, enabled));
    directStatus(reply, Dispatch::Ok, static_cast<std::uint8_t>(WriteStatus::Applied));
    check(api::readDisplay(display) && display.brightness == 60 && display.enabled);
    reply = invoke(direct(Operation::Write, FieldId::Gains, gainsPayload({12, 34, 56})));
    directStatus(reply, Dispatch::Ok);
    check(api::readGains(gains) && gains == Gains{12, 34, 56});
    reply = invoke(direct(Operation::Write, FieldId::Mode, Bytes{std::byte{1}}));
    directStatus(reply, Dispatch::Ok);
    check(api::readMode(mode) && mode == Mode::Measuring);

    // Codec refusal precedes the callback; business refusal follows it.
    auto before = api::diagnostics();
    reply = invoke(direct(Operation::Write, FieldId::Display, Bytes{std::byte{60}, std::byte{2}}));
    directStatus(reply, Dispatch::InvalidPayload);
    check(api::diagnostics().device.writes == before.device.writes);
    reply = invoke(direct(Operation::Write, FieldId::Mode, Bytes{std::byte{2}}));
    // Enum codes decode as their underlying integer. Domain validation is ours.
    directStatus(reply, Dispatch::Ok, static_cast<std::uint8_t>(WriteStatus::InvalidValue));
    check(api::diagnostics().device.writes == before.device.writes + 1);
    reply = invoke(direct(Operation::Write, FieldId::Period, u32(0)));
    directStatus(reply, Dispatch::Ok, static_cast<std::uint8_t>(WriteStatus::InvalidValue));
    check(api::diagnostics().device.writes == before.device.writes + 2);
    check(api::readPeriod(period) && period == 42);

    before = api::diagnostics();
    reply = invoke(direct(Operation::Read, std::uint32_t{0xffffffff}));
    directStatus(reply, Dispatch::NotFound);
    reply = invoke(direct(Operation::Read, FieldId::Period, Bytes{std::byte{0}}));
    directStatus(reply, Dispatch::InvalidPayload);
    check(api::diagnostics().device.reads == before.device.reads);
    std::array<std::byte, 6> shortResponse{}; // 3-byte header leaves only 3 bytes for u32.
    auto packetReply = api::onCompletePacket(direct(Operation::Read, FieldId::Period), shortResponse);
    check(packetReply.written == 3 && shortResponse[1] == static_cast<std::byte>(Dispatch::BufferTooSmall));
    check(api::diagnostics().device.reads == before.device.reads);

    // One aggregate contains every argument. Both configs are applied together.
    const Configure next{{75, Mode::Measuring, {200, 300, 400}}, {80, false}};
    reply = invoke(direct(Operation::Command, CommandId::Configure, configuration(next)));
    directStatus(reply, Dispatch::Ok, static_cast<std::uint8_t>(CommandStatus::Executed));
    check(api::readPeriod(period) && period == 75);
    check(api::readGains(gains) && gains == next.sampling.gains);
    check(api::readDisplay(display) && display.brightness == 80 && !display.enabled);
    auto invalid = next;
    invalid.display.brightness = 101;
    invalid.sampling.periodMs = 90;
    reply = invoke(direct(Operation::Command, CommandId::Configure, configuration(invalid)));
    directStatus(reply, Dispatch::Ok, static_cast<std::uint8_t>(CommandStatus::InvalidValue));
    check(api::readPeriod(period) && period == 75); // No partly applied config.
    before = api::diagnostics();
    auto truncated = configuration(next);
    truncated.pop_back();
    reply = invoke(direct(Operation::Command, CommandId::Configure, truncated));
    directStatus(reply, Dispatch::InvalidPayload);
    check(api::diagnostics().device.commands == before.device.commands);

    reply = invoke(direct(Operation::Service, ServiceId::Sample)); // Zero arguments.
    directStatus(reply, Dispatch::Ok);
    check(reply.size() == 11 && get<std::uint16_t>(reply, 7) == 200 && reply[10] == std::byte{0});
    reply = invoke(direct(Operation::Service, ServiceId::Query, u16(1)));
    directStatus(reply, Dispatch::Ok, static_cast<std::uint8_t>(api::ServiceStatus::Ok));
    check(reply.size() == 11 && get<std::uint16_t>(reply, 7) == 300);
    reply = invoke(direct(Operation::Service, ServiceId::Query, u16(3)));
    directStatus(reply, Dispatch::Ok, static_cast<std::uint8_t>(api::ServiceStatus::InvalidArgument));
    check(reply.size() == 3);
    before = api::diagnostics();
    const auto reset = direct(Operation::Command, CommandId::Reset);
    directStatus(invoke(reset), Dispatch::Ok);
    directStatus(invoke(reset), Dispatch::Ok); // Retrying executes again: no deduplication.
    check(api::diagnostics().device.commands == before.device.commands + 2);
    check(api::readPeriod(period) && period == 10);

    // LIST discovers the two flat labels and their positional file indices.
    Bytes list(10);
    list[0] = static_cast<std::byte>(api::Route::Resource);
    list[1] = std::byte{1}; // LIST, cursor zero
    before = api::diagnostics();
    reply = invoke(list);
    check(reply[0] == std::byte{2} && reply[1] == std::byte{0});
    check(get<std::uint64_t>(reply, 2) == 2 && reply[10] == std::byte{1});
    check(get<std::uint16_t>(reply, 11) == reply.size() - 13);
    const std::array paths{"/device/version.bin", "/device/note.bin"};
    std::size_t offset = 13;
    for (const char* path : paths) {
        const auto size = get<std::uint16_t>(reply, offset);
        check(size == std::strlen(path) && std::memcmp(reply.data() + offset + 2, path, size) == 0);
        offset += 2 + size;
    }
    check(offset == reply.size() && api::diagnostics().fileReads == before.fileReads &&
          api::diagnostics().fileWrites == before.fileWrites);
    reply = invoke(resourceStat(0));
    check(reply.size() == 7 && get<std::uint32_t>(reply, 2) == 4 && reply[6] == std::byte{1});
    reply = invoke(resourceStat(1));
    check(reply.size() == 7 && get<std::uint32_t>(reply, 2) == 16 && reply[6] == std::byte{3});
    reply = invoke(resourceRead(0));
    check(reply.size() == 17 && get<std::uint64_t>(reply, 2) == 4 && reply[10] == std::byte{1});
    check(get<std::uint16_t>(reply, 11) == 4 && get<std::uint32_t>(reply, 13) == 1);

    // The provider consumes at most two bytes. Resubmit only the suffix with next.
    const Bytes note{std::byte{10}, std::byte{20}, std::byte{30}};
    reply = invoke(resourceWrite(0, note));
    check(reply.size() == 15 && reply[1] == std::byte{0} && get<std::uint64_t>(reply, 2) == 2);
    check(get<std::uint32_t>(reply, 10) == 2 && reply[14] == std::byte{0});
    reply = invoke(resourceWrite(2, api::Input{note}.subspan(2)));
    check(get<std::uint64_t>(reply, 2) == 3 && get<std::uint32_t>(reply, 10) == 1 && reply[14] == std::byte{1});
    reply = invoke(resourceRead(1));
    check(reply.size() == 29 && std::equal(note.begin(), note.end(), reply.begin() + 13));
    check(get<std::uint64_t>(reply, 2) == 16 && reply[10] == std::byte{1});
    before = api::diagnostics();
    auto badFinal = resourceWrite(3, note);
    badFinal[14] = std::byte{2};
    reply = invoke(badFinal);
    check(reply == Bytes{std::byte{2}, std::byte{7}}); // InvalidData, no provider call.
    check(api::diagnostics().fileWrites == before.fileWrites);
    std::array<std::byte, 14> shortWriteReply{};
    packetReply = api::onCompletePacket(resourceWrite(3, note), shortWriteReply);
    check(packetReply.status == api::PacketStatus::BufferTooSmall && packetReply.written == 0);
    check(api::diagnostics().fileWrites == before.fileWrites);
    reply = invoke(resourceRead(1, 17));
    check(reply.size() == 13 && reply[1] == std::byte{4} && get<std::uint64_t>(reply, 2) == 17);

    // Real receiver state: split prefix, split payload and two concatenated frames.
    api::StreamReceiver receiver;
    Captured captured;
    const auto fieldFrame = framed(direct(Operation::Write, FieldId::Period, u32(88)));
    const Bytes tail{std::byte{0xaa}, std::byte{0xbb}};
    const auto fileFrame = framed(resourceWrite(3, tail));
    Bytes combined = fieldFrame;
    combined.insert(combined.end(), fileFrame.begin(), fileFrame.end());
    before = api::diagnostics();
    check(receiver.feed(api::Input{combined}.first(1), &Captured::accept, &captured) == api::ReceiveStatus::Ok);
    check(captured.frames.empty() && api::diagnostics().device.writes == before.device.writes);
    check(receiver.feed(api::Input{combined}.subspan(1, 3), &Captured::accept, &captured) == api::ReceiveStatus::Ok);
    check(captured.frames.empty() && api::diagnostics().fileWrites == before.fileWrites);
    check(receiver.feed(api::Input{combined}.subspan(4, fieldFrame.size() - 4 + 2),
                        &Captured::accept, &captured) == api::ReceiveStatus::Ok);
    check(captured.frames.size() == 1 && receiver.completedFrames() == 1);
    check(api::diagnostics().device.writes == before.device.writes + 1 && api::diagnostics().fileWrites == before.fileWrites);
    check(receiver.feed(api::Input{combined}.subspan(fieldFrame.size() + 2),
                        &Captured::accept, &captured) == api::ReceiveStatus::Ok);
    check(captured.frames.size() == 2 && receiver.completedFrames() == 2);
    check(captured.frames[0] == Bytes{std::byte{3}, std::byte{0}, std::byte{1}, std::byte{0}, std::byte{0}});
    check(captured.frames[1].size() == 17 && get<std::uint16_t>(captured.frames[1], 0) == 15);
    check(captured.frames[1][2] == std::byte{2} && captured.frames[1][3] == std::byte{0});
    check(get<std::uint64_t>(captured.frames[1], 4) == 5 &&
          get<std::uint32_t>(captured.frames[1], 12) == 2 && captured.frames[1][16] == std::byte{1});
    check(api::readPeriod(period) && period == 88);

    const auto readFrame = framed(direct(Operation::Read, FieldId::Period));
    combined = readFrame;
    combined.insert(combined.end(), readFrame.begin(), readFrame.end());
    check(receiver.feed(combined, &Captured::accept, &captured) == api::ReceiveStatus::Ok);
    check(receiver.completedFrames() == 4 && captured.frames.size() == 4);
    check(captured.frames[2] == captured.frames[3] && get<std::uint16_t>(captured.frames[2], 0) == 7);
    check(get<std::uint32_t>(captured.frames[2], 5) == 88);

    // Feed every individual byte; no service executes until the last byte arrives.
    const auto queryFrame = framed(direct(Operation::Service, ServiceId::Query, u16(0)));
    before = api::diagnostics();
    for (std::size_t i = 0; i < queryFrame.size(); ++i) {
        check(receiver.feed(api::Input{queryFrame}.subspan(i, 1), &Captured::accept, &captured) == api::ReceiveStatus::Ok);
        check(api::diagnostics().device.services == before.device.services + (i + 1 == queryFrame.size() ? 1u : 0u));
    }
    check(receiver.completedFrames() == 5);

    // An invalid length fails the stream. Remaining bytes are never rescanned.
    before = api::diagnostics();
    const Bytes zeroLength{std::byte{0}, std::byte{0}};
    check(receiver.feed(zeroLength, &Captured::accept, &captured) == api::ReceiveStatus::InvalidLength);
    check(receiver.failed());
    check(receiver.feed(fieldFrame, &Captured::accept, &captured) == api::ReceiveStatus::InvalidLength);
    check(api::diagnostics().device.writes == before.device.writes);
    receiver.reset();
    const Bytes tooLong{std::byte{129}, std::byte{0}};
    check(receiver.feed(tooLong, &Captured::accept, &captured) == api::ReceiveStatus::InvalidLength);
    check(receiver.failed());
    receiver.reset();
    check(receiver.feed(readFrame, &Captured::accept, &captured) == api::ReceiveStatus::Ok && !receiver.failed());
    check(receiver.completedFrames() == 6);
    // A timeout/new stream drops an unfinished frame, with no business callback.
    check(receiver.feed(api::Input{fieldFrame}.first(fieldFrame.size() - 1), &Captured::accept, &captured) == api::ReceiveStatus::Ok);
    receiver.reset();
    check(receiver.feed(readFrame, &Captured::accept, &captured) == api::ReceiveStatus::Ok);
    check(api::diagnostics().device.writes == before.device.writes && receiver.completedFrames() == 7);
    check(receiver.feed(readFrame, nullptr, nullptr) == api::ReceiveStatus::InvalidSink);
    check(receiver.feed({}, &Captured::accept, &captured) == api::ReceiveStatus::Ok);
    check(receiver.completedFrames() == 7);

    std::array<std::byte, api::MaxBodyBytes> output{};
    packetReply = api::onCompletePacket(Bytes{std::byte{99}}, output);
    check(packetReply.status == api::PacketStatus::InvalidPacket && packetReply.written == 2);
    check(output[0] == std::byte{0} && output[1] == std::byte{1});
    auto unknownOperation = direct(Operation::Read, FieldId::Period);
    unknownOperation[1] = std::byte{99};
    directStatus(invoke(unknownOperation), Dispatch::InvalidPayload);

    std::printf("Device integration guide: %u checks passed\n", checks);
}
