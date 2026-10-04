// Direct and braced owning temporaries must not become borrowed byte files.
// Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
#include <resource/BytesFile.hpp>
#include <array>
#include <utility>
#include <vector>

using Array = std::array<std::byte, 2>;
Array array{};
const Array constant{};
std::byte cArray[2]{};
const std::byte constCArray[2]{};
struct Conversion
{
    Array owned{};
    operator resource::Input() const noexcept { return owned; }
} conversion;

#if CASE == 1
resource::BytesFile bad{Array{}};
#elif CASE == 2
resource::BytesFile bad{std::move(array)};
#elif CASE == 3
resource::BytesFile bad{std::move(constant)};
#elif CASE == 4
resource::BytesFile bad{std::move(cArray)};
#elif CASE == 5
resource::BytesFile bad{std::move(constCArray)};
#elif CASE == 6
resource::BytesFile bad{Conversion{}};
#elif CASE == 7
resource::BytesFile bad{conversion};
#elif CASE == 8
resource::BytesFile bad{{Array{}}};
#elif CASE == 9
resource::BytesFile bad{{array}};
#elif CASE == 10
resource::BytesFile bad{{Conversion{}}};
#elif CASE == 11
resource::BytesFile bad{{conversion}};
#elif CASE == 12
resource::BytesFile bad{{std::byte{1}, std::byte{2}}};
#elif CASE == 13
resource::BytesFile bad{std::vector<std::byte>(2)};
#elif CASE == 14
std::vector<std::byte> vector(2);
resource::BytesFile bad{vector};
#elif CASE == 15
resource::BytesFile bad{{cArray}};
#elif CASE == 16
resource::BytesFile bad{{constant}};
#elif CASE == 17
// This only declares the source; no huge object is allocated or accessed.
// A constexpr provider must reject a size that cannot be returned as u32.
extern const std::byte oversized[static_cast<std::size_t>(UINT32_MAX) + 1];
constexpr resource::BytesFile bad{resource::Input{oversized, sizeof oversized}};
#else
resource::BytesFile goodArray{array};
resource::BytesFile goodConst{constant};
resource::BytesFile goodCArray{cArray};
resource::BytesFile goodConstCArray{constCArray};
resource::BytesFile goodSpan{resource::Input{array}};
#endif
