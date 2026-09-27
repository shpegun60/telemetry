/*
 * @file Other.cpp
 * @brief Descriptor and packed bytes have the same identity in another TU.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include "Fixture.hpp"
std::uint64_t descriptorOtherFingerprint() noexcept
{ return descriptor_fixture::edge.fingerprint(); }
const std::byte* descriptorOtherBytes() noexcept
{ return descriptor_fixture::edgeBytes.data(); }
