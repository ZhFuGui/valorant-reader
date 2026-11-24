#include "game/valorant/context.hpp"
#include "game/valorant/reading.hpp"

namespace valorant::game {
using Status = ResolutionStatus;
using memory::Address;
Resolution<ObjectMatch> ContextReader::object(Group& group, Address address, const offsets::Root& type) {
    auto result = observation_.match(address, type);
    if (result.complete())
        group.objects.push_back({result.value, &type});
    return result;
}
Resolution<Address> ContextReader::link(Group& group, Address owner, const offsets::Member& member) {
    auto value = read_reference(channel_, owner, member, true);
    if (value.complete())
        group.links.push_back({owner, &member, value.value});
    return value;
}
void ContextReader::read_map(Address world) {
    auto attempt = [&]() -> Resolution<model::MapIdentity> {
        const auto identity = object(map_, world, layout_.world.context.native_class);
        if (!identity.complete())
            return {identity.status};
        if (!identity.value.matches)
            return {Status::InvalidData};
        const auto package = object(map_, identity.value.identity.outer, layout_.world.native_package);
        if (!package.complete())
            return {package.status};
        if (!package.value.matches || package.value.identity.outer)
            return {Status::InvalidData};
        const auto pool = observation_.roots().name_pool();
        if (!pool.complete())
            return {pool.status};
        names_ = pool.value;
        NameReader reader(channel_, layout_.identity.names, pool.value);
        const auto path = reader.name(package.value.identity.name);
        if (!path.complete())
            return {path.status};
        const auto name = reader.name(identity.value.identity.name);
        if (!name.complete())
            return {name.status};
        if (path.value.empty() || path.value.front() != '/' || name.value.empty())
            return {Status::InvalidData};
        return {Status::Complete, {path.value, name.value}};
    };
    value_.map = attempt();
}
void ContextReader::read_local(Address world) {
    auto path = [&]() -> Resolution<Address> {
        const auto world_id = object(path_, world, layout_.world.context.native_class);
        if (!world_id.complete())
            return {world_id.status};
        if (!world_id.value.matches)
            return {Status::InvalidData};
        const auto instance = link(path_, world, layout_.world.context.game_instance);
        if (!instance.complete())
            return {instance.status};
        const auto instance_id = object(path_, instance.value, layout_.world.game_instance.native_class);
        if (!instance_id.complete())
            return {instance_id.status};
        if (!instance_id.value.matches)
            return {Status::InvalidData};
        if (layout_.world.game_instance.local_player_reference_bytes.value != sizeof(Address))
            return {Status::UnsupportedCode};
        players_ = detail::field_address(instance.value, layout_.world.game_instance.local_players, 16);
        const auto players = read_array_view(channel_, players_, layout_.foundation.array, sizeof(Address));
        if (!players.complete())
            return {players.status};
        if (!players.value.count)
            return {Status::Complete, 0};
        const auto player = memory::read_value<Address>(channel_, players.value.data);
        if (!player)
            return {Status::ReadUnavailable};
        player_ = *player;
        const auto player_id = object(path_, player_, layout_.view.player.native_class);
        if (!player_id.complete())
            return {player_id.status};
        if (!player_id.value.matches)
            return {Status::InvalidData};
        const auto controller = link(path_, player_, layout_.view.player.controller);
        if (!controller.complete() || !controller.value)
            return controller;
        const auto controller_id = object(path_, controller.value, layout_.view.player_controller.native_class);
        if (!controller_id.complete())
            return {controller_id.status};
        if (!controller_id.value.matches)
            return {Status::InvalidData};
        return controller;
    }();
    path_.status = path.status;
    value_.local_character = {path.status};
    value_.local_player_state = {path.status};
    if (!path.complete())
        return;
    if (!path.value) {
        value_.local_character.status = value_.local_player_state.status = Status::Complete;
        return;
    }
    auto body = [&]() -> Resolution<std::optional<ObjectIdentity>> {
        const auto pawn = link(body_, path.value, layout_.view.controller.pawn);
        if (!pawn.complete())
            return {pawn.status};
        if (!pawn.value)
            return {Status::Complete, {}};
        const auto character = object(body_, pawn.value, layout_.actor.character.native_class);
        if (!character.complete())
            return {character.status};
        if (!character.value.matches)
            return {Status::Complete, {}}; // 观战/其他 Pawn 不映射成被观察人物。
        const auto controller = link(body_, pawn.value, layout_.actor.pawn.controller);
        if (!controller.complete())
            return {controller.status};
        if (controller.value != path.value)
            return {Status::SourceChanged};
        return {Status::Complete, character.value.identity};
    }();
    value_.local_character = body;
    auto state = [&]() -> Resolution<std::optional<ObjectIdentity>> {
        const auto player_state = link(state_, path.value, layout_.view.controller.player_state);
        if (!player_state.complete())
            return {player_state.status};
        if (!player_state.value)
            return {Status::Complete, {}};
        const auto identity = object(state_, player_state.value, layout_.gameplay.player_state.native_class);
        if (!identity.complete())
            return {identity.status};
        if (!identity.value.matches)
            return {Status::UnsupportedCode};
        return {Status::Complete, identity.value.identity};
    }();
    value_.local_player_state = state;
}
void ContextReader::read(Address world) {
    if (read_ || closed_)
        return;
    read_ = true;
    read_map(world);
    read_local(world);
}
void ContextReader::close_sources() {
    if (closed_)
        return;
    closed_ = true;
    for (auto* group : {&map_, &path_, &body_, &state_})
        for (const auto& reference : group->links) {
            const auto value = read_reference(channel_, reference.owner, *reference.member, true);
            if (!value.complete())
                group->status = value.status;
            else if (value.value != reference.value)
                group->status = Status::SourceChanged;
        }
    if (names_) {
        const auto names = observation_.roots().name_pool();
        if (!names.complete())
            map_.status = names.status;
        else if (names.value.key != names_->key || names.value.allocator != names_->allocator)
            map_.status = Status::SourceChanged;
    }
    if (players_) {
        const auto players = read_array_view(channel_, players_, layout_.foundation.array, sizeof(Address));
        if (!players.complete())
            path_.status = players.status;
        else if (!players.value.count) {
            if (player_)
                path_.status = Status::SourceChanged;
        } else {
            const auto player = memory::read_value<Address>(channel_, players.value.data);
            if (!player)
                path_.status = Status::ReadUnavailable;
            else if (*player != player_)
                path_.status = Status::SourceChanged;
        }
    }
}
ResolutionStatus ContextReader::confirm(const Group& group) const {
    if (!closed_ || !read_)
        return Status::NotConfigured;
    if (group.status != Status::Complete)
        return group.status;
    for (const auto& proof : group.objects) {
        const auto status = observation_.confirm(proof.object, *proof.type);
        if (status != Status::Complete)
            return status;
    }
    return Status::Complete;
}
ContextSample ContextReader::finish() const {
    auto result = value_;
    if (const auto status = confirm(map_); status != Status::Complete)
        result.map = {status};
    const auto path = confirm(path_);
    const auto body = path == Status::Complete ? confirm(body_) : path;
    const auto state = path == Status::Complete ? confirm(state_) : path;
    if (body != Status::Complete)
        result.local_character = {body};
    if (state != Status::Complete)
        result.local_player_state = {state};
    return result;
}
} // namespace valorant::game
