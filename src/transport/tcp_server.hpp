#pragma once
#include "capture/clock.hpp"
#include "transport/record_handler.hpp"
#include <functional>
#include <memory>
#include <readers/transport/tcp_server.hpp>
#include <string_view>

namespace valorant::transport {
// 共享 TCP 与产品协议所有者之间的有界交接；不拥有 socket、组包或短写实现。
class TcpServer final {
  public:
    TcpServer(std::uint16_t port, std::size_t maximum_connections, std::string_view bind_address = "127.0.0.1");
    explicit TcpServer(readers::transport::TcpOptions);
    ~TcpServer();
    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;
    using StatusCallback = std::function<void(readers::transport::ServiceStatus)>;
    void run(IRecordHandler&, const capture::IClock&, void* stop_event, void* wake_event, StatusCallback = {});

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace valorant::transport
