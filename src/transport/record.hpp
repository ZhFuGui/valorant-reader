#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <readers/transport/record.hpp>
#include <span>
#include <vector>

namespace valorant::transport {
inline constexpr std::uint32_t max_record_bytes = readers::transport::maximum_body_bytes;
struct Record {
    std::vector<std::byte> bytes; // 已编码正文，不包含由共享 TCP 层拥有的长度头。
    std::vector<std::size_t> stamp_offsets;
    std::optional<std::uint64_t> source_token;
    std::uint64_t subscription_revision = 0;
    std::uint64_t capture_sequence = 0;
    std::optional<std::uint64_t> clock_candidate;
    std::function<bool(bool)> eligible; // 冻结来源与订阅资格；网络线程仅作原子核验，不访问协议容器。
};
} // namespace valorant::transport
