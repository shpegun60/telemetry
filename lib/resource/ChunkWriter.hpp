/**
 * @file ChunkWriter.hpp
 * @brief Bounded atomic-token and partial-byte output; owns no cursor.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see LICENSE.
 *
 * Compose bounded output without encoding a resource cursor in the helper.
 * Atomic writes preserve the buffer on insufficient capacity; partial writes
 * commit only the prefix that fits in the borrowed output span.
 */

#ifndef TELEMETRY_LIB_RESOURCE_CHUNKWRITER_HPP
#define TELEMETRY_LIB_RESOURCE_CHUNKWRITER_HPP
#pragma once

#include "Types.hpp"
#include <algorithm>
#include <cstring>

namespace resource {
// The output span remains borrowed for the writer lifetime. used_ is always
// within output_.size(); overlapping input is permitted for both operations.
// Public methods:
// - ChunkWriter(): Borrow output storage.
// - written(): Count committed bytes.
// - remaining(): Report free capacity.
// - empty(): Check zero progress.
// - writeAtomic(): Commit complete token.
// - writePartial(): Commit fitting prefix.
class ChunkWriter {
public:
	explicit ChunkWriter(Output output) noexcept : output_(output)
	{}

	std::size_t written() const noexcept
	{
		return used_;
	}

	std::size_t remaining() const noexcept
	{
		return output_.size() - used_;
	}

	bool empty() const noexcept
	{
		return used_ == 0;
	}

	// Commit a complete token or leave both bytes and progress unchanged.
	bool writeAtomic(Input input) noexcept
	{
		if (input.size() > remaining()) {
			return false;
		}
		(void)writePartial(input);
		return true;
	}

	// Commit the fitting prefix; the return count belongs to this input span.
	std::size_t writePartial(Input input) noexcept
	{
		const auto count = std::min(remaining(), input.size());
		// memmove permits overlapping input/output and zero-sized null spans.
		if (count != 0) {
			std::memmove(output_.data() + used_, input.data(), count);
		}
		used_ += count;
		return count;
	}

private:
	Output output_;
	std::size_t used_ = 0;
};
} // namespace resource

#endif // TELEMETRY_LIB_RESOURCE_CHUNKWRITER_HPP
