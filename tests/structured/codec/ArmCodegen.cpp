/*
 * @file ArmCodegen.cpp
 * @brief Cortex-M7 stack and return-ABI probes for a 4 KiB value.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include <telemetry/codec/Codec.hpp>
#include <telemetry/result/ServiceResult.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <span>

using Big = std::array<std::uint32_t, 1024>;
static_assert(telemetry::wireSize<Big> == 4096);

struct ServiceLike {
    bool ok;
    Big response;
};

struct OptionalServiceLike {
    bool ok;
    std::optional<Big> response;
};

struct DefaultedBig {
    std::uint32_t marker = 7;
    Big response;
};

extern "C" [[gnu::noinline]] Big make_raw(std::uint32_t seed) noexcept
{
    Big result;
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = seed + static_cast<std::uint32_t>(i);
    return result;
}

extern "C" [[gnu::noinline]] ServiceLike make_service(std::uint32_t seed) noexcept
{
    return ServiceLike{true, make_raw(seed)};
}

extern "C" [[gnu::noinline]] OptionalServiceLike
make_optional_service(std::uint32_t seed) noexcept
{
    return OptionalServiceLike{true, std::optional<Big>{make_raw(seed)}};
}

extern "C" [[gnu::noinline]] telemetry::ServiceResult<Big>
make_actual_service(std::uint32_t seed) noexcept
{
    return telemetry::ServiceResult<Big>::successFrom(
        [seed]() noexcept { return make_raw(seed); });
}

extern "C" [[gnu::noinline]] telemetry::CodecStatus
encode_big(const Big& input, std::span<std::byte> output) noexcept
{
    return telemetry::encode(input, output);
}

extern "C" [[gnu::noinline]] std::uint32_t
decode_big(std::span<const std::byte> input,
           telemetry::Workspace& workspace) noexcept
{
    auto lease = workspace.reserve<Big>();
    Big* value = nullptr;
    if (telemetry::decode(input, lease, value) !=
        telemetry::CodecStatus::Ok)
        return 0;
    return (*value)[0] + (*value)[1023];
}

extern "C" [[gnu::noinline]] std::uint32_t
decode_defaulted_big(std::span<const std::byte> input,
                     telemetry::Workspace& workspace) noexcept
{
    auto lease = workspace.reserve<DefaultedBig>();
    DefaultedBig* value = nullptr;
    if (telemetry::decode(input, lease, value) !=
        telemetry::CodecStatus::Ok)
        return 0;
    return value->marker + value->response[1023];
}

extern "C" [[gnu::noinline]] Big* direct_return(void* storage,
                                                  std::uint32_t seed) noexcept
{
    return ::new (storage) Big(make_raw(seed));
}

extern "C" [[gnu::noinline]] Big* construct_at_return(Big* storage,
                                                       std::uint32_t seed) noexcept
{
    return std::construct_at(storage, make_raw(seed));
}

template <class Factory>
[[gnu::noinline]] Big* forwarding_return(void* storage, Factory&& factory) noexcept
{
    return ::new (storage) Big(std::forward<Factory>(factory)());
}

extern "C" [[gnu::noinline]] Big* forwarded_return(void* storage,
                                                     std::uint32_t seed) noexcept
{
    return forwarding_return(storage, [seed]() noexcept { return make_raw(seed); });
}

extern "C" [[gnu::noinline]] ServiceLike* service_return(void* storage,
                                                           std::uint32_t seed) noexcept
{
    return ::new (storage) ServiceLike(make_service(seed));
}

extern "C" [[gnu::noinline]] OptionalServiceLike*
optional_service_return(void* storage, std::uint32_t seed) noexcept
{
    return ::new (storage) OptionalServiceLike(make_optional_service(seed));
}

extern "C" [[gnu::noinline]] telemetry::ServiceResult<Big>*
actual_service_return(void* storage, std::uint32_t seed) noexcept
{
    return ::new (storage) telemetry::ServiceResult<Big>(make_actual_service(seed));
}
