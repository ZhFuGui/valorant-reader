#pragma once
#include "memory/read_channel.hpp"

namespace valorant::platform::windows {
struct PeImage {
    memory::Address base = 0;          // 已绑定进程内的主映像起点。
    std::uint32_t timestamp = 0;       // 原始 PE 时间标识。
    std::uint32_t image_bytes = 0;     // 原始 PE 映像大小。
    std::uint32_t checksum = 0;        // 原始 PE 校验值，不使用重建 dump 的替代值。
};

// 读取并关闭核验原始 PE 头；失败保持缺失，不打开目标进程句柄。
[[nodiscard]] std::optional<PeImage> read_pe_image(memory::IReadChannel&, memory::Address base) noexcept;
} // namespace valorant::platform::windows
