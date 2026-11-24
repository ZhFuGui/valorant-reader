#include "game/valorant/capture_service.hpp"
#include "game/valorant/entity_catalog.hpp"
#include "game/valorant/pose.hpp"
#include "game/valorant/position.hpp"
#include "game/valorant/view.hpp"
#include "memory/read_error.hpp"
#include "platform/windows/pe_image.hpp"
#include "runtime/binding_health.hpp"
#include "runtime/recovery.hpp"
#include "runtime/work_pool.hpp"
#include <algorithm>
#include <atomic>
#include <limits>

namespace valorant::game {
namespace {
using Status = ResolutionStatus;
using Stream = model::Stream;
enum Job : std::size_t { Directory, Classify, Positions, Skeletons, View, Bind, Check, JobCount };
struct Binding {
    std::shared_ptr<memory::ISession> session;
    ModuleView module;
    std::uint64_t serial = 0;
    bool compatible = false;
};
struct Roster {
    std::uint64_t generation = 0;
    std::vector<RosterEntry> entries;
};
struct Result {
    Job job = Bind;
    std::uint64_t epoch = 0, started_ns = 0, sequence = 0, revision = 0, period = 0, directory_generation = 0,
                  generation = 0;
    std::uint64_t occupied_ns = 0;
    long double estimated = 0, weight = 1;
    bool diagnostic = false, error = false;
    Status status = Status::ReadUnavailable;
    memory::ProcessState process_state = memory::ProcessState::Unavailable;
    unsigned uncertain_checks = 0;
    std::optional<memory::ProcessIdentity> process;
    std::optional<memory::ProcessIdentity> pinned;
    std::shared_ptr<Binding> binding;
    std::shared_ptr<const ActorDirectory> directory;
    std::shared_ptr<const Roster> roster;
    std::shared_ptr<ActorDirectory> next_directory;
    std::shared_ptr<ClassificationBatch> classification;
    std::shared_ptr<model::StreamState> state;
    std::optional<WorldContext> view_context;
    std::optional<ObjectIdentity> local_character;
};
struct Ticket {
    std::atomic_bool ready = false;
    Result result;
};
struct Worker {
    std::uint64_t serial = 0;
    std::unique_ptr<memory::IReadChannel> channel;
    memory::IReadChannel& reader(const Binding& binding) {
        if (!channel || serial != binding.serial) {
            channel.reset();
            channel = binding.session->open_reader();
            serial = binding.serial;
        }
        return *channel;
    }
};
bool same_context(const ActorDirectory& a, const ActorDirectory& b) {
    return a.engine == b.engine && a.viewport == b.viewport && a.world == b.world && a.level == b.level;
}
model::Evidence evidence(std::uint64_t id, const ObjectIdentity& identity, unsigned status, unsigned phase = 0) {
    return {
        id, identity.address, identity.type, identity.outer, identity.index, identity.weak_serial, status, phase, {},
        {}};
}
} // namespace

struct CaptureService::Impl {
    std::unique_ptr<memory::IBackend> backend;
    const std::optional<std::uint32_t> pid;
    const capture::IClock& clock;
    const Profile& profile = current_profile();
    capture::Demand demand;
    EntityCatalog catalog;
    std::optional<memory::ProcessIdentity> pinned;
    std::shared_ptr<Binding> binding;
    std::shared_ptr<const ActorDirectory> directory;
    std::shared_ptr<const Roster> roster;
    std::optional<WorldContext> context;
    std::array<Worker, 2> workers;
    std::vector<std::shared_ptr<Ticket>> tickets;
    std::array<bool, JobCount> busy{};
    std::array<capture::Timeline, 5> timelines;
    std::array<model::StreamView, 3> streams;
    std::array<std::uint64_t, 3> sequences{};
    std::array<long double, 3> service{};
    std::array<long double, 3> cost{1000000, 2000000, 1000000};
    std::uint64_t epoch = 1, binding_serial = 0, directory_generation = 0, classification_generation = 0;
    std::uint64_t next_bind = 0, next_check = 0;
    unsigned unavailable_checks = 0;
    runtime::RecoveryBackoff bind_recovery, check_recovery;
    bool classification_ready = false, binding_healthy = false, selected_retired = false;
    runtime::WorkPool pool; // 最后声明、最先析构：先等待返回，再释放通道、会话和票据。
    Impl(std::unique_ptr<memory::IBackend> b, std::optional<std::uint32_t> p, const capture::IClock& c,
         std::function<void()> wake)
        : backend(std::move(b)), pid(p), clock(c), pool(std::move(wake)) {
        tickets.reserve(2);
    }
    void invalidate(Stream s) {
        auto& v = streams[model::stream_index(s)];
        if (v.availability != model::Availability::Unavailable || v.state)
            ++v.generation;
        v.availability = model::Availability::Unavailable;
        v.reason = 1009;
        v.epoch = epoch;
        v.state.reset();
    }
    void reset(bool session) {
        ++epoch;
        directory.reset();
        roster.reset();
        context.reset();
        catalog.clear();
        classification_ready = false;
        for (unsigned i = 0; i < 3; ++i) {
            invalidate(static_cast<Stream>(i + 1));
            sequences[i] = 0;
            service[i] = 0;
            streams[i].completed = streams[i].failed = streams[i].missed = 0;
        }
        for (auto& t : timelines)
            t = {};
        if (session) {
            binding.reset();
            binding_healthy = false;
        }
    }
    void unavailable() {
        directory.reset();
        roster.reset();
        classification_ready = false;
        for (unsigned i = 0; i < streams.size(); ++i)
            invalidate(static_cast<Stream>(i + 1));
    }
    void waiting() {
        for (auto& stream : streams) {
            if (stream.availability != model::Availability::Waiting || stream.state)
                ++stream.generation;
            stream.availability = model::Availability::Waiting;
            stream.epoch = epoch;
            stream.reason = 1009;
            stream.state.reset();
        }
    }
    void retired(std::uint64_t now) {
        reset(true);
        if (!pid) {
            pinned.reset();
            waiting();
        } else
            selected_retired = true; // 显式 PID 固定实例，不能把复用此数字的新进程当旧进程。
        next_bind = now + 500000000;
    }
    void configure(std::uint64_t now) {
        timelines[Directory].configure(capture::Demand::period(demand.foundation() + 1), now);
        timelines[Classify].configure(capture::Demand::period(demand.foundation()), now);
        for (unsigned i = 0; i < 3; ++i) {
            auto hz = demand.maximum(static_cast<Stream>(i + 1));
            if (hz && !timelines[i + 2].period) {
                // 新激活类别从当前服务水位开始，不积攒空闲期间的无限信用。
                long double floor = 0;
                bool found = false;
                for (unsigned j = 0; j < 3; ++j)
                    if (timelines[j + 2].period) {
                        floor = found ? (std::min)(floor, service[j]) : service[j];
                        found = true;
                    }
                service[i] = floor;
            }
            timelines[i + 2].configure(hz ? capture::Demand::period(hz) : 0, now);
        }
    }
    void sample(Result& out, Worker& worker) {
        auto& reader = worker.reader(*out.binding);
        std::vector<ObjectIdentity> inputs;
        if (out.roster)
            for (const auto& row : out.roster->entries)
                if (row.identity)
                    inputs.push_back(*row.identity);
        auto state = std::make_shared<model::StreamState>();
        state->stream = static_cast<Stream>(out.job - 1);
        state->config_revision = out.revision;
        state->period_ns = out.period;
        std::shared_ptr<model::Diagnostics> diag;
        if (out.diagnostic) {
            diag = std::make_shared<model::Diagnostics>();
            diag->directory_generation = out.directory_generation;
            if (out.job != View && out.roster) {
                diag->classification_generation = out.roster->generation;
                diag->planned = out.roster->entries.size();
            }
            if (out.job != View && out.directory) {
                const auto& c = out.directory->coverage;
                diag->slots = c.slots;
                diag->empty = c.empty;
                diag->invalid = c.invalid;
                diag->duplicates = c.duplicates;
            }
            if (out.job == View) {
                diag->directory_generation = 0;
                diag->planned = 1;
            }
        }
        if (out.job == Positions) {
            auto raw = PositionReader(reader, profile, out.binding->module, clock)
                           .read(*out.directory, inputs, out.sequence, out.epoch);
            out.status = raw.source_status;
            if (out.status != Status::Complete)
                return;
            state->stamp = raw.stamp;
            model::PositionContext context_value;
            if (raw.context.map.complete())
                context_value.map = std::move(raw.context.map.value);
            if (raw.context.local_character.complete()) {
                context_value.local.state = raw.context.local_character.value ? model::LocalCharacterState::Unresolved
                                                                              : model::LocalCharacterState::Absent;
                out.local_character = raw.context.local_character.value;
            }
            if (raw.reference_team.complete())
                context_value.reference_team = raw.reference_team.value.identity;
            state->position_context = std::move(context_value);
            std::vector<model::EntityPosition> values;
            std::size_t index = 0;
            for (const auto& row : out.roster->entries) {
                model::EntityPosition value{row.id, {}};
                if (row.identity) {
                    const auto& data = raw.characters[index++];
                    if (data.position.complete())
                        value.value = data.position.value;
                    if (data.team.complete()) {
                        value.team = data.team.value.identity;
                        if (raw.reference_team.complete())
                            value.relationship = TeamReader::relationship(data.team.value, raw.reference_team.value);
                    }
                    if (out.local_character && *out.local_character == data.identity)
                        value.relationship = model::Relationship::Self;
                    if (diag)
                        diag->entities.push_back(
                            evidence(row.id, data.identity, static_cast<unsigned>(data.position.status)));
                }
                values.push_back(value);
            }
            state->value = std::move(values);
        } else if (out.job == Skeletons) {
            auto raw = PoseReader(reader, profile, out.binding->module, clock)
                           .read(*out.directory, inputs, out.sequence, out.epoch);
            out.status = raw.source_status;
            if (out.status != Status::Complete)
                return;
            state->stamp = raw.stamp;
            std::vector<model::EntitySkeleton> values;
            std::size_t index = 0;
            if (diag)
                diag->metadata_retries = raw.metadata_source_retries;
            for (const auto& row : out.roster->entries) {
                model::EntitySkeleton value{row.id, {}};
                if (row.identity) {
                    const auto& data = raw.characters[index++];
                    if (data.skeleton &&
                        std::any_of(data.skeleton->world_positions.begin(), data.skeleton->world_positions.end(),
                                    [](const auto& p) { return p.has_value(); }))
                        value.value = data.skeleton->world_positions;
                    if (diag) {
                        auto e = evidence(row.id, data.identity, static_cast<unsigned>(data.status),
                                          static_cast<unsigned>(data.failed_at));
                        if (data.skeleton) {
                            e.leader = data.leader_pose;
                            for (auto status : data.joint_status)
                                e.joints.push_back(static_cast<unsigned>(status));
                        }
                        diag->entities.push_back(std::move(e));
                    }
                }
                values.push_back(std::move(value));
            }
            state->value = std::move(values);
        } else {
            auto raw = ViewReader(reader, profile, out.binding->module, clock).read(out.sequence, out.epoch);
            out.status = raw.camera.status;
            if (out.status != Status::Complete)
                return;
            state->stamp = raw.stamp;
            state->value = std::optional<model::Camera>{raw.camera.value};
            out.view_context = raw.context;
        }
        state->diagnostic = std::move(diag);
        state->stamp.finished_qpc = clock.now();
        out.state = std::move(state);
    }
    void execute(Result& out, std::size_t index) {
        if (out.job == Bind) {
            out.process = out.pinned ? out.pinned : backend->find_process(pid);
            if (!out.process)
                return;
            try {
                auto candidate = std::make_shared<Binding>();
                candidate->session = backend->attach(*out.process);
                if (!candidate->session || candidate->session->identity() != *out.process)
                    throw memory::ReadUnavailable("Binding did not retain the selected process identity");
                auto channel = candidate->session->open_reader();
                const auto pe = platform::windows::read_pe_image(*channel, candidate->session->image_base());
                if (!pe)
                    return;
                candidate->module = {
                    pe->base, "VALORANT-Win64-Shipping.exe", {pe->timestamp, pe->image_bytes, pe->checksum}};
                candidate->compatible = profile.image && *profile.image == candidate->module.identity;
                out.process_state = candidate->session->state();
                if (out.process_state != memory::ProcessState::Current)
                    throw memory::ReadUnavailable("Binding changed before publication");
                candidate->serial = out.sequence;
                out.binding = std::move(candidate);
                out.status = out.binding->compatible ? Status::Complete : Status::BuildMismatch;
            } catch (const std::runtime_error&) {
                // attach 失败不回到按名称枚举；只独立确认已选实例是否真的消失。
                try {
                    const auto current = backend->find_process(out.process->pid);
                    if (!current)
                        out.process_state = memory::ProcessState::Exited;
                    else if (*current != *out.process)
                        out.process_state = memory::ProcessState::Replaced;
                } catch (const std::runtime_error&) {
                }
                throw;
            }
            return;
        }
        if (out.job == Check) {
            const auto health = runtime::observe_binding(*backend, *out.binding->session, out.uncertain_checks);
            out.process_state = health.state;
            out.uncertain_checks = health.uncertain_checks;
            return;
        }
        auto& reader = workers[index].reader(*out.binding);
        if (out.job == Directory) {
            auto value = DirectoryReader(reader, profile, out.binding->module).read();
            out.status = value.status;
            if (value.complete())
                out.next_directory = std::make_shared<ActorDirectory>(std::move(value.value));
        } else if (out.job == Classify) {
            out.classification = std::make_shared<ClassificationBatch>(
                ClassificationReader(reader, profile, out.binding->module).read(*out.directory));
            out.status = out.classification->source_status;
        } else
            sample(out, workers[index]);
    }
    bool dispatch(Job job, std::uint64_t now) {
        if (busy[job] || pool.busy() >= 2)
            return false;
        auto ticket = std::make_shared<Ticket>();
        auto& out = ticket->result;
        out.job = job;
        out.epoch = epoch;
        out.started_ns = now;
        out.binding = binding;
        out.directory = directory;
        out.roster = roster;
        out.pinned = pinned;
        out.uncertain_checks = unavailable_checks;
        out.directory_generation = directory_generation;
        out.revision = demand.revision();
        if (job == Bind)
            out.sequence = binding_serial + 1;
        if (job >= Positions && job <= View) {
            const auto i = job - Positions;
            out.sequence = sequences[i] + 1;
            out.period = timelines[job].period;
            out.diagnostic = demand.diagnostic(static_cast<Stream>(i + 1));
            out.generation = streams[i].generation;
            out.estimated = cost[i];
            out.weight =
                (std::max)(1.0L, static_cast<long double>(demand.maximum(static_cast<Stream>(i + 1))) * cost[i]);
        }
        runtime::WorkPool::Work work = [this, ticket](std::size_t worker) noexcept {
            const auto started = capture::nanoseconds(clock);
            try {
                execute(ticket->result, worker);
            } catch (...) {
                ticket->result.error = true;
                workers[worker].channel.reset(); // 仅本 worker 在返回边界释放自己的通道。
            }
            ticket->result.occupied_ns = capture::nanoseconds(clock) - started;
            ticket->ready.store(true, std::memory_order_release);
        };
        // 所有可能分配的输入和回调先准备完，再登记 busy 与消耗机会。
        tickets.push_back(ticket);
        busy[job] = true;
        if (job == Bind)
            ++binding_serial;
        if (job >= Positions && job <= View) {
            ++sequences[job - Positions];
            service[job - Positions] += out.estimated * 1000000000.0L / out.weight;
        }
        if (job <= View)
            timelines[job].pending = false;
        const bool queued = pool.submit(std::move(work));
        if (!queued) {
            busy[job] = false;
            tickets.pop_back();
            if (job <= View)
                timelines[job].pending = true;
            if (job == Bind)
                --binding_serial;
            if (job >= Positions && job <= View) {
                --sequences[job - Positions];
                service[job - Positions] -= out.estimated * 1000000000.0L / out.weight;
            }
            return false;
        }
        return true;
    }
    void commit(Result& out, std::uint64_t now) {
        if (out.epoch != epoch)
            return;
        if (out.job >= Positions && out.job <= View) {
            const auto i = out.job - Positions;
            service[i] += (static_cast<long double>(out.occupied_ns) - out.estimated) * 1000000000.0L / out.weight;
            cost[i] = cost[i] * 0.8L + static_cast<long double>(out.occupied_ns) * 0.2L;
            if (out.generation != streams[i].generation) {
                ++streams[i].failed;
                return;
            }
        }
        if (out.job == Bind) {
            if (!pinned && out.process)
                pinned = out.process;
            if (out.binding && !out.error && (out.status == Status::Complete || out.status == Status::BuildMismatch)) {
                binding = std::move(out.binding);
                binding_healthy = true;
                bind_recovery.progress(now);
                unavailable_checks = 0;
                next_check = now + 1000000000;
                if (!binding->compatible)
                    unavailable(); // 不匹配时持有实例，只观察生命周期，不反复重开采集器。
            } else if (out.process_state == memory::ProcessState::Exited ||
                       out.process_state == memory::ProcessState::Replaced) {
                retired(now);
            } else if (!out.error && !out.process) {
                waiting();
                next_bind = now + 500000000; // 正常等待游戏，非故障退避；有订阅也不制造空采样。
                bind_recovery.progress(now);
            } else {
                unavailable();
                next_bind = bind_recovery.retry_at(now);
            }
            return;
        }
        if (out.job == Check) {
            unavailable_checks = out.error ? unavailable_checks : out.uncertain_checks;
            if (!out.error && out.process_state == memory::ProcessState::Current) {
                binding_healthy = true;
                bind_recovery.progress(now);
                check_recovery.progress(now);
                next_check = now + 1000000000;
            } else if (!out.error && (out.process_state == memory::ProcessState::Exited ||
                                      out.process_state == memory::ProcessState::Replaced)) {
                retired(now);
            } else if (!out.error && out.process_state == memory::ProcessState::AddressSpaceChanged) {
                reset(true);
                next_bind = now + 100000000;
            } else {
                binding_healthy = false;
                unavailable();
                next_check = check_recovery.retry_at(now);
            }
            return;
        }
        if (out.error || out.status != Status::Complete) {
            if (out.job == Directory || out.job == Classify) {
                classification_ready = false;
                invalidate(Stream::Positions);
                invalidate(Stream::Skeletons);
            }
            if (out.job >= Positions) {
                auto s = static_cast<Stream>(out.job - 1);
                ++streams[model::stream_index(s)].failed;
                invalidate(s);
            }
            return;
        }
        if (out.job == Directory) {
            const WorldContext next{out.next_directory->engine, out.next_directory->viewport,
                                    out.next_directory->world};
            if ((context && *context != next) || (directory && !same_context(*directory, *out.next_directory)))
                reset(false);
            context = next;
            directory = std::move(out.next_directory);
            ++directory_generation;
            catalog.retain(*directory);
            return;
        }
        if (out.job == Classify) {
            if (!directory || !same_context(*directory, *out.directory))
                return;
            auto next = std::make_shared<Roster>();
            next->generation = ++classification_generation;
            next->entries = catalog.classify(*out.classification);
            roster = std::move(next);
            classification_ready = true;
            return;
        }
        if (!out.state)
            return;
        if (out.job == View && out.view_context) {
            if (context && *context != *out.view_context) {
                reset(false);
                context = out.view_context;
                return;
            }
            context = out.view_context;
        }
        const auto i = out.job - Positions;
        if (out.job == Positions) {
            std::erase_if(std::get<std::vector<model::EntityPosition>>(out.state->value),
                          [&](const auto& v) { return !catalog.contains(v.id); });
            if (out.local_character && out.state->position_context) {
                auto& local = out.state->position_context->local;
                const auto id = catalog.find(*out.local_character);
                const auto& values = std::get<std::vector<model::EntityPosition>>(out.state->value);
                // 名单或身份在读批期间已经更新时，不把旧地址映射成新生命周期。
                if (id && std::any_of(values.begin(), values.end(), [&](const auto& v) { return v.id == *id; })) {
                    local.state = model::LocalCharacterState::Resolved;
                    local.entity_id = id;
                }
            }
            if (out.state->position_context) {
                const auto& local = out.state->position_context->local;
                for (auto& value : std::get<std::vector<model::EntityPosition>>(out.state->value)) {
                    if (local.entity_id == value.id)
                        value.relationship = model::Relationship::Self;
                    else if (value.relationship == model::Relationship::Self)
                        value.relationship = model::Relationship::Unknown;
                }
            }
        }
        if (out.job == Skeletons)
            std::erase_if(std::get<std::vector<model::EntitySkeleton>>(out.state->value),
                          [&](const auto& v) { return !catalog.contains(v.id); });
        auto& v = streams[i];
        out.state->generation = v.generation;
        out.state->stamp.finished_qpc = clock.now();
        v.state = std::move(out.state);
        v.availability = model::Availability::Ready;
        v.reason = 0;
        v.epoch = epoch;
        ++v.completed;
    }
    void step() {
        const auto now = capture::nanoseconds(clock);
        for (auto it = tickets.begin(); it != tickets.end();) {
            if (!(*it)->ready.load(std::memory_order_acquire)) {
                ++it;
                continue;
            }
            auto completed = *it;
            it = tickets.erase(it);
            auto& result = completed->result;
            busy[result.job] = false;
            try {
                commit(result, now);
            } catch (...) {
                // 所有权先收回，提交失败不会使该类别永久 busy，也不会重新提交同一票据。
                if (result.job == Directory || result.job == Classify) {
                    classification_ready = false;
                    invalidate(Stream::Positions);
                    invalidate(Stream::Skeletons);
                } else if (result.job >= Positions && result.job <= View) {
                    ++streams[result.job - Positions].failed;
                    invalidate(static_cast<Stream>(result.job - 1));
                } else if (result.job == Bind)
                    next_bind = now + 100000000;
            }
        }
        configure(now);
        if (!binding) {
            if (!selected_retired && tickets.empty() && now >= next_bind)
                dispatch(Bind, now);
            return;
        }
        for (auto& timer : timelines)
            timer.advance(now);
        for (unsigned i = 0; i < 3; ++i) {
            streams[i].missed += timelines[i + 2].missed;
            timelines[i + 2].missed = 0;
        }
        while (pool.busy() < 2) {
            if (now >= next_check && !busy[Check]) {
                if (!dispatch(Check, now))
                    break;
                continue;
            }
            if (!binding_healthy || !binding->compatible)
                break;
            if (timelines[Directory].pending && !busy[Directory]) {
                if (!dispatch(Directory, now))
                    break;
                continue;
            }
            if (timelines[Classify].pending && !busy[Classify] && directory) {
                if (!dispatch(Classify, now))
                    break;
                continue;
            }
            std::optional<Job> selected;
            for (unsigned i = 0; i < 3; ++i) {
                const auto job = static_cast<Job>(i + 2);
                if (!timelines[job].pending || busy[job] || (job != View && (!roster || !classification_ready)))
                    continue;
                if (!selected || service[i] < service[*selected - Positions])
                    selected = job;
            }
            if (!selected || !dispatch(*selected, now))
                break;
        }
    }
};

CaptureService::CaptureService(std::unique_ptr<memory::IBackend> b, std::optional<std::uint32_t> pid,
                               const capture::IClock& clock, std::function<void()> wake)
    : impl_(std::make_unique<Impl>(std::move(b), pid, clock, std::move(wake))) {}
CaptureService::~CaptureService() = default;
capture::Rate CaptureService::subscribe(std::uint64_t id, Stream stream, double hz, bool debug) {
    auto result = impl_->demand.set(id, stream, hz, debug);
    impl_->configure(capture::nanoseconds(impl_->clock));
    return result;
}
void CaptureService::unsubscribe(std::uint64_t id) {
    impl_->demand.erase(id);
    impl_->configure(capture::nanoseconds(impl_->clock));
}
model::StreamView CaptureService::view(Stream stream) const {
    return impl_->streams[model::stream_index(stream)];
}
std::uint64_t CaptureService::barrier(Stream stream) const {
    return impl_->sequences[model::stream_index(stream)];
}
void CaptureService::step() {
    impl_->step();
}
unsigned CaptureService::wait_ms() const {
    return 2;
}
} // namespace valorant::game
