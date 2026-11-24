#include "game/valorant/view.hpp"
#include "game/valorant/reading.hpp"
#include <array>
#include <cstring>
#include <utility>

namespace valorant::game {
namespace {
using Status = ResolutionStatus;
using memory::Address;

Resolution<model::Camera> read_camera(memory::IReadChannel& channel, Address manager, const offsets::View& layout) {
    if (layout.camera_cache.timestamp_format != offsets::RealFormat::Float32 ||
        layout.pov.location.value != 0u || layout.pov.rotation.value != 0x18u || layout.pov.fov.value != 0x30u ||
        layout.pov.ortho_width.value != 0x38u || layout.pov.aspect_ratio.value != 0x48u ||
        layout.pov.constrain_aspect_ratio.storage.value != 0x54u || layout.pov.constrain_aspect_ratio.mask != 1u ||
        layout.pov.projection_mode.value != 0x58u)
        return {Status::UnsupportedCode};
    const auto cache = detail::field_address(manager, layout.camera_manager.camera_cache, 1);
    const auto timestamp = detail::field_address(cache, layout.camera_cache.timestamp, 4);
    const auto pov = detail::field_address(cache, layout.camera_cache.pov, 0x5c);
    if (!cache || !timestamp || !pov)
        return {Status::NotConfigured};
    const auto before = memory::read_value<float>(channel, timestamp);
    const auto payload = memory::read_value<std::array<std::byte, 0x5c>>(channel, pov);
    const auto after = memory::read_value<float>(channel, timestamp);
    if (!before || !payload || !after)
        return {Status::ReadUnavailable};
    if (*before != *after)
        return {Status::SourceChanged};
    model::Camera value;
    value.engine_timestamp = *before;
    const auto copy = [&](std::size_t offset, auto& target) {
        std::memcpy(&target, payload->data() + offset, sizeof(target));
    };
    copy(0, value.location); copy(0x18, value.rotation); copy(0x30, value.fov);
    copy(0x38, value.ortho_width); copy(0x48, value.aspect_ratio); copy(0x58, value.raw_projection);
    value.constrain_aspect_ratio = (std::to_integer<unsigned>((*payload)[0x54]) & 1) != 0;
    // 当前 dump 的原生枚举注册表分别写入 0、1；其他值保留 Unknown。
    if (value.raw_projection == 0) value.projection = model::Projection::Perspective;
    if (value.raw_projection == 1) value.projection = model::Projection::Orthographic;
    if (!model::finite(value.location) || !std::isfinite(value.rotation.pitch) || !std::isfinite(value.rotation.yaw) ||
        !std::isfinite(value.rotation.roll) || !std::isfinite(value.engine_timestamp) ||
        !std::isfinite(value.fov) || !std::isfinite(value.aspect_ratio) || !std::isfinite(value.ortho_width) ||
        value.aspect_ratio <= 0 || (value.projection == model::Projection::Perspective && (value.fov <= 0 || value.fov >= 180)) ||
        (value.projection == model::Projection::Orthographic && value.ortho_width <= 0))
        return {Status::InvalidData};
    return {Status::Complete, value};
}
} // namespace

ViewBatch ViewReader::read(std::uint64_t sequence, std::uint64_t epoch, std::uint32_t local_slot) const {
    ViewBatch batch;
    batch.stamp = {sequence, epoch, clock_.frequency(), clock_.now(), {}, {}};
    const auto& layout = profile_.layout;
    Observation observation(channel_, profile_, image_);
    auto attempt = [&]() -> Resolution<model::Camera> {
        const auto opened = observation.open();
        if (opened != Status::Complete)
            return {opened};
        const auto engine = observation.roots().engine();
        if (!engine.complete()) return {engine.status};
        const auto viewport = read_reference(channel_, engine.value, layout.world.engine.game_viewport);
        if (!viewport.complete()) return {viewport.status};
        const auto world = read_reference(channel_, viewport.value, layout.world.viewport.world);
        if (!world.complete()) return {world.status};
        const auto world_id = observation.require(world.value, layout.world.context.native_class);
        if (!world_id.complete()) return {world_id.status};
        const auto instance = read_reference(channel_, world.value, layout.world.context.game_instance);
        if (!instance.complete()) return {instance.status};
        const auto instance_id = observation.require(instance.value, layout.world.game_instance.native_class);
        if (!instance_id.complete()) return {instance_id.status};
        if (layout.world.game_instance.local_player_reference_bytes.value != sizeof(Address))
            return {Status::UnsupportedCode};
        const auto array = detail::field_address(instance.value, layout.world.game_instance.local_players, 16);
        const auto players = read_array_view(channel_, array, layout.foundation.array, sizeof(Address));
        if (!players.complete()) return {players.status};
        if (local_slot >= static_cast<std::uint32_t>(players.value.count)) return {Status::InvalidData};
        const auto player = memory::read_value<Address>(channel_, players.value.data + std::uint64_t{local_slot}*sizeof(Address));
        if (!player) return {Status::ReadUnavailable};
        const auto player_id = observation.require(*player, layout.view.player.native_class);
        if (!player_id.complete()) return {player_id.status};
        const auto controller = read_reference(channel_, *player, layout.view.player.controller);
        if (!controller.complete()) return {controller.status};
        const auto controller_id = observation.require(controller.value, layout.view.player_controller.native_class);
        if (!controller_id.complete()) return {controller_id.status};
        const auto manager = read_reference(channel_, controller.value, layout.view.player_controller.camera_manager);
        if (!manager.complete()) return {manager.status};
        const auto manager_id = observation.require(manager.value, layout.view.camera_manager.native_class);
        if (!manager_id.complete()) return {manager_id.status};
        const auto camera = read_camera(channel_, manager.value, layout.view);
        if (!camera.complete()) return {camera.status};
        // 关闭实际使用的来源，不读取或要求 Pawn、血量、存活状态及人物名单。
        const auto closing_players = read_array_view(channel_, array, layout.foundation.array, sizeof(Address));
        if (!closing_players.complete()) return {closing_players.status};
        if (local_slot >= static_cast<std::uint32_t>(closing_players.value.count)) return {Status::SourceChanged};
        const auto closing_player = memory::read_value<Address>(channel_, closing_players.value.data + std::uint64_t{local_slot}*8);
        if (!closing_player) return {Status::ReadUnavailable};
        if (*closing_player != *player) return {Status::SourceChanged};
        struct Link { Address owner; const offsets::Member* member; Address expected; };
        const std::array links{Link{engine.value, &layout.world.engine.game_viewport, viewport.value},
            Link{viewport.value, &layout.world.viewport.world, world.value},
            Link{world.value, &layout.world.context.game_instance, instance.value},
            Link{*player, &layout.view.player.controller, controller.value},
            Link{controller.value, &layout.view.player_controller.camera_manager, manager.value}};
        for (const auto& link : links) {
            const auto value = read_reference(channel_, link.owner, *link.member);
            if (!value.complete()) return {value.status};
            if (value.value != link.expected) return {Status::SourceChanged};
        }
        const auto closing_engine = observation.roots().engine();
        if (!closing_engine.complete()) return {closing_engine.status};
        if (closing_engine.value != engine.value) return {Status::SourceChanged};
        const auto closed = observation.close();
        batch.stamp.read_finished_qpc = clock_.now();
        if (closed != Status::Complete) return {closed};
        for (const auto status : {observation.confirm(world_id.value, layout.world.context.native_class),
                 observation.confirm(instance_id.value, layout.world.game_instance.native_class),
                 observation.confirm(player_id.value, layout.view.player.native_class),
                 observation.confirm(controller_id.value, layout.view.player_controller.native_class),
                 observation.confirm(manager_id.value, layout.view.camera_manager.native_class)})
            if (status != Status::Complete) return {status};
        batch.context = WorldContext{engine.value, viewport.value, world.value};
        return camera;
    };
    batch.camera = attempt();
    // 失败路径也关闭本批已建立的证据；不无限重试、不复用上次相机。
    const auto closed = observation.close();
    if (batch.camera.complete() && closed != Status::Complete)
        batch.camera = {closed};
    if (!batch.stamp.read_finished_qpc)
        batch.stamp.read_finished_qpc = clock_.now();
    batch.stamp.finished_qpc = clock_.now();
    return batch;
}
} // namespace valorant::game
