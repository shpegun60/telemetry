// Critic trial: exposing an array (harmonic orders 1..63 of one phase) without an array type.
// The only route is one field per element, generated with a template getter and a name table.
#include "Telemetry.h"
#include "serialization/TelemetryJson.h"

#include <cstdio>
#include <utility>

using namespace telemetry;

constexpr std::size_t Orders = 63;
float spectrum[Orders];

template <std::size_t I>
float harmonic() noexcept
{
    return spectrum[I];
}

// Names must be stable C strings; build "H1".."H63" as constant storage.
struct Names {
    char text[Orders][4];
};
constexpr Names makeNames()
{
    Names n{};
    for (std::size_t i = 0; i < Orders; ++i) {
        const std::size_t order = i + 1;
        n.text[i][0] = 'H';
        if (order < 10) {
            n.text[i][1] = static_cast<char>('0' + order);
        } else {
            n.text[i][1] = static_cast<char>('0' + order / 10);
            n.text[i][2] = static_cast<char>('0' + order % 10);
        }
    }
    return n;
}
constexpr Names names = makeNames();

template <std::size_t... I>
constexpr auto makeHarmonics(std::index_sequence<I...>)
{
    return FieldTable{field<&harmonic<I>>(names.text[I], "%")...};
}
constexpr auto harmonicFields = makeHarmonics(std::make_index_sequence<Orders>{});
constexpr FieldCatalogTable catalog{group("harmonicsUa", harmonicFields)};

char json[8192];

int main()
{
    for (std::size_t i = 0; i < Orders; ++i) spectrum[i] = 100.0f / static_cast<float>(i + 1);
    const auto n = writeValues(catalog.index(), json, sizeof(json));
    std::printf("%u fields, values JSON %u bytes: %.120s...\n", static_cast<unsigned>(harmonicFields.size()),
                static_cast<unsigned>(n), json);
    const auto s = writeSchema(catalog.index(), json, sizeof(json));
    std::printf("schema JSON %u bytes for one 63-element array\n", static_cast<unsigned>(s));
    return n && s ? 0 : 1;
}
