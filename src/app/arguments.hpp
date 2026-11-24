#pragma once
#include "app/server.hpp"
#include <span>
#include <string_view>

namespace valorant::app {
enum class Command { Serve, Check, Inspect, Help };
struct Arguments {
    Command command = Command::Serve; // 无参数进入自动发现的持续服务；部署过程不会执行此入口。
    ServerOptions server;
    std::uint32_t inspect_pid = 0;
};
[[nodiscard]] Arguments parse_arguments(std::span<const std::wstring_view>);
} // namespace valorant::app
