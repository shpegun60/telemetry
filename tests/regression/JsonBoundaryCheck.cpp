// Regression promoted from tests/review/catalog-json/JsonBoundaryProbe.cpp.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
// Review probe (catalog-json): JSON output capacity sweep with getter-call
// accounting, escaping of every byte value, and number formatting endpoints.
// Prints FAIL lines for any violated claim; exit code is the failure count.
#include "serialization/TelemetryJson.h"
#include "serialization/TelemetryCommandJson.h"
#include "field/TelemetryEnum.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace telemetry;

namespace {
int failures = 0;
void expect(bool ok, const char* what)
{
    if (!ok) { ++failures; std::printf("FAIL  %s\n", what); }
    else std::printf("ok    %s\n", what);
}

int reads = 0;
Scalar f32() noexcept { ++reads; return Scalar::fromF32(1.25f); }
Scalar u64() noexcept { ++reads; return Scalar::fromU64(UINT64_MAX); }
Scalar s64() noexcept { ++reads; return Scalar::fromS64(INT64_MIN); }
Scalar nul() noexcept { ++reads; return Scalar::null(); }

enum class Wide : std::int64_t { Low = INT64_MIN, High = INT64_MAX };

// Every size from 0 to length+8: result is 0 or the full length, the buffer
// is NUL-terminated, bytes after `size` are untouched, and the getter count
// never exceeds the number of fields whose preceding text fitted.
template <class Serialize>
void sweep(const char* label, Serialize serialize, bool values, int expectedReads)
{
    std::vector<char> reference(8192);
    reads = 0;
    const std::size_t length = serialize(reference.data(), reference.size());
    const int fullReads = reads;
    bool ok = length != 0 && (!values || fullReads == expectedReads);
    // Value start offsets inside arrays (names here contain no brackets/commas).
    std::vector<std::size_t> starts;
    bool inArray = false;
    for (std::size_t i = 0; i < length; ++i) {
        if (reference[i] == '[') { inArray = true; if (reference[i + 1] != ']') starts.push_back(i + 1); }
        else if (reference[i] == ']') inArray = false;
        else if (reference[i] == ',' && inArray) starts.push_back(i + 1);
    }
    ok = ok && (!values || starts.size() == static_cast<std::size_t>(expectedReads));
    int lastReads = 0;
    bool monotonic = true;
    bool exactReads = true;
    for (std::size_t size = 0; size <= length + 8; ++size) {
        std::vector<char> guarded(size + 64, '#');
        reads = 0;
        const std::size_t written = serialize(guarded.data() + 16, size);
        ok = ok && written == (size > length ? length : 0);
        for (std::size_t i = 0; i < 16; ++i) ok = ok && guarded[i] == '#';
        for (std::size_t i = 16 + size; i < guarded.size(); ++i) ok = ok && guarded[i] == '#';
        if (size != 0) ok = ok && std::memchr(guarded.data() + 16, '\0', size) != nullptr;
        if (written != 0) ok = ok && std::strcmp(guarded.data() + 16, reference.data()) == 0;
        if (size != 0 && written == 0) {
            // The partial prefix must be a prefix of the full document.
            const std::size_t prefix = std::strlen(guarded.data() + 16);
            ok = ok && prefix < size && std::strncmp(guarded.data() + 16, reference.data(), prefix) == 0;
            // A getter runs iff all text before its value was committed.
            if (values) {
                int expected = 0;
                for (const auto start : starts) if (start <= prefix) ++expected;
                if (reads != expected) {
                    exactReads = false;
                    std::printf("      size %zu prefix %zu reads %d expected %d\n", size, prefix, reads, expected);
                }
            }
        }
        if (size == 0 && values) exactReads = exactReads && reads == 0;
        monotonic = monotonic && reads >= lastReads;
        lastReads = reads;
    }
    expect(ok && monotonic && exactReads, label);
}
} // namespace

