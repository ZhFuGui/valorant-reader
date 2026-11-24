#include "transport/tcp_server.hpp"
#include <algorithm>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>
#include <windows.h>

namespace valorant::transport {
namespace {
namespace tcp = readers::transport;
struct Outgoing {
    Record metadata;
    tcp::Record record;
    std::uint64_t tag = 0; // 不依赖已移动的 shared::Record 仍保留 tag。
    bool accepted = false, backpressured = false;
    std::uint64_t retry_at = 0, writable_seen = 0;
};
struct Received {
    std::vector<std::byte> body;
    std::uint64_t qpc = 0;
};
enum class Completion { None, Sent, Discarded };
struct Peer {
    explicit Peer(ConnectionId value) : id(value) {}
    const ConnectionId id;
    bool announced = false, closed = false;
    std::uint64_t writable_revision = 0;
    std::deque<Received> input;
    std::shared_ptr<Outgoing> outgoing;
    Completion completion = Completion::None;
    std::uint64_t completion_qpc = 0;
};

class Bridge final : public tcp::RecordHandler {
  public:
    Bridge(IRecordHandler& handler, const capture::IClock& clock, HANDLE wake, std::size_t maximum)
        : handler_(handler), clock_(clock), wake_(wake), maximum_(maximum) {
        peers_.reserve(maximum);
        snapshot_.reserve(maximum);
    }
    ~Bridge() { finish(); } // 调用方保证共享网络线程已经 join。
    tcp::TcpServer* server = nullptr;

    void connected(const tcp::PeerInfo& info) override {
        {
            std::lock_guard lock(mutex_);
            if (peers_.size() < maximum_) {
                peers_.emplace(info.id, std::make_shared<Peer>(info.id));
                signal();
                return;
            }
        }
        server->close(info.id); // 待处理断线也占产品预算，不能用反复重连堆积事件。
    }
    void received(ConnectionId id, std::span<const std::byte> body, std::uint64_t qpc) override {
        std::lock_guard lock(mutex_);
        const auto it = peers_.find(id);
        if (it == peers_.end() || it->second->closed)
            return;
        if (body.size() > 4096 || it->second->input.size() >= 16)
            throw std::runtime_error("control budget");
        it->second->input.push_back({{body.begin(), body.end()}, qpc});
        signal();
    }
    bool eligible(ConnectionId id, const tcp::RecordInfo& info, bool started) override {
        const auto outgoing = active(id, info.tag);
        return outgoing && handler_.valid(id, outgoing->metadata, started);
    }
    bool before_send(ConnectionId id, const tcp::RecordInfo& info, std::span<std::byte> body,
                     std::uint64_t qpc) override {
        const auto outgoing = active(id, info.tag);
        if (!outgoing || !qpc || !handler_.valid(id, outgoing->metadata, false))
            return false;
        for (const auto offset : outgoing->metadata.stamp_offsets) {
            if (offset > body.size() || body.size() - offset < 8)
                return false;
            for (std::size_t i = 0; i < 8; ++i)
                body[offset + i] = std::byte((qpc >> (8 * i)) & 0xffU);
        }
        return true;
    }
    void sent(ConnectionId id, const tcp::RecordInfo& info, const tcp::SendTiming& timing) override {
        complete(id, info.tag, Completion::Sent, timing.finished_qpc);
    }
    void discarded(ConnectionId id, const tcp::RecordInfo& info, tcp::DiscardReason) override {
        complete(id, info.tag, Completion::Discarded, clock_.now());
    }
    void writable(ConnectionId id) override {
        std::lock_guard lock(mutex_);
        if (const auto it = peers_.find(id); it != peers_.end())
            ++it->second->writable_revision;
        signal();
    }
    void disconnected(ConnectionId id, tcp::Error) noexcept override {
        try {
            std::lock_guard lock(mutex_);
            if (const auto it = peers_.find(id); it != peers_.end())
                it->second->closed = true;
            signal(); // 固定标志不与正文事件争抢容量，断线不会因邮箱满而丢失。
        } catch (...) {
            server->request_stop();
        }
    }
    void service_changed(tcp::ServiceStatus) noexcept override { signal(); }

