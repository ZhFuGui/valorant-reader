#include "game/valorant/classification.hpp"
#include "game/valorant/reading.hpp"
#include "game/valorant/types.hpp"
#include <algorithm>
#include <array>
#include <utility>

namespace valorant::game {
namespace {
using memory::Address;
using Status = ResolutionStatus;

ClassificationState failure(Status status) noexcept {
    switch (status) {
    case Status::ReadUnavailable: return ClassificationState::ReadUnavailable;
    case Status::SourceChanged: return ClassificationState::SourceChanged;
    case Status::NotConfigured:
    case Status::BuildMismatch:
    case Status::UnsupportedCode: return ClassificationState::Unsupported;
    default: return ClassificationState::InvalidData;
    }
}

// 分类只维护 Actor 业务规则，类型来源与元类型核验由通用目录负责。
class ActorTypes {
  public:
    ActorTypes(memory::IReadChannel& channel, const offsets::Table& layout, const ObjectReader& objects,
               const RootResolver& roots)
        : types_(channel, layout.identity, objects, roots), roots_(roots), slots_{
              &layout.identity.type.native_class, &layout.actor.base.native_class, &layout.actor.pawn.native_class,
              &layout.actor.character.native_class, &layout.gameplay.character.native_class} {}

    Status initialize() {
        const auto initialized = types_.initialize();
        if (initialized != Status::Complete)
            return initialized;
        anchors_[0] = types_.native_class();
        for (std::size_t i = 1; i < slots_.size(); ++i) {
            const auto value = roots_.pointer_slot(*slots_[i]);
            if (!value.complete())
                return value.status;
            if (std::find(anchors_.begin(), anchors_.begin() + i, value.value) != anchors_.begin() + i)
                return Status::InvalidData;
            anchors_[i] = value.value;
            const auto& record = types_.observe(value.value);
            if (record.status != Status::Complete)
                return record.status;
            if (record.identity.type != anchors_[0] || (i >= 2 && record.super != anchors_[i - 1]))
                return Status::InvalidData;
        }
        return Status::Complete;
    }

    Resolution<ActorKind> classify(Address type) {
        ActorKind kind = ActorKind::OtherActor;
        std::vector<Address> seen;
        while (type) {
            if (std::find(seen.begin(), seen.end(), type) != seen.end())
                return {Status::InvalidData};
            seen.push_back(type);
            const auto& record = types_.observe(type);
            if (record.status != Status::Complete)
                return {record.status};
            if (kind == ActorKind::OtherActor) {
                if (type == anchors_[4])
                    kind = ActorKind::ShooterCharacter;
                else if (type == anchors_[3])
                    kind = ActorKind::Character;
                else if (type == anchors_[2])
                    kind = ActorKind::Pawn;
            }
            if (type == anchors_[1])
                return {Status::Complete, kind};
            type = record.super;
        }
        return {Status::Complete, ActorKind::Count};
    }
    Address default_object(Address type) { return types_.observe(type).default_object; }
    std::size_t size() const noexcept { return types_.size(); }
    void close() { shared_ = types_.close(); }
    Status confirm_anchors() const {
        if (shared_ != Status::Complete)
            return shared_;
        for (std::size_t i = 1; i < anchors_.size(); ++i) {
            const auto closing = roots_.pointer_slot(*slots_[i]);
            if (!closing.complete())
                return closing.status;
            if (closing.value != anchors_[i])
                return Status::SourceChanged;
            const auto status = types_.confirm_chain(anchors_[i], anchors_[1]);
            if (status != Status::Complete)
                return status;
        }
        return Status::Complete;
    }
    Status confirm_chain(Address type, bool is_actor) const { return types_.confirm_chain(type, is_actor ? anchors_[1] : 0); }

