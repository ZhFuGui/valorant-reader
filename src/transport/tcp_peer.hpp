#pragma once
#include "capture/clock.hpp"
#include "transport/record_decoder.hpp"
#include "transport/record_handler.hpp"
#include <utility>
#include <winsock2.h>

namespace valorant::transport::detail {
[[noreturn]] void socket_error(const char* operation);

struct Socket {
    SOCKET value = INVALID_SOCKET;
    Socket() = default;
    explicit Socket(SOCKET socket) noexcept : value(socket) {}
    ~Socket() {
        if (value != INVALID_SOCKET)
            closesocket(value);
    }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept : value(std::exchange(other.value, INVALID_SOCKET)) {}
};
struct SocketEvent {
    const WSAEVENT value = WSACreateEvent();
    SocketEvent() {
        if (value == WSA_INVALID_EVENT)
            socket_error("WSACreateEvent");
    }
    ~SocketEvent() { WSACloseEvent(value); }
    SocketEvent(const SocketEvent&) = delete;
    SocketEvent& operator=(const SocketEvent&) = delete;
};

// 单条 TCP 字节流的组包、短写及记录失效。监听与连接生命周期由 TcpServer 管理。
class TcpPeer final {
  public:
    TcpPeer(ConnectionId, Socket);
    ConnectionId id() const noexcept { return id_; }
    WSAEVENT event() const noexcept { return event_.value; }
    bool service(IRecordHandler&, const capture::IClock&, bool& more_work);

  private:
    bool receive(IRecordHandler&, const capture::IClock&, bool& more_work);
    bool flush(IRecordHandler&, const capture::IClock&, bool& more_work);

    const ConnectionId id_;
    Socket socket_;
    SocketEvent event_;
    RecordDecoder decoder_{4096};
    std::optional<Record> output_;
    std::size_t sent_bytes_ = 0;
    bool writable_ = true, stamped_ = false;
};
} // namespace valorant::transport::detail
