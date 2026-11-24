#pragma once
#include "game/valorant/offsets.hpp"
#include "memory/read_channel.hpp"

namespace valorant::game::detail {
[[nodiscard]] inline bool user_range(memory::Address address, std::size_t bytes) noexcept {
    return memory::user_range(address, bytes);
}

[[nodiscard]] inline memory::Address field_address(memory::Address base, const offsets::Member& member,
                                                   std::size_t bytes) noexcept {
    if (!member.value || !memory::valid_range(base, std::size_t{*member.value} + bytes))
        return 0;
    const auto address = base + *member.value;
    return user_range(address, bytes) ? address : 0;
}

[[nodiscard]] inline memory::ReadRequest field_read(memory::Address base, const offsets::Member& member,
                                                    void* value, std::size_t bytes) noexcept {
    return {field_address(base, member, bytes), value, bytes, {}};
}

// 任一字段未配置/越界时不提交部分计划；已提交后逐项确认，短读不能算成功。
[[nodiscard]] inline bool complete_fields(memory::IReadChannel& channel, std::span<memory::ReadRequest> requests) noexcept {
    for (const auto& request : requests)
        if (!request.address)
            return false;
    channel.read_many(requests);
    for (const auto& request : requests)
        if (!request.result.complete(request.bytes))
            return false;
    return true;
}
} // namespace valorant::game::detail