  private:
    TypeCatalog types_;
    const RootResolver& roots_;
    std::array<const offsets::Root*, 5> slots_;
    std::array<Address, 5> anchors_{};
    Status shared_ = Status::NotConfigured;
};

void summarize(ClassificationBatch& batch) noexcept {
    batch.terminal_counts.fill(0);
    batch.kind_counts.fill(0);
    for (auto& actor : batch.actors) {
        if (actor.state != ClassificationState::Classified) {
            actor.kind.reset();
            actor.identity.reset();
        }
        ++batch.terminal_counts[static_cast<std::size_t>(actor.state)];
        if (actor.kind)
            ++batch.kind_counts[static_cast<std::size_t>(*actor.kind)];
    }
}
} // namespace

ClassificationBatch ClassificationReader::read(const ActorDirectory& directory) const {
    ClassificationBatch result;
    result.planned = directory.coverage.unique;
    result.actors.reserve(result.planned);
    for (std::size_t i = 0; i < directory.slots.size(); ++i)
        if (directory.slots[i].state == ActorSlotState::Unique)
            result.actors.push_back({static_cast<std::uint32_t>(i), directory.slots[i].address});
    auto finish = [&](Status status) {
        result.source_status = status;
        summarize(result);
        return std::move(result);
    };
    if (result.actors.size() != result.planned)
        return finish(Status::InvalidData);
    DirectoryReader context(channel_, profile_, image_);
    if (!result.planned)
        return finish(context.confirm_context(directory));
    const auto& flag = profile_.layout.actor.base.being_destroyed;
    if (!flag.storage.value || !flag.mask || !*flag.mask)
        return finish(Status::NotConfigured);
    RootResolver roots(channel_, profile_, image_);
    const auto storage = roots.object_storage();
    if (!storage.complete())
        return finish(storage.status);
    const auto registry = read_object_registry(channel_, profile_.layout.identity, storage.value);
    if (!registry.complete())
        return finish(registry.status);
    ObjectReader objects(channel_, profile_.layout.identity, registry.value);
    ActorTypes types(channel_, profile_.layout, objects, roots);
    const auto initialized = types.initialize();
    result.type_records = types.size();
    if (initialized != Status::Complete)
        return finish(initialized);
    std::vector<Address> addresses;
    addresses.reserve(result.actors.size());
    for (const auto& actor : result.actors) addresses.push_back(actor.address);
    std::vector<Resolution<ObjectIdentity>> identities(addresses.size());
    objects.observe_many(addresses, identities);
    std::vector<std::uint8_t> native_actor(result.actors.size(), 0);
    std::vector<std::uint8_t> flags(result.actors.size());
    std::vector<memory::ReadRequest> flag_reads;
    std::vector<std::size_t> flag_actors;
    flag_reads.reserve(result.actors.size());
    flag_actors.reserve(result.actors.size());
    for (std::size_t i = 0; i < result.actors.size(); ++i) {
        auto& actor = result.actors[i];
        const auto& identity = identities[i];
        if (!identity.complete()) {
            actor.state = failure(identity.status);
            continue;
        }
        const auto kind = types.classify(identity.value.type);
        if (!kind.complete()) {
            actor.state = failure(kind.status);
            continue;
        }
        actor.identity = identity.value;
        native_actor[i] = kind.value != ActorKind::Count;
        actor.state = kind.value == ActorKind::Count ? ClassificationState::NotActor : ClassificationState::Classified;
        if (actor.state == ClassificationState::Classified) {
            if (types.default_object(identity.value.type) == actor.address) {
                actor.state = ClassificationState::DefaultObject;
            } else {
                actor.kind = kind.value;
                flag_actors.push_back(i);
                flag_reads.push_back(detail::field_read(actor.address, flag.storage, &flags[i], sizeof(flags[i])));
            }
        }
    }
    if (!flag_reads.empty()) channel_.read_many(flag_reads);
    for (std::size_t i = 0; i < flag_reads.size(); ++i) {
        auto& actor = result.actors[flag_actors[i]];
        if (!flag_reads[i].result.complete(sizeof(std::uint8_t)))
            actor.state = ClassificationState::ReadUnavailable;
        else if (flags[flag_actors[i]] & *flag.mask)
            actor.state = ClassificationState::Destroying;
    }
    // 只关闭真正用于分类的身份；失败类型不发布结果，不为其制造成功证明。
    std::vector<ObjectIdentity> closing_inputs;
    std::vector<std::size_t> closing_actors;
    closing_inputs.reserve(result.actors.size());
    closing_actors.reserve(result.actors.size());
    for (std::size_t i = 0; i < result.actors.size(); ++i)
        if (result.actors[i].identity) {
            closing_inputs.push_back(*result.actors[i].identity);
            closing_actors.push_back(i);
        }
    std::vector<Status> closings(closing_inputs.size());
    objects.confirm_many(closing_inputs, closings);
    for (std::size_t i = 0; i < closings.size(); ++i) {
        if (closings[i] != Status::Complete)
            result.actors[closing_actors[i]].state = failure(closings[i]);
    }
    types.close();
    result.type_records = types.size();
    for (std::size_t i = 0; i < result.actors.size(); ++i) {
        auto& actor = result.actors[i];
        if (!actor.identity)
            continue;
        const auto closing = types.confirm_chain(actor.identity->type, native_actor[i]);
        if (closing != Status::Complete)
            actor.state = failure(closing);
    }
    auto shared = types.confirm_anchors();
    const auto closing_storage = roots.object_storage();
    if (!closing_storage.complete())
        shared = closing_storage.status;
    else if (closing_storage.value != storage.value)
        shared = Status::SourceChanged;
    if (shared == Status::Complete)
        shared = confirm_object_registry(channel_, profile_.layout.identity, registry.value);
    if (shared == Status::Complete)
        shared = context.confirm_context(directory);
    if (shared != Status::Complete)
        for (auto& actor : result.actors)
            if (actor.state != ClassificationState::Unexecuted)
                actor.state = ClassificationState::SourceChanged;
    return finish(shared);
}
} // namespace valorant::game
