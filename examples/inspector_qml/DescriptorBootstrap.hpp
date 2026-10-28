/*
 * @file DescriptorBootstrap.hpp
 * @brief Sequential Resource discovery shared by the application's transports.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_EXAMPLES_INSPECTOR_QML_DESCRIPTOR_BOOTSTRAP_HPP
#define TELEMETRY_EXAMPLES_INSPECTOR_QML_DESCRIPTOR_BOOTSTRAP_HPP
#pragma once

#include <resource/protocol/Packets.hpp>
#include <telemetry/type/WireTypes.hpp>
#include <QtCore/QByteArray>
#include <limits>
#include <optional>
#include <span>

namespace inspector_qml {
class DescriptorBootstrap {
public:
	void reset()
	{
		*this = DescriptorBootstrap{};
	}

	resource::protocol::Op operation() const noexcept
	{
		return operation_;
	}

	bool complete() const noexcept
	{
		return complete_;
	}

	const QByteArray& descriptor() const noexcept
	{
		return descriptor_;
	}

	resource::protocol::client::BuildResult request(std::span<std::byte> output) const noexcept
	{
		namespace rc = resource::protocol::client;
		using Op = resource::protocol::Op;
		if (complete_)
			return {rc::BuildStatus::BufferTooSmall, 0};
		if (operation_ == Op::List)
			return rc::makeList(output, cursor_);
		if (!index_)
			return {rc::BuildStatus::BufferTooSmall, 0};
		return operation_ == Op::Stat ? rc::makeStat(output, *index_)
		                              : rc::makeRead(output, *index_, cursor_);
	}

	bool accept(std::span<const std::byte> bytes)
	{
		namespace rc = resource::protocol::client;
		using Op = resource::protocol::Op;
		if (complete_)
			return false;
		if (operation_ == Op::List) {
			const auto reply = rc::parseList(bytes);
			if (!reply || reply.shortError || reply.response.status != resource::Status::Ok ||
			    ++pages_ > 4096 ||
			    reply.response.paths.size() >
			        std::numeric_limits<resource::Cursor>::max() - cursor_ ||
			    reply.response.next != cursor_ + reply.response.paths.size() ||
			    reply.response.next > std::numeric_limits<resource::FileIndex>::max())
				return false;
			auto index = cursor_;
			for (const auto path : reply.response.paths) {
				if (path == "/telemetry/descriptor.bin") {
					if (index_)
						return false;
					index_ = static_cast<resource::FileIndex>(index);
				}
				++index;
			}
			if (!reply.response.eof && reply.response.next == cursor_)
				return false;
			cursor_ = reply.response.next;
			if (reply.response.eof) {
				if (!index_)
					return false;
				operation_ = Op::Stat;
			}
		} else if (operation_ == Op::Stat) {
			const auto reply = rc::parseStat(bytes);
			if (!reply || reply.shortError || reply.response.status != resource::Status::Ok ||
			    !resource::has(reply.response.flags, resource::FileFlag::Readable) ||
			    reply.response.size < 64 ||
			    reply.response.size > telemetry::Limits::maxDescriptorBytes)
				return false;
			size_ = reply.response.size;
			descriptor_.reserve(static_cast<qsizetype>(size_));
			cursor_ = 0;
			operation_ = Op::Read;
		} else if (operation_ == Op::Read) {
			const auto reply = rc::parseRead(bytes);
			if (!reply || reply.shortError || reply.response.status != resource::Status::Ok ||
			    cursor_ > size_ || reply.response.data.empty() ||
			    reply.response.data.size() > size_ - cursor_ ||
			    reply.response.next != cursor_ + reply.response.data.size())
				return false;
			const auto next = reply.response.next;
			if (reply.response.eof != (next == size_))
				return false;
			descriptor_.append(reinterpret_cast<const char*>(reply.response.data.data()),
			                   static_cast<qsizetype>(reply.response.data.size()));
			cursor_ = next;
			complete_ = reply.response.eof;
		} else {
			return false;
		}
		return true;
	}

private:
	QByteArray descriptor_;
	std::optional<resource::FileIndex> index_;
	resource::Cursor cursor_ = 0;
	resource::FileSize size_ = 0;
	unsigned pages_ = 0;
	resource::protocol::Op operation_ = resource::protocol::Op::List;
	bool complete_ = false;
};
} // namespace inspector_qml
#endif
