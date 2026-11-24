#include "synchronization/v1/service.hpp"
#include "synchronization/v1/codec.hpp"
#include "synchronization/v1/request.hpp"
#include "synchronization/v1/schema.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>
#include <unordered_map>

namespace valorant::synchronization::v1 {
namespace {
using wire::Writer;
using Stream = model::Stream;
using State = std::shared_ptr<const model::StreamState>;
struct Client {
    std::optional<Stream> stream;
    bool debug = false;
    double hz = 0;
    std::uint64_t request_id = 0, event = 0, revision = 0, config = 0, period = 0, minimum_sequence = 0;
    std::uint64_t generation = 0, epoch = 0, next_send = 0, heartbeat = 0, sent_count = 0, coalesced = 0;
    std::uint64_t candidate = 0, sampled_candidate = 0, active_mapping = 0;
    std::uint64_t rejected_sequence = 0, rejected_generation = 0;
    model::Availability availability = model::Availability::Waiting;
    std::deque<transport::Record> controls;
    State pending, sending, baseline;
    std::shared_ptr<std::atomic<std::uint64_t>> eligibility = std::make_shared<std::atomic<std::uint64_t>>(0);
};
bool same(const State& a, const State& b) {
    return a && b && a->stamp.epoch == b->stamp.epoch && a->stamp.sequence == b->stamp.sequence &&
           a->generation == b->generation;
}
} // namespace
struct Service::Impl {
    capture::IProvider& provider;
    const capture::IClock& clock;
    Identity identity;
    const std::size_t maximum_connections;
    std::unordered_map<transport::ConnectionId, Client> clients;
    Impl(capture::IProvider& p, const capture::IClock& c, Identity id, std::size_t maximum)
        : provider(p), clock(c), identity(id), maximum_connections(maximum) {
        if (!maximum || maximum > 60)
            throw wire::Error("connection budget");
    }
    transport::Record record(Client& c, unsigned kind, const Writer& body) {
        return envelope(kind, body, ++c.event, c.active_mapping ? std::optional{c.active_mapping} : std::nullopt);
    }
    void qualify(Client& c, transport::Record& record, const model::StreamView& source) {
        const auto stream = *c.stream;
        const auto generation = source.generation, epoch = source.epoch, revision = c.revision;
        record.eligible = [provider = &provider, gate = c.eligibility, stream, generation, epoch,
                           revision](bool started) {
            const auto current = gate->load(std::memory_order_acquire);
            return current && (started || current == revision) && provider->current(stream, generation, epoch);
        };
    }
    void enqueue(Client& c, transport::Record record) {
        if (c.controls.size() >= 16)
            throw wire::Error("control queue limit");
        c.controls.push_back(std::move(record));
    }
    void reject(Client& c, std::uint64_t request_id, unsigned code) {
        Writer body;
        body.uint64(1, request_id);
        body.uint32(2, code);
        enqueue(c, record(c, 17, body));
    }
    void status(Client& c, const model::StreamView& v) {
        Writer body;
        body.uint32(1, static_cast<unsigned>(v.availability));
        if (v.epoch)
            body.uint64(2, v.epoch);
        body.boolean(3, c.epoch != v.epoch);
        if (v.reason)
            body.uint32(4, v.reason);
        auto value = record(c, 16, body);
        value.source_token = v.generation;
        value.subscription_revision = c.revision;
        qualify(c, value, v);
        enqueue(c, std::move(value));
        c.availability = v.availability;
        c.generation = v.generation;
        c.epoch = v.epoch;
    }
    void configured(Client& c, transport::ConnectionId id, const Request& r) {
        const auto rate = provider.subscribe(id, *c.stream, r.hz, c.debug);
        c.hz = r.hz;
        c.period = rate.period_ns;
        c.config = rate.revision;
        ++c.revision;
        c.eligibility->store(c.revision, std::memory_order_release);
        c.minimum_sequence = provider.barrier(*c.stream) + 1;
        c.pending.reset();
        c.baseline.reset();
        c.sending.reset();
        c.next_send = 0;
        c.rejected_sequence = 0;
        c.rejected_generation = 0;
        Writer body;
        body.uint64(1, r.id);
        body.uint32(2, static_cast<unsigned>(*c.stream));
        body.uint32(3, c.debug ? 2 : 1);
        body.real(4, r.hz);
        body.uint64(5, rate.period_ns);
        body.real(6, rate.category);
        body.uint64(7, rate.revision);
        enqueue(c, record(c, 13, body));
        const auto v = provider.view(*c.stream);
        status(c, v);
    }
    void receive(transport::ConnectionId id, std::span<const std::byte> bytes, std::uint64_t received) {
        auto& c = clients.at(id);
        const auto r = request(bytes);
        if (r.id <= c.request_id) {
            reject(c, r.id, 1001);
            return;
        }
        c.request_id = r.id;
        if (r.kind == 11 || r.kind == 12) {
            if (!std::isfinite(r.hz) || r.hz < 1 || r.hz > 300) {
                reject(c, r.id, 1005);
                return;
            }
        }
        if (r.kind == 11) {
            if (r.version != 1) {
                reject(c, r.id, 1002);
                return;
            }
            if (!std::equal(r.schema.begin(), r.schema.end(), schema_sha256.begin(), schema_sha256.end())) {
                reject(c, r.id, 1003);
                return;
            }
            if (r.stream < 1 || r.stream > 3 || r.projection < 1 || r.projection > 2 ||
                (c.stream && static_cast<unsigned>(*c.stream) != r.stream)) {
                reject(c, r.id, 1004);
                return;
            }
            c.stream = static_cast<Stream>(r.stream);
            c.debug = r.projection == 2;
            configured(c, id, r);
        } else if (r.kind == 12) {
            if (!c.stream) {
                reject(c, r.id, 1001);
                return;
            }
            // 前置版本是本连接最后确认的配置，其他消费者调频不使本连接失去更新资格。
            if (r.revision != c.config) {
                reject(c, r.id, 1006);
                return;
            }
            configured(c, id, r);
        } else if (r.kind == 18) {
            if (!r.revision || r.revision < c.active_mapping) {
                reject(c, r.id, 1007);
                return;
            }
            c.candidate = r.revision;
            Writer body;
            body.uint64(1, r.id);
            body.uint64(2, r.revision);
            body.uint64(3, 1);
            if (received)
                (void)body.fixed64(4, received);
            auto value = record(c, 19, body);
            value.clock_candidate = r.revision;
            enqueue(c, std::move(value));
        } else if (r.kind == 20) {
            if (r.clock != 1 || !r.revision || r.revision != c.candidate || r.revision != c.sampled_candidate ||
                r.revision < c.active_mapping) {
                reject(c, r.id, 1007);
                return;
            }
            c.active_mapping = r.revision;
            Writer body;
            body.uint64(1, r.id);
            body.uint64(2, r.revision);
            enqueue(c, record(c, 21, body));
        } else if (r.kind == 22) {
            if (!c.stream) {
                reject(c, r.id, 1001);
                return;
            }
            ++c.revision;
            c.eligibility->store(c.revision, std::memory_order_release);
            c.pending.reset();
            c.baseline.reset();
            c.sending.reset();
            c.next_send = 0;
            c.minimum_sequence = provider.barrier(*c.stream) + 1;
            // 等下一次新采样建立基线，避免在同连接重复已完整发送的采样序号。
        }
    }
    void poll(transport::ConnectionId id) {
        auto& c = clients.at(id);
        if (!c.stream)
            return;
        const auto v = provider.view(*c.stream);
        if (v.state && v.state->generation == c.rejected_generation && v.state->stamp.sequence == c.rejected_sequence &&
            v.availability == model::Availability::Ready)
            return;
        if (c.generation != v.generation || c.epoch != v.epoch || c.availability != v.availability) {
            if (c.epoch && c.epoch != v.epoch)
                c.minimum_sequence = 1;
            if (c.generation != v.generation || c.epoch != v.epoch) {
                c.pending.reset();
                c.baseline.reset();
                c.sending.reset();
            }
            status(c, v);
        }
        const auto& s = v.state;
        if (v.availability != model::Availability::Ready || !s || s->stamp.sequence < c.minimum_sequence ||
            (c.debug && !s->diagnostic) || same(s, c.pending) || same(s, c.sending) || same(s, c.baseline))
            return;
        if (c.pending)
            ++c.coalesced;
        c.pending = s;
    }
    std::optional<transport::Record> next(transport::ConnectionId id) {
        auto& c = clients.at(id);
        if (!c.controls.empty()) {
            auto value = std::move(c.controls.front());
            c.controls.pop_front();
            return value;
        }
        const auto now = capture::nanoseconds(clock);
        if (c.pending && now >= c.next_send) {
            auto state = std::move(c.pending);
            try {
                const auto data = update(*state, c.baseline.get());
                auto value = record(c, c.debug ? 15 : 14, c.debug ? diagnostic(*state, data) : data);
                value.source_token = state->generation;
                value.subscription_revision = c.revision;
                value.capture_sequence = state->stamp.sequence;
                auto source = provider.view(*c.stream);
                source.generation = state->generation;
                source.epoch = state->stamp.epoch;
                qualify(c, value, source);
                c.sending = std::move(state);
                return value;
            } catch (const wire::Error&) {
                c.baseline.reset();
                c.sending.reset();
                c.rejected_sequence = state->stamp.sequence;
                c.rejected_generation = state->generation;
                auto failed = provider.view(*c.stream);
                failed.availability = model::Availability::Unavailable;
                failed.reason = 1008;
                status(c, failed);
                auto value = std::move(c.controls.front());
                c.controls.pop_front();
                return value;
            }
        }
        if (now >= c.heartbeat) {
            c.heartbeat = now + 1000000000;
            Writer body;
            body.uint64(2, c.coalesced);
            body.uint64(7, c.sent_count);
            if (c.stream) {
                const auto v = provider.view(*c.stream);
                if (v.epoch) {
                    body.uint64(3, v.epoch);
                    body.uint64(4, v.completed);
                    body.uint64(5, v.failed);
                    body.uint64(6, v.missed);
                    if (v.state)
                        body.uint64(1, v.state->stamp.sequence);
                }
            }
            return record(c, 23, body);
        }
        return {};
    }
};
Service::Service(capture::IProvider& p, const capture::IClock& c, Identity id, std::size_t maximum)
    : impl_(std::make_unique<Impl>(p, c, id, maximum)) {}
Service::~Service() = default;
void Service::tick() {
    impl_->provider.step();
}
unsigned Service::wait_ms() const {
    return impl_->provider.wait_ms();
}
void Service::connected(transport::ConnectionId id) {
    if (impl_->clients.size() >= impl_->maximum_connections)
        throw wire::Error("connection budget");
    auto [it, inserted] = impl_->clients.try_emplace(id);
    if (!inserted)
        throw wire::Error("duplicate connection");
    auto& c = it->second;
    Writer body, clock;
    body.uint32(1, 1);
    (void)body.blob(2, schema_sha256);
    (void)body.blob(3, impl_->identity.instance);
    (void)clock.blob(1, impl_->identity.clock_domain);
    (void)clock.fixed64(2, impl_->clock.frequency());
    clock.uint64(3, 1);
    body.message(4, clock);
    const std::array supported{std::byte{1}, std::byte{2}, std::byte{3}};
    (void)body.blob(5, supported);
    body.boolean(6, true);
    body.boolean(7, true);
    body.real(8, 300.0);
    impl_->enqueue(c, impl_->record(c, 10, body));
}
void Service::received(transport::ConnectionId id, std::span<const std::byte> bytes, std::uint64_t received) {
    impl_->receive(id, bytes, received);
}
void Service::poll(transport::ConnectionId id) {
    impl_->poll(id);
}
std::optional<transport::Record> Service::next(transport::ConnectionId id) {
    return impl_->next(id);
}
bool Service::valid(transport::ConnectionId id, const transport::Record& record, bool started) {
    (void)id;
    return !record.eligible || record.eligible(started);
}
void Service::sent(transport::ConnectionId id, const transport::Record& record) {
    auto& c = impl_->clients.at(id);
    if (record.clock_candidate && *record.clock_candidate == c.candidate)
        c.sampled_candidate = *record.clock_candidate;
    if (record.capture_sequence && record.subscription_revision == c.revision && c.sending &&
        record.capture_sequence == c.sending->stamp.sequence && valid(id, record, true)) {
        c.baseline = std::move(c.sending);
        ++c.sent_count;
        c.next_send = capture::nanoseconds(impl_->clock) + c.period;
    }
}
void Service::disconnected(transport::ConnectionId id) noexcept {
    const auto client = impl_->clients.find(id);
    if (client != impl_->clients.end())
        client->second.eligibility->store(0, std::memory_order_release);
    try {
        impl_->provider.unsubscribe(id);
    } catch (...) {
    }
    impl_->clients.erase(id);
}
void Service::discarded(transport::ConnectionId id, const transport::Record& record) {
    auto& c = impl_->clients.at(id);
    if (record.capture_sequence && record.subscription_revision == c.revision && c.sending &&
        record.capture_sequence == c.sending->stamp.sequence) {
        c.sending.reset();
        c.baseline.reset();
    }
}
} // namespace valorant::synchronization::v1
