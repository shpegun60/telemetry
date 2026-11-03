/* Caller-owned standalone writer root; no Qt, Model, client or allocation.
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
 */
#include <wire_edit/Codec.hpp>

extern "C" wire_edit::codec::Status preserving_root(const wire_edit::control::types::Config& value,
                                                    const std::byte* original,
                                                    std::byte* output) noexcept
{
	return wire_edit::codec::encodePreserving(value, {original, 50}, {output, 50});
}