    bool pump() {
        handler_.tick();
        snapshot_.clear();
        {
            std::lock_guard lock(mutex_);
            for (const auto& [id, peer] : peers_) {
                (void)id;
                snapshot_.push_back(peer);
            }
        }
        bool more = false;
        bool check_queued = false;
        for (const auto& peer : snapshot_) {
            try {
                more |= process(peer);
            } catch (...) {
                close(peer->id);
            }
            // outgoing 仅由管理线程安装/释放；网络线程只读其元数据并写完成标志。
            check_queued |= peer->outgoing && peer->outgoing->accepted;
        }
        if (check_queued)
            server->notify(); // 仅有在途记录时才需复验，空服务不反复唤醒网络线程。
        return more;
    }

  private:
    void signal() const noexcept { SetEvent(wake_); }
    std::shared_ptr<Outgoing> active(ConnectionId id, std::uint64_t tag) {
        std::lock_guard lock(mutex_);
        const auto it = peers_.find(id);
        if (it == peers_.end() || it->second->closed || !it->second->outgoing || it->second->outgoing->tag != tag)
            return {};
        return it->second->outgoing;
    }
    void complete(ConnectionId id, std::uint64_t tag, Completion completion, std::uint64_t qpc) {
        std::lock_guard lock(mutex_);
        const auto it = peers_.find(id);
        if (it != peers_.end() && it->second->outgoing && it->second->outgoing->tag == tag) {
            it->second->completion = completion;
            it->second->completion_qpc = qpc;
        }
        signal(); // 完成不分配事件节点；submit 返回前到达也不会被覆盖。
    }
    void close(ConnectionId id) noexcept {
        server->close(id);
        handler_.disconnected(id);
        std::lock_guard lock(mutex_);
        peers_.erase(id);
    }
    void finish() noexcept {
        for (const auto& [id, peer] : peers_) {
            (void)peer;
            handler_.disconnected(id);
        }
        peers_.clear();
    }
    bool process(const std::shared_ptr<Peer>& peer) {
        bool closed = false;
        Completion completion;
        std::shared_ptr<Outgoing> outgoing;
        {
            std::lock_guard lock(mutex_);
            closed = peer->closed;
            // 收到控制请求的真实重组时刻可能早于发送完成，不能用管理线程处理顺序倒置因果。
            completion = Completion::None;
            if (peer->input.empty() || peer->input.front().qpc >= peer->completion_qpc)
                completion = std::exchange(peer->completion, Completion::None);
            outgoing = peer->outgoing;
        }
        if (closed) {
            close(peer->id);
            return false;
        }
        if (!peer->announced) {
            peer->announced = true;
            handler_.connected(peer->id);
        }
        // 先处理发送完成，再解释后来到达的控制请求，保证时钟映射选择等因果关系。
        if (outgoing && completion != Completion::None) {
            if (completion == Completion::Sent)
                handler_.sent(peer->id, outgoing->metadata);
            else
                handler_.discarded(peer->id, outgoing->metadata);
            {
                std::lock_guard lock(mutex_);
                peer->outgoing.reset();
            }
            outgoing.reset();
        }
        std::optional<Received> received;
        bool more = false;
        {
            std::lock_guard lock(mutex_);
            if (!peer->input.empty()) {
                received = std::move(peer->input.front());
                peer->input.pop_front();
                more = !peer->input.empty();
            }
        }
        if (received)
            handler_.received(peer->id, received->body, received->qpc);
        handler_.poll(peer->id);
        if (!outgoing) {
            auto next = handler_.next(peer->id);
            if (!next)
                return more;
            if (!handler_.valid(peer->id, *next, false)) {
                handler_.discarded(peer->id, *next);
                return true;
            }
            outgoing = std::make_shared<Outgoing>();
            outgoing->metadata = std::move(*next);
            outgoing->record.body = std::move(outgoing->metadata.bytes);
            outgoing->tag = ++tag_;
            // 当前协议增量相对前一条完整发送状态；只用可靠 FIFO，不混入 Latest。
            outgoing->record.delivery = tcp::Delivery::Reliable;
            std::lock_guard lock(mutex_);
            peer->outgoing = outgoing; // 必须早于 submit，回调可能先于它返回。
        }
        if (!outgoing->accepted) {
            const auto now = capture::nanoseconds(clock_);
            std::uint64_t writable;
            {
                std::lock_guard lock(mutex_);
                writable = peer->writable_revision;
            }
            if (now < outgoing->retry_at || (outgoing->backpressured && writable == outgoing->writable_seen))
                return more;
            if (!handler_.valid(peer->id, outgoing->metadata, false)) {
                handler_.discarded(peer->id, outgoing->metadata);
                std::lock_guard lock(mutex_);
                peer->outgoing.reset();
                return true;
            }
            outgoing->record.tag = outgoing->tag;
            const auto result = server->submit(peer->id, std::move(outgoing->record));
            switch (result.status) {
            case tcp::SubmitStatus::Accepted:
                outgoing->accepted = true;
                break;
            case tcp::SubmitStatus::Replaced:
                // Reliable 不应发生替换；不能猜测被替换的链式基线，直接结束本连接。
                close(peer->id);
                return more;
            case tcp::SubmitStatus::Backpressured:
                outgoing->backpressured = true;
                outgoing->writable_seen = writable;
                break;
            case tcp::SubmitStatus::ResourceExhausted:
            case tcp::SubmitStatus::Unavailable:
                outgoing->backpressured = false;
                outgoing->retry_at = now + 100000000;
                break;
            case tcp::SubmitStatus::Disconnected:
            case tcp::SubmitStatus::TooLarge:
            case tcp::SubmitStatus::InvalidArgument:
                close(peer->id);
                return more;
            }
        }
        return more;
    }

