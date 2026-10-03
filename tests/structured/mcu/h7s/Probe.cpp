/* MCU-only owning-return comparison. Authors: Ruslan Kovtun (shpegun60),
 * codexAi. SPDX-License-Identifier: MIT. */
#include "../Fixture.hpp"

namespace mcu::h7s {
#ifndef MCU_SCALE
__attribute__((noinline)) std::uint32_t nativeBig(std::uint32_t id) noexcept
{
    // This deliberately returns an owning optional<Big>. Its storage belongs
    // on the caller stack and is compared with the existing encoded big_read.
    const auto value = fixture::fields.readAs<fixture::Big>((id & 1u ? 0x20000u : 0u) | 8u);
    return value ? 4096u + value->words.front() + value->words.back() : 0u;
}
extern const Operation nativeOperation{"native_big", nativeBig, 7, 128};
unsigned valuesBytes() noexcept
{
    constexpr qualification::rs::Descriptor descriptor{fixture::model};
    constexpr qualification::rs::ValuesFile values{descriptor, qualification::sharedWorkspace};
    return values.size();
}

void checkExtra(unsigned& checked, unsigned& failed) noexcept
{
    const auto check = [&](bool condition) noexcept { ++checked; if (!condition) ++failed; };
    const auto value = fixture::fields.readAs<fixture::Big>(8u);
    bool allWords = value.has_value();
    if (value) for (unsigned i = 0; i < value->words.size(); ++i)
        allWords = allWords && value->words[i] == (i < 3 ? i + 1 : 0);
    check(allWords);
    check(nativeBig(0) == operations()[13].invoke(0));
    check(nativeBig(1) == 4097u);
}
#else
void checkExtra(unsigned&, unsigned&) noexcept {}
#endif
} // namespace mcu::h7s
