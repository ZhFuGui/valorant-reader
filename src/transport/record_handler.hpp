#pragma once
#include "transport/record.hpp"
#include <span>

namespace valorant::transport {
using ConnectionId = std::uint64_t;
// 业务事件在产品管理线程交付；只有 valid 允许跨到网络线程，只读冻结数据及原子资格。
// next 交出的正文会移动给共享层，sent/discarded 仅使用留下的产品元数据。
class IRecordHandler {
  public:
    virtual ~IRecordHandler() = default;
    virtual void tick() = 0;
    virtual unsigned wait_ms() const = 0;
    virtual void connected(ConnectionId) = 0;
    virtual void received(ConnectionId, std::span<const std::byte>, std::uint64_t received_qpc) = 0;
    virtual void poll(ConnectionId) = 0;
    virtual std::optional<Record> next(ConnectionId) = 0;
    // started 表示已有字节进入 TCP，此时记录失效只能通过结束连接处理。
    virtual bool valid(ConnectionId, const Record&, bool started) = 0;
    virtual void sent(ConnectionId, const Record&) = 0;
    virtual void discarded(ConnectionId, const Record&) {}
    virtual void disconnected(ConnectionId) noexcept = 0;
};
} // namespace valorant::transport
