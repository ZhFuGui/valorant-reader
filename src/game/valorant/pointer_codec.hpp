#pragma once
#include <array>
#include <cstdint>

namespace valorant::game {
struct PointerState {
    std::array<std::uint64_t, 7> values{}; // 本次观测的七条编码状态，仅使用种子选择的条目。
    std::uint32_t seed = 0;               // 32 位原生种子，必须保留原生溢出规则。
    std::uint32_t revision = 0;           // 原生调用计数；不作为对象生命周期编号。
    std::uint8_t updating = 0;            // 更新锁非零时，由调用方拒绝本次观测。
};

// 仅执行本地无副作用整数计算，不访问内存、不调用目标函数、不改变输入状态。
[[nodiscard]] std::uint64_t decode_name_key_pointer(const PointerState&) noexcept;
[[nodiscard]] std::uint64_t decode_object_storage_pointer(const PointerState&) noexcept;
} // namespace valorant::game
