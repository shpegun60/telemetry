/**
 * @file ChunkWriter.hpp
 * @brief Bounded atomic-token and partial-byte output; owns no cursor.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see LICENSE.
 */
#pragma once
#include "Types.hpp"
#include <algorithm>
#include <cstring>

namespace resource
{
class ChunkWriter
{
public:
    explicit ChunkWriter(Output output) noexcept : output_(output)
    {
    }

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

    bool writeAtomic(Input input) noexcept
    {
        if (input.size() > remaining())
        {
            return false;
        }
        (void)writePartial(input);
        return true;
    }

    std::size_t writePartial(Input input) noexcept
    {
        const auto count = std::min(remaining(), input.size());
        // memmove permits overlapping input/output and zero-sized null spans.
        if (count != 0)
        {
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
