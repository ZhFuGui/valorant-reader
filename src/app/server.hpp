#pragma once
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>

namespace valorant::app {
struct ServerOptions {
    std::optional<std::uint32_t> pid; // 缺失时自动发现，显式 PID 只绑定该实例。
    std::uint16_t port = 13302;
    std::size_t maximum_connections = 16;
    std::string bind = "127.0.0.1";
    std::wstring stop_event;
};
int serve(const ServerOptions&, std::ostream&, std::ostream&);
} // namespace valorant::app
