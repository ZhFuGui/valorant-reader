#pragma once
#include <array>
#include <cstddef>
namespace valorant::synchronization::v1 {
inline constexpr std::array<std::byte, 32> schema_sha256{
    std::byte{0xef}, std::byte{0x15}, std::byte{0xd4}, std::byte{0xc5}, std::byte{0x67}, std::byte{0x86},
    std::byte{0x1c}, std::byte{0xd4}, std::byte{0xf9}, std::byte{0x88}, std::byte{0x95}, std::byte{0xcd},
    std::byte{0x36}, std::byte{0xb5}, std::byte{0xfd}, std::byte{0x30}, std::byte{0x37}, std::byte{0x04},
    std::byte{0x5c}, std::byte{0xe5}, std::byte{0xb0}, std::byte{0x59}, std::byte{0x15}, std::byte{0x5b},
    std::byte{0xc8}, std::byte{0x91}, std::byte{0xb3}, std::byte{0xbd}, std::byte{0x6d}, std::byte{0x4c},
    std::byte{0xaf}, std::byte{0x01}};
}