    IRecordHandler& handler_;
    const capture::IClock& clock_;
    const HANDLE wake_;
    const std::size_t maximum_;
    std::mutex mutex_;
    std::unordered_map<ConnectionId, std::shared_ptr<Peer>> peers_;
    std::vector<std::shared_ptr<Peer>> snapshot_; // 仅产品管理线程使用，预留固定连接预算。
    std::uint64_t tag_ = 0;
};

struct Timer {
    HANDLE value = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    Timer() {
        if (!value)
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "timer");
    }
    ~Timer() { CloseHandle(value); }
};
struct Stop {
    tcp::TcpServer& server;
    ~Stop() {
        server.request_stop();
        (void)server.join();
    }
};
tcp::TcpOptions options(std::uint16_t port, std::size_t maximum, std::string_view bind) {
    tcp::TcpOptions result;
    result.port = port;
    result.maximum_connections = maximum;
    result.bind_address = bind;
    return result;
}
} // namespace

struct TcpServer::Impl {
    explicit Impl(tcp::TcpOptions value) : options(std::move(value)) {
        // 控制消息有界，线格式和网络发送队列均由共享层持有。
        options.maximum_receive_body = 4096;
        options.connection_queue.records = 1;
    }
    tcp::TcpOptions options;
};
TcpServer::TcpServer(std::uint16_t port, std::size_t maximum, std::string_view bind)
    : TcpServer(options(port, maximum, bind)) {}
TcpServer::TcpServer(tcp::TcpOptions options) : impl_(std::make_unique<Impl>(std::move(options))) {}
TcpServer::~TcpServer() = default;
void TcpServer::run(IRecordHandler& handler, const capture::IClock& clock, void* stop_event, void* wake_event,
                    StatusCallback changed) {
    Bridge bridge(handler, clock, static_cast<HANDLE>(wake_event), impl_->options.maximum_connections);
    tcp::TcpServer server(impl_->options, bridge);
    bridge.server = &server;
    Stop stop{server};
    if (const auto error = server.start())
        throw std::runtime_error("TCP start " + std::to_string(static_cast<unsigned>(error.code)));
    Timer timer;
    auto previous = tcp::ServiceState::Stopped;
    bool observed_running = false;
    while (WaitForSingleObject(static_cast<HANDLE>(stop_event), 0) != WAIT_OBJECT_0) {
        ResetEvent(static_cast<HANDLE>(wake_event));
        const auto status = server.status();
        if (status.state != tcp::ServiceState::Stopped)
            observed_running = true;
        if (status.state != previous) {
            previous = status.state;
            if (changed)
                changed(status); // 仅产品管理线程写日志，网络回调不做磁盘 I/O。
        }
        // start 创建线程后，第一次查询可能早于网络线程发布 Starting；不将这个初值当成退出。
        if (status.state == tcp::ServiceState::Failed ||
            (status.state == tcp::ServiceState::Stopped && observed_running))
            throw std::runtime_error("TCP service unavailable");
        if (bridge.pump())
            continue;
        LARGE_INTEGER due{};
        due.QuadPart = -static_cast<LONGLONG>((std::max)(1u, handler.wait_ms())) * 10000;
        if (!SetWaitableTimer(timer.value, &due, 0, nullptr, nullptr, FALSE))
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "arm timer");
        const HANDLE events[]{static_cast<HANDLE>(stop_event), static_cast<HANDLE>(wake_event), timer.value};
        if (WaitForMultipleObjects(3, events, FALSE, INFINITE) == WAIT_FAILED)
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "service wait");
    }
}
} // namespace valorant::transport
