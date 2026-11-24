#include "game/valorant/directory.hpp"
#include "game/valorant/reading.hpp"
#include <array>
#include <unordered_map>
#include <utility>

namespace valorant::game {
namespace {
using memory::Address;
using Status = ResolutionStatus;

Resolution<Address> pointer(memory::IReadChannel& channel, Address owner, const offsets::Member& member) noexcept {
    if (!member.value)
        return {Status::NotConfigured};
    const auto address = detail::field_address(owner, member, sizeof(Address));
    if (!address)
        return {Status::InvalidData};
    const auto value = memory::read_value<Address>(channel, address);
    if (!value)
        return {Status::ReadUnavailable};
    if (!memory::user_range(*value) || (*value & 7))
        return {Status::InvalidData};
    return {Status::Complete, *value};
}

struct ArrayState {
    Address data = 0;
    std::int32_t count = 0, capacity = 0;
    bool operator==(const ArrayState&) const = default;
};

Resolution<ArrayState> read_array(memory::IReadChannel& channel, Address array, const offsets::Array& layout) noexcept {
    ArrayState state;
    std::array requests{
        detail::field_read(array, layout.data, &state.data, sizeof(state.data)),
        detail::field_read(array, layout.count, &state.count, sizeof(state.count)),
        detail::field_read(array, layout.capacity, &state.capacity, sizeof(state.capacity)),
    };
    if (!detail::complete_fields(channel, requests))
        return {Status::ReadUnavailable};
    if (state.count < 0 || state.capacity < state.count)
        return {Status::InvalidData};
    const auto capacity_bytes = std::size_t{static_cast<std::uint32_t>(state.capacity)} * sizeof(Address);
    if (state.capacity && (!memory::user_range(state.data, capacity_bytes) || (state.data & 7)))
        return {Status::InvalidData};
    return {Status::Complete, state};
}
} // namespace

Resolution<ActorDirectory> DirectoryReader::read() const {
    const auto& table = profile_.layout;
    if (!table.has_world_discovery_layout() || !table.world.level.owning_world.value)
        return {Status::NotConfigured};
    if (table.world.level.actor_reference_bytes.value != sizeof(Address) ||
        table.foundation.array.header_bytes.value != 16u)
        return {Status::UnsupportedCode};
    const auto engine = roots_.engine();
    if (!engine.complete())
        return {engine.status};
    const auto viewport = pointer(channel_, engine.value, table.world.engine.game_viewport);
    if (!viewport.complete())
        return {viewport.status};
    const auto world = pointer(channel_, viewport.value, table.world.viewport.world);
    if (!world.complete())
        return {world.status};
    const auto level = pointer(channel_, world.value, table.world.context.persistent_level);
    if (!level.complete())
        return {level.status};
    const auto owner = pointer(channel_, level.value, table.world.level.owning_world);
    if (!owner.complete())
        return {owner.status};
    if (owner.value != world.value)
        return {Status::SourceChanged};
    const auto array = detail::field_address(level.value, table.world.level.actors, *table.foundation.array.header_bytes.value);
    if (!array)
        return {Status::InvalidData};
    const auto first = read_array(channel_, array, table.foundation.array);
    if (!first.complete())
        return {first.status};
    const auto count = static_cast<std::uint32_t>(first.value.count);
    std::vector<Address> addresses(count);
    const auto bytes = addresses.size() * sizeof(Address);
    if (bytes && !channel_.read(first.value.data, addresses.data(), bytes).complete(bytes))
        return {Status::ReadUnavailable};

    const auto closing = read_array(channel_, array, table.foundation.array);
    if (!closing.complete())
        return {closing.status};
    const auto closing_owner = pointer(channel_, level.value, table.world.level.owning_world);
    const auto closing_level = pointer(channel_, world.value, table.world.context.persistent_level);
    const auto closing_world = pointer(channel_, viewport.value, table.world.viewport.world);
    const auto closing_viewport = pointer(channel_, engine.value, table.world.engine.game_viewport);
    const auto closing_engine = roots_.engine();
    for (const auto status : {closing_owner.status, closing_level.status, closing_world.status, closing_viewport.status,
                             closing_engine.status})
        if (status != Status::Complete)
            return {status};
    if (first.value != closing.value || owner.value != closing_owner.value || level.value != closing_level.value ||
        world.value != closing_world.value || viewport.value != closing_viewport.value ||
        engine.value != closing_engine.value)
        return {Status::SourceChanged};

    ActorDirectory result;
    result.engine = engine.value;
    result.viewport = viewport.value;
    result.world = world.value;
    result.level = level.value;
    result.coverage.slots = count;
    result.slots.resize(count);
    std::unordered_map<Address, std::uint32_t> first_slot;
    first_slot.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        auto& slot = result.slots[i];
        slot.address = addresses[i];
        slot.first_slot = i;
        if (!slot.address) {
            slot.state = ActorSlotState::Empty;
            ++result.coverage.empty;
        } else if (!memory::user_range(slot.address) || (slot.address & 7)) {
            slot.state = ActorSlotState::InvalidAddress;
            ++result.coverage.invalid;
        } else {
            const auto [entry, inserted] = first_slot.emplace(slot.address, i);
            slot.first_slot = entry->second;
            slot.state = inserted ? ActorSlotState::Unique : ActorSlotState::Duplicate;
            if (inserted)
                ++result.coverage.unique;
            else
                ++result.coverage.duplicates;
        }
    }
    return {Status::Complete, std::move(result)};
}

ResolutionStatus DirectoryReader::confirm_context(const ActorDirectory& first) const noexcept {
    const auto& table = profile_.layout;
    const auto engine = roots_.engine();
    if (!engine.complete())
        return engine.status;
    if (engine.value != first.engine)
        return Status::SourceChanged;
    const auto viewport = pointer(channel_, engine.value, table.world.engine.game_viewport);
    if (!viewport.complete())
        return viewport.status;
    if (viewport.value != first.viewport)
        return Status::SourceChanged;
    const auto world = pointer(channel_, viewport.value, table.world.viewport.world);
    if (!world.complete())
        return world.status;
    if (world.value != first.world)
        return Status::SourceChanged;
    const auto level = pointer(channel_, world.value, table.world.context.persistent_level);
    if (!level.complete())
        return level.status;
    if (level.value != first.level)
        return Status::SourceChanged;
    const auto owner = pointer(channel_, level.value, table.world.level.owning_world);
    if (!owner.complete())
        return owner.status;
    return owner.value == world.value ? Status::Complete : Status::SourceChanged;
}
} // namespace valorant::game
