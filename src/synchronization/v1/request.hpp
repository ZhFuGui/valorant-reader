#pragma once
#include "synchronization/v1/wire.hpp"
#include <array>

namespace valorant::synchronization::v1 {
// 严格配对协议：拒绝重复字段、错误方向、超范围值；不在损坏流里猜测下一边界。
struct Request {
    unsigned kind = 0, stream = 0, projection = 1;
    std::uint64_t id = 0, revision = 0, clock = 0;
    double hz = 0;
    std::uint32_t version = 0;
    wire::Bytes schema;
};
Request request(std::span<const std::byte>);
} // namespace valorant::synchronization::v1
