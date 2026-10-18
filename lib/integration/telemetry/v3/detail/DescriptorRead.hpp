/*
 * @file DescriptorRead.hpp
 * @brief Explicit mapping from descriptor byte results to Resource outcomes.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef RESOURCE_TELEMETRY_V3_DETAIL_DESCRIPTOR_READ_HPP
#define RESOURCE_TELEMETRY_V3_DETAIL_DESCRIPTOR_READ_HPP
#pragma once

#include <resource/Types.hpp>
#include <telemetry/descriptor/ReadResult.hpp>

namespace resource::telemetry::v3::detail {

[[nodiscard]] constexpr ReadResult
descriptorRead(::telemetry::descriptor::ReadResult result) noexcept
{
	Status status = Status::InvalidData;
	switch (result.status) {
		case ::telemetry::descriptor::ReadStatus::Ok:
			status = Status::Ok;
			break;
		case ::telemetry::descriptor::ReadStatus::InvalidCursor:
			status = Status::InvalidCursor;
			break;
		case ::telemetry::descriptor::ReadStatus::BufferTooSmall:
			status = Status::BufferTooSmall;
			break;
		case ::telemetry::descriptor::ReadStatus::InvalidData:
			status = Status::InvalidData;
			break;
	}
	return {status, result.next, result.written, result.eof};
}

} // namespace resource::telemetry::v3::detail
#endif
