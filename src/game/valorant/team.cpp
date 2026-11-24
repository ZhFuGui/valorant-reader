#include "game/valorant/team.hpp"
#include "game/valorant/reading.hpp"

namespace valorant::game {
using Status = ResolutionStatus;
using memory::Address;
Resolution<ObjectMatch> TeamReader::object(Record& record, Address address, const offsets::Root& type) {
    auto value = observation_.match(address, type);
    if (value.complete())
        record.proofs.push_back({value.value, &type});
    return value;
}
Resolution<Address> TeamReader::reference(Record& record, Address owner, const offsets::Member& member) {
    auto value = read_reference(channel_, owner, member, true);
    if (value.complete())
        record.links.push_back({owner, &member, value.value});
    return value;
}
Resolution<TeamSample> TeamReader::component(Record& record, Address address) {
    if (!address)
        return {Status::Complete, {}};
    const auto base = object(record, address, layout_.gameplay.team.base_class);
    if (!base.complete())
        return {base.status};
    if (!base.value.matches)
        return {Status::InvalidData};
    const auto bomb = object(record, address, layout_.gameplay.team.bomb_class);
    if (!bomb.complete())
        return {bomb.status};
    if (!bomb.value.matches)
        return {Status::Complete, {}}; // 其他原生方案不套用 Bomb 字节规则。
    const auto key_address = detail::field_address(address, layout_.gameplay.team.bomb_key, 1);
    if (!key_address)
        return {Status::NotConfigured};
    const auto key = memory::read_value<std::uint8_t>(channel_, key_address);
    if (!key)
        return {Status::ReadUnavailable};
    record.component = address;
    TeamSample value{model::TeamIdentity{*key}, false};
    const auto vtable = memory::read_value<Address>(channel_, address);
    if (!vtable || !layout_.gameplay.team.relation_slot.value)
        return {Status::ReadUnavailable};
    const auto entry = detail::field_address(*vtable, layout_.gameplay.team.relation_slot, sizeof(Address));
    const auto function = entry ? memory::read_value<Address>(channel_, entry) : std::nullopt;
    if (!function)
        return {Status::ReadUnavailable};
    record.vtable = *vtable;
    record.function = *function;
    const auto& expected = layout_.gameplay.team.bomb_relation;
    value.comparable = expected.module == image_.name && expected.rva && *expected.rva < image_.identity.image_bytes &&
                       *function == image_.base + *expected.rva;
    return {Status::Complete, value};
}
Resolution<TeamSample> TeamReader::owned(Record& record, Address actor) {
    const auto address = detail::field_address(actor, layout_.actor.base.owned_components, 1);
    if (!address)
        return {Status::NotConfigured};
    const auto set =
        ComponentReader(channel_, layout_.foundation.object_set, observation_.object_slots()).read(address);
    if (!set.complete())
        return {set.status};
    record.components = set.value;
    for (const auto member : set.value.objects) {
        if (!member)
            continue;
        const auto match = object(record, member, layout_.gameplay.team.base_class);
        if (!match.complete())
            return {match.status};
        if (match.value.matches)
            return component(record, member);
    }
    return {Status::Complete, {}};
}
Resolution<TeamSample> TeamReader::read_player_state(const ObjectIdentity& input) {
    if (closed_)
        return {Status::NotConfigured};
    auto [it, added] = records_.try_emplace(input.address);
    auto& record = it->second;
    if (!added)
        return record.sample;
    record.sample = [&]() -> Resolution<TeamSample> {
        const auto state = object(record, input.address, layout_.gameplay.player_state.native_class);
        if (!state.complete())
            return {state.status};
        if (!state.value.matches)
            return {Status::UnsupportedCode};
        if (state.value.identity != input)
            return {Status::SourceChanged};
        const auto team = reference(record, input.address, layout_.gameplay.player_state.team_component);
        return team.complete() ? component(record, team.value) : Resolution<TeamSample>{team.status};
    }();
    return record.sample;
}
Resolution<TeamSample> TeamReader::read_character(const ObjectIdentity& input) {
    if (closed_)
        return {Status::NotConfigured};
    auto [it, added] = records_.try_emplace(input.address);
    auto& record = it->second;
    if (!added)
        return record.sample;
    record.sample = [&]() -> Resolution<TeamSample> {
        const auto character = object(record, input.address, layout_.actor.character.native_class);
        if (!character.complete())
            return {character.status};
        if (!character.value.matches || character.value.identity != input)
            return {Status::SourceChanged};
        const auto state = reference(record, input.address, layout_.actor.pawn.player_state);
        if (!state.complete())
            return {state.status};
        if (!state.value)
            return owned(record, input.address); // 训练 BOT 没有 PlayerState，独立核验自身组件。
        const auto player_state = object(record, state.value, layout_.gameplay.player_state.native_class);
        if (!player_state.complete())
            return {player_state.status};
        if (!player_state.value.matches)
            return {Status::UnsupportedCode};
        record.dependency = state.value;
        return read_player_state(player_state.value.identity);
    }();
    return record.sample;
}
void TeamReader::close_sources() {
    if (closed_)
        return;
    closed_ = true;
    for (auto& [address, record] : records_) {
        (void)address;
        for (const auto& link : record.links) {
            const auto value = read_reference(channel_, link.owner, *link.member, true);
            if (!value.complete())
                record.closing = value.status;
            else if (value.value != link.value)
                record.closing = Status::SourceChanged;
        }
        if (record.components) {
            const auto status = ComponentReader(channel_, layout_.foundation.object_set, observation_.object_slots())
                                    .confirm(*record.components);
            if (status != Status::Complete)
                record.closing = status;
        }
        if (record.component && record.sample.complete() && record.sample.value.identity) {
            const auto key = memory::read_value<std::uint8_t>(
                channel_, detail::field_address(record.component, layout_.gameplay.team.bomb_key, 1));
            const auto table = memory::read_value<Address>(channel_, record.component);
            const auto entry =
                table ? detail::field_address(*table, layout_.gameplay.team.relation_slot, sizeof(Address)) : 0;
            const auto function = entry ? memory::read_value<Address>(channel_, entry) : std::nullopt;
            if (!key || !table || !function)
                record.closing = Status::ReadUnavailable;
            else if (*key != record.sample.value.identity->bomb_team || *table != record.vtable ||
                     *function != record.function)
                record.closing = Status::SourceChanged;
        }
    }
}
Resolution<TeamSample> TeamReader::result(Address owner) const {
    if (!closed_)
        return {Status::NotConfigured};
    const auto found = records_.find(owner);
    if (found == records_.end())
        return {Status::NotConfigured};
    const auto& record = found->second;
    if (!record.sample.complete())
        return record.sample;
    if (record.closing != Status::Complete)
        return {record.closing};
    for (const auto& proof : record.proofs) {
        const auto status = observation_.confirm(proof.object, *proof.type);
        if (status != Status::Complete)
            return {status};
    }
    // 只存在 Character → PlayerState 的单向依赖，不建立任意递归对象图。
    if (record.dependency)
        return result(record.dependency);
    return record.sample;
}
model::Relationship TeamReader::relationship(const TeamSample& character, const TeamSample& reference) noexcept {
    if (!character.comparable || !character.identity || !reference.identity || character.identity->bomb_team == 0xfe ||
        reference.identity->bomb_team == 0xfe)
        return model::Relationship::Unknown;
    return character.identity->bomb_team == reference.identity->bomb_team ? model::Relationship::Ally
                                                                          : model::Relationship::Enemy;
}
} // namespace valorant::game
