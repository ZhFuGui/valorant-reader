#include "game/valorant/scene.hpp"
#include "game/valorant/reading.hpp"
#include <array>

namespace valorant::game {
using Status = ResolutionStatus;
using memory::Address;

Resolution<SceneReader::Selection> SceneReader::selection(Address component) const {
    const auto& p = layout_.foundation.pointer_selection;
    if (p.primary.value != 0u || p.alternate.value != 8u || p.selector.value != 16u)
        return {Status::UnsupportedCode};
    const auto address = detail::field_address(component, layout_.actor.scene_component.world_transform_source,
                                               sizeof(Selection));
    const auto value = address ? memory::read_value<Selection>(channel_, address) : std::nullopt;
    return value ? Resolution<Selection>{Status::Complete, *value} : Resolution<Selection>{Status::ReadUnavailable};
}

Resolution<bool> SceneReader::predicate(Address function) const {
    if (image_.identity.image_bytes < 3 || function < image_.base ||
        function - image_.base > image_.identity.image_bytes - 3u)
        return {Status::UnsupportedCode};
    const auto bytes = memory::read_value<std::array<std::uint8_t, 3>>(channel_, function);
    if (!bytes)
        return {Status::ReadUnavailable};
    if (*bytes == std::array<std::uint8_t, 3>{0x32, 0xc0, 0xc3} ||
        *bytes == std::array<std::uint8_t, 3>{0x33, 0xc0, 0xc3})
        return {Status::Complete, false};
    if (*bytes == std::array<std::uint8_t, 3>{0xb0, 0x01, 0xc3})
        return {Status::Complete, true};
    return {Status::UnsupportedCode};
}

Resolution<SceneValue> SceneReader::read(Address component, bool position_only) {
    if (closed_)
        return {Status::NotConfigured};
    const auto identity = observation_.require(component, layout_.actor.scene_component.native_class);
    if (!identity.complete())
        return {identity.status};
    const auto source = selection(component);
    if (!source.complete())
        return {source.status};
    auto [it, inserted] = selectors_.try_emplace(source.value.selector);
    if (inserted)
        it->second.initial = predicate(source.value.selector);
    const auto& branch = it->second.initial;
    if (!branch.complete())
        return {branch.status};
    const auto selected = branch.value ? source.value.alternate : source.value.primary;
    if (!memory::user_range(selected, 96) || (selected & 7))
        return {Status::InvalidData};
    const auto& layout = layout_.foundation.transform;
    if (layout.translation_format != offsets::RealFormat::Float64 || layout.translation.value != 0x20u)
        return {Status::UnsupportedCode};
    SceneValue value{identity.value, source.value.selector, selected, {}};
    if (position_only) {
        auto request = detail::field_read(selected, layout.translation, &value.transform.translation, sizeof(model::Vector3));
        if (!detail::complete_fields(channel_, std::span{&request, 1}))
            return {Status::ReadUnavailable};
        if (!model::finite(value.transform.translation))
            return {Status::InvalidData};
    } else {
        if (layout.rotation.value != 0u || layout.scale.value != 0x40u || layout.bytes.value != 96u ||
            layout.rotation_format != offsets::RealFormat::Float64 || layout.scale_format != offsets::RealFormat::Float64)
            return {Status::UnsupportedCode};
        std::array requests{detail::field_read(selected, layout.rotation, &value.transform.rotation, sizeof(model::Quaternion)),
                            detail::field_read(selected, layout.translation, &value.transform.translation, sizeof(model::Vector3)),
                            detail::field_read(selected, layout.scale, &value.transform.scale, sizeof(model::Vector3))};
        if (!detail::complete_fields(channel_, requests))
            return {Status::ReadUnavailable};
        if (!model::valid(value.transform))
            return {Status::InvalidData};
    }
    return {Status::Complete, value};
}

ResolutionStatus SceneReader::confirm(const SceneValue& value) const {
    const auto source = selection(value.component.address);
    if (!source.complete())
        return source.status;
    const auto found = selectors_.find(value.selector);
    if (found == selectors_.end() || !found->second.initial.complete())
        return Status::NotConfigured;
    if (source.value.selector != value.selector ||
        (found->second.initial.value ? source.value.alternate : source.value.primary) != value.selected)
        return Status::SourceChanged;
    return Status::Complete;
}

void SceneReader::close() {
    if (closed_)
        return;
    closed_ = true;
    for (auto& [address, entry] : selectors_) {
        const auto value = predicate(address);
        entry.closing = !entry.initial.complete() ? entry.initial.status : !value.complete() ? value.status
                       : value.value != entry.initial.value ? Status::SourceChanged : Status::Complete;
    }
}

ResolutionStatus SceneReader::confirm_selector(const SceneValue& value) const {
    const auto found = selectors_.find(value.selector);
    return closed_ && found != selectors_.end() ? found->second.closing : Status::NotConfigured;
}
} // namespace valorant::game
