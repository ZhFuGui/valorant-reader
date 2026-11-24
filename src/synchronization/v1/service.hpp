#pragma once
#include "capture/clock.hpp"
#include "capture/provider.hpp"
#include "transport/record_handler.hpp"
#include <array>
#include <memory>

namespace valorant::synchronization::v1 {
struct Identity {
    std::array<std::byte, 16> instance{}, clock_domain{};
};
// 连接独占基线和背压状态；此层不依赖游戏偏移、读取通道或 Windows socket。
class Service final : public transport::IRecordHandler {
  public:
    Service(capture::IProvider&, const capture::IClock&, Identity, std::size_t maximum_connections = 16);
    ~Service();
    void tick() override;
    unsigned wait_ms() const override;
    void connected(transport::ConnectionId) override;
    void received(transport::ConnectionId, std::span<const std::byte>, std::uint64_t) override;
    void poll(transport::ConnectionId) override;
    std::optional<transport::Record> next(transport::ConnectionId) override;
    bool valid(transport::ConnectionId, const transport::Record&, bool) override;
    void sent(transport::ConnectionId, const transport::Record&) override;
    void discarded(transport::ConnectionId, const transport::Record&) override;
    void disconnected(transport::ConnectionId) noexcept override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace valorant::synchronization::v1
