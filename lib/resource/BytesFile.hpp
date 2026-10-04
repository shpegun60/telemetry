/**
 * @file BytesFile.hpp
 * @brief Read-only resource borrowing a stable byte buffer.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see LICENSE.
 *
 * Expose existing storage as a file without copying it into a second buffer.
 * Reads use byte offsets and permit overlapping source and output; callers
 * keep the borrowed source alive and synchronize any mutation.
 */

#ifndef TELEMETRY_LIB_RESOURCE_BYTESFILE_HPP
#define TELEMETRY_LIB_RESOURCE_BYTESFILE_HPP
#pragma once

#include "Types.hpp"
#include <array>
#include <concepts>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <type_traits>

namespace resource {
namespace detail {
template<class T>
inline constexpr bool byteSpan = false;

template<class T, std::size_t N>
inline constexpr bool byteSpan<std::span<T, N>> = std::same_as<std::remove_const_t<T>, std::byte>;

template<class T>
inline constexpr bool byteArray = false;

template<std::size_t N>
inline constexpr bool byteArray<std::array<std::byte, N>> = true;

template<std::size_t N>
inline constexpr bool byteArray<std::byte[N]> = true;

template<class Source>
concept BorrowedBytesSource =
    !std::is_volatile_v<std::remove_reference_t<Source>> &&
    (byteSpan<std::remove_cvref_t<Source>> ||
     (std::is_lvalue_reference_v<Source> && byteArray<std::remove_cvref_t<Source>>));

[[noreturn]] inline void bytesFileSizeExceeded() noexcept
{
	std::abort();
}
} // namespace detail

// The source bytes must outlive this provider and every read. The provider
// owns only a span: changes to a mutable source are visible on later reads.
// Explicit spans are already borrowed views; their lifetime is the caller's
// responsibility. Direct owning array temporaries and conversion proxies are
// rejected before they can be converted to a span.
// Public methods:
// - BytesFile(): Borrow byte storage.
// - size(): Report byte extent.
// - read(): Read bounded slice.
class BytesFile {
public:
	// Deduce the original source before any conversion to Input. Requiring a
	// deducible typed argument also rejects nested braced temporary buffers.
	template<detail::BorrowedBytesSource Source>
	constexpr explicit BytesFile(Source&& bytes) noexcept : bytes_(bytes)
	{
		if (bytes_.size() > std::numeric_limits<FileSize>::max()) {
			detail::bytesFileSizeExceeded();
		}
	}

	template<class Source>
	    requires(!detail::BorrowedBytesSource<Source> &&
	             !std::same_as<std::remove_cvref_t<Source>, BytesFile>)
	BytesFile(Source&&) = delete;

	[[nodiscard]] constexpr FileSize size() const noexcept
	{
		return static_cast<FileSize>(bytes_.size());
	}

	// Read a byte-offset slice. Invalid/empty preflight writes nothing; EOF
	// is a successful zero-byte read even when output has no capacity.
	[[nodiscard]] ReadResult read(Cursor cursor, Output output) const noexcept
	{
		const auto bytes = size();
		if (cursor > bytes) {
			return {Status::InvalidCursor, cursor};
		}
		if (cursor == bytes) {
			return {Status::Ok, cursor, 0, true};
		}
		if (output.empty()) {
			return {Status::BufferTooSmall, cursor};
		}

		// The cursor is within the validated u32-sized source before narrowing.
		const auto offset = static_cast<FileSize>(cursor);
		const auto remaining = bytes - offset;
		const auto count =
		    static_cast<FileSize>(output.size() < remaining ? output.size() : remaining);
		std::memmove(output.data(), bytes_.data() + offset, count);
		return {Status::Ok, cursor + count, count, cursor + count == bytes};
	}

private:
	Input bytes_;
};
} // namespace resource

#endif // TELEMETRY_LIB_RESOURCE_BYTESFILE_HPP
