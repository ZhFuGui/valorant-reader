#include "game/valorant/position.hpp"
#include "game/valorant/reading.hpp"

namespace valorant::game {
using Status = ResolutionStatus;

PositionBatch PositionReader::read(const ActorDirectory& directory, std::span<const ObjectIdentity> inputs,
                                   std::uint64_t sequence, std::uint64_t epoch) const {
    PositionBatch result;
    result.stamp = {sequence, epoch, clock_.frequency(), clock_.now(), {}, {}};
    result.characters.resize(inputs.size());
    Observation observation(channel_, profile_, image_);
    SceneReader scene(channel_, profile_, image_, observation);
    auto shared = observation.open();
    ContextReader context(channel_, profile_, observation);
    TeamReader teams(channel_, profile_, image_, observation);
    if (shared == Status::Complete) {
        context.read(directory.world);
        const auto& local = context.opened().local_player_state;
        if (local.complete() && local.value)
            (void)teams.read_player_state(*local.value);
    }
    const auto& layout = profile_.layout.actor;
    std::vector<Resolution<SceneValue>> sources(inputs.size());
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        auto& output = result.characters[i];
        output.identity = inputs[i];
        output.position.status = shared;
        output.team.status = shared;
        if (shared != Status::Complete)
            continue;
        (void)teams.read_character(inputs[i]);
        const auto actor = observation.require(inputs[i].address, layout.character.native_class);
        auto status = actor.status;
        if (actor.complete() && actor.value != inputs[i])
            status = Status::SourceChanged;
        if (status == Status::Complete) {
            const auto root = read_reference(channel_, inputs[i].address, layout.base.root_component);
            status = root.status;
            if (root.complete()) {
                sources[i] = scene.read(root.value, true);
                status = sources[i].status;
            }
        }
        output.position.status = status;
    }
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        if (!result.characters[i].position.complete())
            continue;
        const auto root = read_reference(channel_, inputs[i].address, layout.base.root_component);
        auto status = !root.complete() ? root.status : root.value != sources[i].value.component.address ? Status::SourceChanged
                                                                                                   : scene.confirm(sources[i].value);
        const auto& flag = layout.base.being_destroyed;
        const auto address = detail::field_address(inputs[i].address, flag.storage, 1);
        const auto destroyed = address ? memory::read_value<std::uint8_t>(channel_, address) : std::nullopt;
        if (!flag.mask || !*flag.mask)
            status = Status::NotConfigured;
        else if (!destroyed)
            status = Status::ReadUnavailable;
        else if (*destroyed & *flag.mask)
            status = Status::SourceChanged;
        result.characters[i].position.status = status;
    }
    scene.close();
    context.close_sources();
    teams.close_sources();
    shared = observation.close();
    if (shared == Status::Complete)
        shared = DirectoryReader(channel_, profile_, image_).confirm_context(directory);
    result.stamp.read_finished_qpc = clock_.now();
    result.source_status = shared;
    result.context = context.finish();
    const auto& local_state = result.context.local_player_state;
    if (local_state.complete() && local_state.value)
        result.reference_team = teams.result(local_state.value->address);
    if (shared != Status::Complete) {
        result.context = {{shared}, {shared}, {shared}};
        result.reference_team = {shared};
    }
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        auto& value = result.characters[i].position;
        result.characters[i].team = shared == Status::Complete ? teams.result(inputs[i].address) : Resolution<TeamSample>{shared};
        if (shared != Status::Complete)
            value = {shared};
        if (!value.complete())
            continue;
        for (const auto status : {observation.confirm(inputs[i], layout.character.native_class),
                                 observation.confirm(sources[i].value.component, layout.scene_component.native_class),
                                 scene.confirm_selector(sources[i].value)})
            if (status != Status::Complete)
                value.status = status;
        if (value.complete())
            value.value = sources[i].value.transform.translation;
    }
    result.stamp.finished_qpc = clock_.now();
    return result;
}
} // namespace valorant::game
