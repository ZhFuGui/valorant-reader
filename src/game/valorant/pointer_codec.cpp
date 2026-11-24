#include "game/valorant/pointer_codec.hpp"
#include <bit>

namespace valorant::game {
namespace {
struct Selection {
    std::uint32_t index;
    std::uint32_t high;
};

Selection select(std::uint32_t seed) noexcept {
    // 原生 shl ebx,25 是 32 位移位，不能误写成 64 位 seed<<25。
    const std::uint32_t shifted = seed << 25;
    const auto mixed = (std::uint64_t{shifted} ^ ((seed ^ (seed >> 15)) >> 12) ^ seed) *
                       std::uint64_t{0x2545f4914f6cdd1d};
    return {static_cast<std::uint32_t>(mixed % 7), static_cast<std::uint32_t>(mixed >> 32)};
}

std::uint64_t swap_bits(std::uint64_t value, int shift, std::uint64_t mask) noexcept {
    const auto lower = value >> shift;
    return (((value << shift) ^ lower) & mask) ^ lower;
}

std::uint64_t permute(std::uint64_t value) noexcept {
    value = swap_bits(value, 1, 0xaaaaaaaaaaaaaaaa);
    value = swap_bits(value, 2, 0xcccccccccccccccc);
    value = swap_bits(value, 4, 0xf0f0f0f0f0f0f0f0);
    value = swap_bits(value, 8, 0xff00ff00ff00ff00);
    // 原生逻辑没有交换十六位分组，不等于完整的 64 位位序翻转。
    return std::rotr(value, 32);
}

int rotation(std::uint32_t value) noexcept {
    return static_cast<int>(1 + value % 63);
}
} // namespace

std::uint64_t decode_name_key_pointer(const PointerState& state) noexcept {
    const auto [index, high] = select(state.seed);
    const std::uint32_t one = high + index;
    const std::uint32_t two = high + 2u * index;
    auto value = state.values[index];
    switch (index) {
    case 0: value = std::rotl(permute(value), rotation(high)); break;
    case 1: value = permute(std::rotr(value, rotation(two))); break;
    case 2: value = (value ^ ~std::uint64_t{two}) - one; break;
    case 3: value = std::rotl(swap_bits(value, 1, 0xaaaaaaaaaaaaaaaa), rotation(one)); break;
    case 4: value = std::rotl(value, rotation(two)) ^ ~std::uint64_t{one}; break;
    case 6: value = std::rotr(swap_bits(value, 1, 0xaaaaaaaaaaaaaaaa), rotation(one)); break;
    default: break; // 原生第五分支不再变换状态值。
    }
    return value ^ state.seed;
}

std::uint64_t decode_object_storage_pointer(const PointerState& state) noexcept {
    const auto [index, high] = select(state.seed);
    const std::uint32_t one = high + index;
    const std::uint32_t two = high + 2u * index;
    auto value = state.values[index];
    switch (index) {
    case 0: value = permute(value) ^ ~std::uint64_t{high}; break;
    case 1: value = std::rotl(std::rotl(value, rotation(two)), rotation(one)); break;
    case 2: value = std::rotr(value, rotation(two)) + one; break;
    case 3: value = swap_bits(value ^ ~std::uint64_t{two}, 1, 0xaaaaaaaaaaaaaaaa); break;
    case 4: value = std::rotl(value + two, rotation(one)); break;
    case 5: value = std::rotr(value, rotation(two)) - one; break;
    default: break; // 原生第六分支连续交换相邻位两次，结果等于原值。
    }
    return value ^ state.seed;
}
} // namespace valorant::game
