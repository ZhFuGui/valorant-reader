#pragma once
#include "capture/clock.hpp"
#include "transport/record_handler.hpp"
#include <atomic>
#include <memory>
#include <string_view>

namespace valorant::transport {
// 只处理TCP记录与公平的非阻塞I/O，不识别游戏或protobuf。
class TcpServer final {
  public:
    TcpServer(std::uint16_t port, std::size_t maximum_connections, std::string_view bind_address = "127.0.0.1");
    ~TcpServer();
    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;
    void run(IRecordHandler&, const capture::IClock&, void* stop_event, void* wake_event);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace valorant::transport