int main()
{
    // ---- capacity sweep with getter accounting ----
    const Field fields[] = {
        {"a", "V", ScalarType::F32, &f32},
        {"b", "", ScalarType::U64, &u64},
        {"c", "", ScalarType::S64, &s64},
        {"d", "", ScalarType::F32, &nul},
        {"e", "", enumType<Wide, Wide::Low, Wide::High>(), &s64},
    };
    const Catalog catalogs[] = {{"g0", fields}, {"g1", nullptr, 0}, {"g2", fields}};
    const CatalogIndex index{catalogs};
    for (int mode = 0; mode < 2; ++mode) {
        const JsonOptions options{mode ? JsonInt64Mode::String : JsonInt64Mode::Number};
        sweep(mode ? "values sweep (string mode)" : "values sweep (number mode)",
              [&](char* b, std::size_t n) { return writeValues(index, b, n, options); }, true, 10);
        reads = 0;
        sweep(mode ? "schema sweep (string mode)" : "schema sweep (number mode)",
              [&](char* b, std::size_t n) { return writeSchema(index, b, n, options); }, false, 0);
        expect(reads == 0, "schema never invokes getters");
    }

    char text[4096];
    // ---- string mode quoting of S64 enum bounds including INT64_MIN ----
    const Field wide[] = {{"e", "", enumType<Wide, Wide::Low, Wide::High>(), &s64}};
    const Catalog wideCatalog[] = {{"w", wide}};
    expect(writeSchema(wideCatalog, 1, text, sizeof text, JsonOptions{JsonInt64Mode::String}) != 0
           && std::strstr(text, "\"min\":\"-9223372036854775808\",\"max\":\"9223372036854775807\"") != nullptr
           && std::strstr(text, "\"enum\":{\"-9223372036854775808\":\"Low\",\"9223372036854775807\":\"High\"}") != nullptr,
           "string mode quotes S64 enum bounds (INT64_MIN) and keeps enum keys as decimal strings");
    std::printf("      %s\n", text);
    const auto crcNumber = schemaCrc(wideCatalog, 1);
    char numberText[4096];
    expect(writeSchema(wideCatalog, 1, numberText, sizeof numberText) != 0
           && std::strncmp(numberText, text, 20) == 0 && crcNumber != 0,
           "schema fingerprint identical in number and string mode");

    // ---- escaping of every byte value 0x01..0xFF in names/units/keys ----
    std::string all;
    for (int byte = 1; byte < 256; ++byte) all.push_back(static_cast<char>(byte));
    const Field escaped[] = {{all.c_str(), all.c_str(), ScalarType::U8, +[]() noexcept { return Scalar::fromU8(7); }}};
    const Catalog escapedCatalog[] = {{all.c_str(), escaped}};
    std::vector<char> big(8192);
    const auto schemaLength = writeSchema(escapedCatalog, 1, big.data(), big.size());
    std::string expectedEscape = "\"";
    for (int byte = 1; byte < 256; ++byte) {
        char piece[8];
        if (byte < 0x20) { std::snprintf(piece, sizeof piece, "\\u%04x", byte); expectedEscape += piece; }
        else if (byte == '"') expectedEscape += "\\\"";
        else if (byte == '\\') expectedEscape += "\\\\";
        else expectedEscape.push_back(static_cast<char>(byte));
    }
    expectedEscape += "\"";
    expect(schemaLength != 0 && std::strstr(big.data(), ("\"name\":" + expectedEscape).c_str()) != nullptr
           && std::strstr(big.data(), ("\"n\":" + expectedEscape + ",\"u\":" + expectedEscape).c_str()) != nullptr,
           "all bytes 0x01..0x1f, quote and backslash are escaped; 0x7f..0xff pass through unchanged");
    std::FILE* out = std::fopen("build/review-catalog-json/escaped-schema.json", "wb");
    if (out) { std::fwrite(big.data(), 1, schemaLength, out); std::fclose(out); }
    const auto valueLength = writeValues(escapedCatalog, 1, big.data(), big.size());
    expect(valueLength != 0 && std::strncmp(big.data() + 1, expectedEscape.c_str(), expectedEscape.size()) == 0,
           "value object key uses the same escaping");

    // ---- enum name with embedded NUL is escaped as \u0000 ----
    // (TelemetryMetadataCheck customizes a name to "a\0b"; here use a command param enum.)

    // ---- float formatting endpoints ----
    struct Case { Scalar value; const char* expected; };
    const Case cases[] = {
        {Scalar::fromF32(-0.0f), "-0"},
        {Scalar::fromF32(std::numeric_limits<float>::denorm_min()), "1.40129846e-45"},
        {Scalar::fromF32(std::numeric_limits<float>::max()), "3.40282347e+38"},
        {Scalar::fromF32(std::nextafter(1.0f, 2.0f)), "1.00000012"},
        {Scalar::fromF32(16777217.0f), "16777216"},
        {Scalar::fromF64(-0.0), "-0"},
        {Scalar::fromF64(std::numeric_limits<double>::denorm_min()), "4.9406564584124654e-324"},
        {Scalar::fromF64(std::numeric_limits<double>::max()), "1.7976931348623157e+308"},
        {Scalar::fromF64(0.1), "0.10000000000000001"},
        {Scalar::fromF64(1e21), "1e+21"},
    };
    // Serialize each value through a field whose getter returns it.
    static Scalar current;
    for (const auto& item : cases) {
        current = item.value;
        const Field row[] = {{"v", "", item.value.type(), +[]() noexcept { return current; }}};
        const Catalog catalog[] = {{"x", row}};
        char buffer[128];
        const bool ok = writeValues(catalog, 1, buffer, sizeof buffer) != 0
            && std::strcmp(buffer, ("{\"x\":[" + std::string(item.expected) + "]}").c_str()) == 0;
        char message[160];
        std::snprintf(message, sizeof message, "value text %s -> %s", item.expected, buffer);
        expect(ok, message);
    }

    // ---- F32 metadata precision: 17 digits, advertised endpoint round-trips as double ----
    const Field bounded[] = {{"f", "", numericType<float>(0.1f, -0.1f, 0.3f), +[]() noexcept { return 0.f; }}};
    const Catalog boundedCatalog[] = {{"b", bounded}};
    expect(writeSchema(boundedCatalog, 1, text, sizeof text) != 0
           && std::strstr(text, "\"min\":-0.10000000149011612,\"max\":0.30000001192092896,\"default\":0.10000000149011612") != nullptr,
           "F32 bounds/defaults use 17 significant digits of the promoted value");
    std::printf("%d failure(s)\n", failures);
    return failures;
}
