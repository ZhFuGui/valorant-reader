#include "transport/tcp_peer.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>
#include <system_error>

namespace valorant::transport::detail {
[[noreturn]] void socket_error(const char* operation) {
    throw std::system_error(WSAGetLastError(), std::system_category(), operation);
}
namespace {
void abort_stream_on_close(SOCKET socket) {
    const linger immediate{1, 0};
    // 即使客户端不再读，也让本次关闭立即结束这条字节流。
    (void)setsockopt(socket, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&immediate), sizeof(immediate));
}
void stamp(Record& record, std::uint64_t value) {
    for (auto offset : record.stamp_offsets) {
        if (offset > record.bytes.size() || record.bytes.size() - offset < 8)
            throw std::runtime_error("invalid record stamp");
        for (std::size_t i = 0; i < 8; ++i)
            record.bytes[offset + i] = std::byte((value >> (8 * i)) & 0xffU);
    }
}
} // namespace

TcpPeer::TcpPeer(ConnectionId identity, Socket value) : id_(identity), socket_(std::move(value)) {
    const BOOL yes = TRUE;
    if (setsockopt(socket_.value, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&yes), sizeof(yes)) ==
            SOCKET_ERROR ||
        WSAEventSelect(socket_.value, event_.value, FD_READ | FD_WRITE | FD_CLOSE) == SOCKET_ERROR)
        socket_error("configure peer");
}

bool TcpPeer::service(IRecordHandler& handler, const capture::IClock& clock, bool& more_work) {
    if (!receive(handler, clock, more_work))
        return false;
    handler.poll(id_);
    if (!output_ && writable_) {
        output_ = handler.next(id_);
        sent_bytes_ = 0;
        stamped_ = false;
    }
    if (output_ && !handler.valid(id_, *output_, sent_bytes_ != 0)) {
        if (sent_bytes_) {
            abort_stream_on_close(socket_.value);
            return false; // 半条记录失效后，已入 TCP 的前缀不可撤回。
        }
        output_.reset();
        more_work = true;
    }
    return !output_ || !writable_ || flush(handler, clock, more_work);
}

bool TcpPeer::receive(IRecordHandler& handler, const capture::IClock& clock, bool& more_work) {
    WSANETWORKEVENTS notification{};
    if (WaitForSingleObject(event_.value, 0) == WAIT_OBJECT_0) {
        if (WSAEnumNetworkEvents(socket_.value, event_.value, &notification) == SOCKET_ERROR)
            socket_error("peer events");
        if (notification.lNetworkEvents & FD_WRITE)
            writable_ = true;
        if (notification.lNetworkEvents & (FD_READ | FD_CLOSE)) {
            std::array<std::byte, 16384> buffer{};
            for (std::size_t budget = 0; budget < 65536;) {
                const auto count =
                    recv(socket_.value, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0);
                if (!count) {
                    decoder_.finish();
                    return false;
                }
                if (count == SOCKET_ERROR) {
                    if (WSAGetLastError() == WSAEWOULDBLOCK)
                        break;
                    socket_error("recv");
                }
                budget += static_cast<std::size_t>(count);
                decoder_.feed(std::span{buffer}.first(static_cast<std::size_t>(count)), [&](auto body) {
                    const auto received = clock.now();
                    handler.received(id_, body, received);
                });
                if (budget >= 65536)
                    more_work = true;
            }
        }
    }
    return true;
}

bool TcpPeer::flush(IRecordHandler& handler, const capture::IClock& clock, bool& more_work) {
    if (!stamped_) {
        const auto now = clock.now();
        if (!now)
            throw std::runtime_error("missing send clock");
        stamp(*output_, now);
        stamped_ = true;
    }
    const auto remaining = output_->bytes.size() - sent_bytes_;
    const auto count = send(socket_.value, reinterpret_cast<const char*>(output_->bytes.data() + sent_bytes_),
                            static_cast<int>(std::min<std::size_t>(remaining, 65536)), 0);
    if (count == SOCKET_ERROR) {
        if (WSAGetLastError() == WSAEWOULDBLOCK)
            writable_ = false;
        else
            socket_error("send");
    } else if (!count)
        return false;
    else {
        sent_bytes_ += static_cast<std::size_t>(count);
        if (sent_bytes_ == output_->bytes.size()) {
            handler.sent(id_, *output_);
            output_.reset();
        }
        more_work = true;
    }
    return true;
}
} // namespace valorant::transport::detail
