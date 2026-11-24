#include "transport/tcp_server.hpp"
#include "transport/ipv4.hpp"
#include "transport/tcp_peer.hpp"
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>
#include <ws2tcpip.h>

namespace valorant::transport {
namespace {
using detail::Socket;
using detail::socket_error;
using detail::SocketEvent;
using detail::TcpPeer;

struct Startup {
    Startup() {
        WSADATA data{};
        if (const auto error = WSAStartup(MAKEWORD(2, 2), &data); error)
            throw std::system_error(error, std::system_category(), "WSAStartup");
    }
    ~Startup() { WSACleanup(); }
};
struct Timer {
    HANDLE value = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    Timer() {
        if (!value)
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "timer");
    }
    ~Timer() { CloseHandle(value); }
};
} // namespace
struct TcpServer::Impl {
    Startup startup;
    Socket listener;
    SocketEvent event;
    Timer timer;
    std::size_t maximum;
    ConnectionId next_id{};
    std::vector<std::unique_ptr<TcpPeer>> peers;
    Impl(std::uint16_t port, std::size_t count, std::string_view bind_address) : maximum(count) {
        const auto ipv4 = parse_ipv4(bind_address);
        if (!ipv4)
            throw std::runtime_error("Invalid numeric IPv4 bind address");
        if (!port || !count || count > WSA_MAXIMUM_WAIT_EVENTS - 4)
            throw std::runtime_error("invalid TCP service limits");
        listener.value = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener.value == INVALID_SOCKET)
            socket_error("socket");
        const BOOL yes = TRUE;
        if (setsockopt(listener.value, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&yes),
                       sizeof(yes)) == SOCKET_ERROR)
            socket_error("exclusive socket");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(*ipv4);
        if (bind(listener.value, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
            socket_error("bind");
        if (listen(listener.value, SOMAXCONN) == SOCKET_ERROR)
            socket_error("listen");
        if (WSAEventSelect(listener.value, event.value, FD_ACCEPT | FD_CLOSE) == SOCKET_ERROR)
            socket_error("listener event");
    }
    void accept_ready(IRecordHandler&);
};

void TcpServer::Impl::accept_ready(IRecordHandler& handler) {
    if (WaitForSingleObject(event.value, 0) == WAIT_OBJECT_0) {
        WSANETWORKEVENTS notification{};
        if (WSAEnumNetworkEvents(listener.value, event.value, &notification) == SOCKET_ERROR)
            socket_error("listener events");
        if (notification.lNetworkEvents & FD_CLOSE)
            throw std::runtime_error("listener closed");
        for (std::size_t accepted = 0; accepted < 8; ++accepted) {
            const auto socket_value = accept(listener.value, nullptr, nullptr);
            if (socket_value == INVALID_SOCKET) {
                if (WSAGetLastError() == WSAEWOULDBLOCK)
                    break;
                socket_error("accept");
            }
            if (peers.size() >= maximum) {
                closesocket(socket_value);
                continue;
            }
            Socket accepted_socket(socket_value);
            // socket在Peer构造成功前就交由RAII持有；异常只影响该连接。
            try {
                auto peer = std::make_unique<TcpPeer>(++next_id, std::move(accepted_socket));
                handler.connected(peer->id());
                peers.push_back(std::move(peer));
            } catch (...) {
                handler.disconnected(next_id);
            }
        }
    }
}

TcpServer::TcpServer(std::uint16_t port, std::size_t count, std::string_view bind_address)
    : impl_(std::make_unique<Impl>(port, count, bind_address)) {}
TcpServer::~TcpServer() = default;
void TcpServer::run(IRecordHandler& handler, const capture::IClock& clock, void* stop_event, void* wake_event) {
    auto& state = *impl_;
    struct DisconnectAll {
        Impl& state;
        IRecordHandler& handler;
        ~DisconnectAll() {
            for (const auto& peer : state.peers)
                handler.disconnected(peer->id());
            state.peers.clear();
        }
    } cleanup{state, handler};
    while (WaitForSingleObject(stop_event, 0) != WAIT_OBJECT_0) {
        // 先复位再处理完成票据；之后到来的 worker 通知不会丢失。
        ResetEvent(wake_event);
        handler.tick();
        bool more_work = false;
        state.accept_ready(handler);
        for (auto iterator = state.peers.begin(); iterator != state.peers.end();) {
            auto& peer = **iterator;
            bool keep = false;
            try {
                keep = peer.service(handler, clock, more_work);
            } catch (...) {
                // 单连接的译码或 I/O 异常不影响其他订阅。
            }
            if (!keep) {
                handler.disconnected(peer.id());
                iterator = state.peers.erase(iterator);
            } else
                ++iterator;
        }
        // 先发送已经就绪的数据，再等待下一事件；定时等待不依赖系统粗粒度 tick。
        if (!more_work) {
            LARGE_INTEGER due{};
            due.QuadPart = -static_cast<LONGLONG>((std::max)(1u, handler.wait_ms())) * 10000;
            if (!SetWaitableTimer(state.timer.value, &due, 0, nullptr, nullptr, FALSE))
                throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "arm timer");
        }
        std::vector<WSAEVENT> events{static_cast<HANDLE>(stop_event), static_cast<HANDLE>(wake_event),
                                     state.event.value, state.timer.value};
        for (const auto& peer : state.peers)
            events.push_back(peer->event());
        const auto wait = WSAWaitForMultipleEvents(static_cast<DWORD>(events.size()), events.data(), FALSE,
                                                   more_work ? 0 : WSA_INFINITE, FALSE);
        if (wait == WSA_WAIT_FAILED)
            socket_error("WSAWaitForMultipleEvents");
    }
}
} // namespace valorant::transport
