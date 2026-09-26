// Review probe (catalog-json): serialize floats under numeric locales whose
// decimal separator is not '.', including multi-byte separators, and with
// LC_NUMERIC and LC_CTYPE set independently. Prints each outcome.
#include "serialization/TelemetryJson.h"
#include <clocale>
#include <cstdio>
#include <cstring>
#include <string>

using namespace telemetry;

static void show(const char* category, const char* name)
{
    char text[512];
    const Field fields[] = {
        {"f", "", numericType<float>(1.25f, 0.5f, 2.5f), []() noexcept { return 1.5f; }},
        {"d", "", numericType<double>(-2.25, -3.5, 0.0), []() noexcept { return -1234567.125; }},
    };
    const Catalog catalogs[] = {{"v", fields}};
    const char* decimal = std::localeconv()->decimal_point;
    std::string hex;
    for (const char* p = decimal; *p; ++p) {
        char b[4]; std::snprintf(b, sizeof b, "%02x", static_cast<unsigned char>(*p)); hex += b;
    }
    const auto n = writeValues(catalogs, 1, text, sizeof text);
    char raw[64];
    std::snprintf(raw, sizeof raw, "%.9g", 1.5);
    std::printf("%-8s %-24s decimal=[%s] raw=[%s] values(%zu)=%s\n", category, name, hex.c_str(), raw, n, text);
    const auto s = writeSchema(catalogs, 1, text, sizeof text);
    const char* bounds = std::strstr(text, "\"min\"");
    std::printf("         schema(%zu) %.*s\n", s, bounds ? 60 : 0, bounds ? bounds : "");
}

int main()
{
    const char* names[] = {"German_Germany.1252", "de-DE", "de_DE.UTF-8", "fa-IR", "fa-IR.UTF-8",
                           "ps-AF", "ps-AF.UTF-8", "ar-SA.UTF-8", "Persian_Iran.1256", "bn-IN.UTF-8"};
    for (const char* name : names) {
        if (std::setlocale(LC_NUMERIC, name) != nullptr) show("NUMERIC", name);
        std::setlocale(LC_ALL, "C");
        if (std::setlocale(LC_ALL, name) != nullptr) show("ALL", name);
        std::setlocale(LC_ALL, "C");
    }
    return 0;
}
