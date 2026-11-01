/*
 * @file Workbench.hpp
 * @brief Optional snapshot-preserving read operations over the Workbench's existing FIFO.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_INTEGRATION_READ_MANY_WORKBENCH_HPP
#define TELEMETRY_INTEGRATION_READ_MANY_WORKBENCH_HPP
#pragma once

#include <integration/read_many/Client.hpp>
#include <integration/workbench/WorkbenchModel.hpp>
#include <optional>

namespace telemetry::integration::read_many {
namespace detail {
struct WorkbenchAccess {
private:
	// Nested callback ownership can occur while an outer publication or
	// completion is already active. Restore that prior state, never clear it.
	struct FlagScope {
		bool& flag;
		bool previous;

		explicit FlagScope(bool& value) noexcept : flag(value), previous(value)
		{
			flag = true;
		}

		~FlagScope()
		{
			flag = previous;
		}
	};

	template<class W>
	[[nodiscard]] static bool& publishing(W& owner) noexcept
	{
		return owner.publishing_;
	}

	template<class W>
	[[nodiscard]] static bool& completing(W& owner) noexcept
	{
		return owner.completionActive_;
	}

	template<class W>
	[[nodiscard]] static client::Status gate(const W& owner) noexcept
	{
		const auto status = owner.readOnlyGate();
		if (status != client::Status::Ok)
			return status;
		if (owner.publishing_ || owner.completionActive_ ||
		    owner.phase_ == workbench::Phase::Writing ||
		    owner.phase_ == workbench::Phase::Refreshing ||
		    owner.phase_ == workbench::Phase::Executing)
			return client::Status::Busy;
		return client::Status::Ok;
	}

	template<class F>
	[[nodiscard]] static bool validCallback(const F& callback, std::uint32_t timeout) noexcept
	{
		if (!timeout || timeout > client::maxTimeoutMs)
			return false;
		if constexpr (std::is_pointer_v<std::decay_t<F>>)
			return callback != nullptr;
		return true;
	}

	template<class W, class F>
	struct Completion {
		W* owner;
		std::optional<F> callable;

		template<class G>
		Completion(W& value, G&& callback) noexcept : owner(&value)
		{
			static_assert(std::is_nothrow_constructible_v<F, G&&> &&
			                  std::is_nothrow_move_constructible_v<F> &&
			                  std::is_nothrow_destructible_v<F>,
			              "Workbench read completion ownership must not throw");
			const FlagScope publication{publishing(*owner)};
			callable.emplace(std::forward<G>(callback));
		}

		Completion(Completion&& other) noexcept : owner(other.owner)
		{
			const FlagScope publication{publishing(*owner)};
			callable.emplace(std::move(*other.callable));
		}

		Completion(const Completion&) = delete;
		Completion& operator=(const Completion&) = delete;
		Completion& operator=(Completion&&) = delete;

		~Completion()
		{
			// Reset explicitly while guarded: an ordinary member destructor
			// would run after a destructor-local Publication had already ended.
			const FlagScope publication{publishing(*owner)};
			callable.reset();
		}

		template<class Result>
		void operator()(const Result& result) noexcept
		{
			const FlagScope completion{completing(*owner)};
			(*callable)(result);
		}
	};

public:
	template<class W, class B>
	[[nodiscard]] static ReadManyBindingResult bind(W& owner, B& backend,
	                                                wire::Capability capability,
	                                                std::span<const std::uint32_t> ids) noexcept
	{
		const auto status = gate(owner);
		if (status != client::Status::Ok)
			return {status, {}};
		const FlagScope publication{publishing(owner)};
		const auto result =
		    read_many::bindReadMany(owner.readOnlyClient(), backend, capability, ids);
		const auto captured = owner.readOnlyGate();
		return captured == client::Status::Ok ? result : ReadManyBindingResult{captured, {}};
	}

	template<class W, class F>
	[[nodiscard]] static client::Submission many(W& owner, const ReadManyBinding& binding,
	                                             F&& callback, std::uint32_t timeout) noexcept
	{
		static_assert(std::is_nothrow_invocable_v<std::decay_t<F>&, const ReadManyResult&>,
		              "Workbench ReadMany completion must be noexcept");
		const auto status = gate(owner);
		if (status != client::Status::Ok)
			return {status, {}};
		if (!ReadManyAccess::current(owner.readOnlyClient(), binding))
			return {client::Status::NotAdmitted, {}};
		if (!validCallback(callback, timeout))
			return {client::Status::InvalidArgument, {}};
		const auto frozen = binding;
		const FlagScope publication{publishing(owner)};
		Completion<W, std::decay_t<F>> completion{owner, std::forward<F>(callback)};
		return read_many::readMany(owner.readOnlyClient(), frozen, std::move(completion), timeout);
	}

	template<class W, class F>
	[[nodiscard]] static client::Submission field(W& owner, const dynamic::EndpointBinding& binding,
	                                              F&& callback, std::uint32_t timeout) noexcept
	{
		static_assert(std::is_nothrow_invocable_v<std::decay_t<F>&, const dynamic::DynamicResult&>,
		              "Workbench Field value completion must be noexcept");
		const auto status = gate(owner);
		if (status != client::Status::Ok)
			return {status, {}};
		if (!owner.readOnlyClient().isCurrent(binding))
			return {client::Status::NotAdmitted, {}};
		if (binding.operation() != client::Operation::FieldRead ||
		    !validCallback(callback, timeout))
			return {client::Status::InvalidArgument, {}};
		const auto frozen = binding;
		const FlagScope publication{publishing(owner)};
		Completion<W, std::decay_t<F>> completion{owner, std::forward<F>(callback)};
		return owner.readOnlyClient().readField(frozen, std::move(completion), timeout);
	}
};
} // namespace detail

// These optional operations neither select an Inspector Field nor publish a
// snapshot, change drafts or advance command/service editors. The caller owns
// result bytes inside its callback and keeps the Workbench alive through drain.
// CallbackBytes includes the copied batch binding and the guarded callable;
// explicitly select 768 bytes for a small GUI callback, leaving defaults intact.
template<std::size_t N, std::size_t P, std::size_t S, std::size_t C, class B>
    requires std::is_convertible_v<B*, client::Backend*>
[[nodiscard]] ReadManyBindingResult bindReadMany(workbench::WorkbenchModel<N, P, S, C>& owner,
                                                 B& backend, wire::Capability capability,
                                                 std::span<const std::uint32_t> ids) noexcept
{
	return detail::WorkbenchAccess::bind(owner, backend, capability, ids);
}

template<std::size_t N, std::size_t P, std::size_t S, std::size_t C, class F>
[[nodiscard]] client::Submission readMany(workbench::WorkbenchModel<N, P, S, C>& owner,
                                          const ReadManyBinding& binding, F&& callback,
                                          std::uint32_t timeoutMs = 30000) noexcept
{
	return detail::WorkbenchAccess::many(owner, binding, std::forward<F>(callback), timeoutMs);
}

template<std::size_t N, std::size_t P, std::size_t S, std::size_t C, class F>
[[nodiscard]] client::Submission readFieldValue(workbench::WorkbenchModel<N, P, S, C>& owner,
                                                const dynamic::EndpointBinding& binding,
                                                F&& callback,
                                                std::uint32_t timeoutMs = 30000) noexcept
{
	return detail::WorkbenchAccess::field(owner, binding, std::forward<F>(callback), timeoutMs);
}
} // namespace telemetry::integration::read_many
#endif // TELEMETRY_INTEGRATION_READ_MANY_WORKBENCH_HPP
