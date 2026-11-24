#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace valorant::transport {
// 通用记录边界，不依赖业务模型或 protobuf。
inline constexpr std::uint32_t max_record_bytes = 16U * 1024U * 1024U;
struct Record {
    std::vector<std::byte> bytes;
    std::vector<std::size_t> stamp_offsets;
    std::optional<std::uint64_t> source_token;
    std::uint64_t subscription_revision = 0;
    std::uint64_t capture_sequence = 0;
    std::optional<std::uint64_t> clock_candidate;
};

[[nodiscard]] constexpr std::optional<std::array<std::byte, 4>> encode_length(std::uint32_t bytes) noexcept {
    if (!bytes || bytes > max_record_bytes)
        return {};
    return std::array{std::byte(bytes >> 24), std::byte(bytes >> 16), std::byte(bytes >> 8), std::byte(bytes)};
}

[[nodiscard]] constexpr std::optional<std::uint32_t> decode_length(std::span<const std::byte, 4> prefix) noexcept {
    const auto bytes = (std::to_integer<std::uint32_t>(prefix[0]) << 24) |
                       (std::to_integer<std::uint32_t>(prefix[1]) << 16) |
                       (std::to_integer<std::uint32_t>(prefix[2]) << 8) | std::to_integer<std::uint32_t>(prefix[3]);
    if (!bytes || bytes > max_record_bytes)
        return {};
    return bytes;
}
} // namespace valorant::transport
