#include "synchronization/v1/codec.hpp"
#include <algorithm>
#include <cmath>

namespace valorant::synchronization::v1 {
namespace {
using wire::Writer;
Writer vector(const model::Vector3& v) {
    Writer out;
    out.real(1, v.x);
    out.real(2, v.y);
    out.real(3, v.z);
    return out;
}
Writer team(const model::TeamIdentity& value) {
    Writer out;
    out.uint32(1, value.bomb_team); // oneof 的零值也必须编码，保留存在性。
    return out;
}
Writer context(const model::PositionContext& value) {
    Writer out, local;
    if (value.map) {
        if (value.map->package_path.empty() || value.map->world_name.empty())
            throw wire::Error("invalid map identity");
        Writer map;
        map.text(1, value.map->package_path);
        map.text(2, value.map->world_name);
        out.message(1, map);
    }
    const auto& character = value.local;
    if (static_cast<unsigned>(character.state) > 3 ||
        (character.state == model::LocalCharacterState::Resolved) != character.entity_id.has_value() ||
        (character.entity_id && !*character.entity_id))
        throw wire::Error("invalid local identity");
    local.uint32(1, static_cast<unsigned>(character.state));
    if (character.entity_id)
        local.uint64(2, *character.entity_id);
    out.message(2, local);
    if (value.reference_team)
        out.message(3, team(*value.reference_team));
    return out;
}
Writer position(const model::EntityPosition& v) {
    Writer out;
    out.uint64(1, v.id);
    if (v.value)
        out.message(2, vector(*v.value));
    if (v.team)
        out.message(3, team(*v.team));
    if (static_cast<unsigned>(v.relationship) > 3)
        throw wire::Error("invalid relationship");
    out.uint32(4, static_cast<unsigned>(v.relationship));
    return out;
}
void validate_context(const model::PositionContext& context, const std::vector<model::EntityPosition>& values) {
    bool found_local = false;
    for (const auto& value : values) {
        const bool local = context.local.entity_id && *context.local.entity_id == value.id;
        found_local |= local;
        if ((value.relationship == model::Relationship::Self) != local)
            throw wire::Error("inconsistent local relation");
        if (value.relationship == model::Relationship::Ally || value.relationship == model::Relationship::Enemy) {
            if (!value.team || !context.reference_team || value.team->bomb_team == 0xfe ||
                context.reference_team->bomb_team == 0xfe ||
                (value.relationship == model::Relationship::Ally) != (value.team == context.reference_team))
                throw wire::Error("inconsistent team relation");
        }
    }
    if (context.local.entity_id && !found_local)
        throw wire::Error("unresolved local reference");
}
void packed_double(wire::Bytes& out, double value) {
    if (!std::isfinite(value))
        throw wire::Error("non-finite point");
    const auto bits = std::bit_cast<std::uint64_t>(value);
    for (unsigned i = 0; i < 8; ++i)
        out.push_back(std::byte((bits >> (i * 8)) & 255));
}
Writer skeleton(const model::EntitySkeleton& v) {
    Writer out;
    out.uint64(1, v.id);
    if (v.value) {
        Writer shape;
        std::uint32_t mask = 0;
        wire::Bytes xyz;
        for (unsigned i = 0; i < model::main_joint_count; ++i)
            if ((*v.value)[i]) {
                mask |= 1u << i;
                const auto& p = *(*v.value)[i];
                packed_double(xyz, p.x);
                packed_double(xyz, p.y);
                packed_double(xyz, p.z);
            }
        if (mask) {
            shape.uint32(1, 1);
            shape.fixed32(2, mask);
            (void)shape.blob(3, xyz);
            out.message(2, shape);
        }
    }
    return out;
}
Writer view(const std::optional<model::Camera>& v) {
    Writer out;
    if (!v)
        return out;
    Writer camera, rotation;
    rotation.real(1, v->rotation.pitch);
    rotation.real(2, v->rotation.yaw);
    rotation.real(3, v->rotation.roll);
    camera.message(1, vector(v->location));
    camera.message(2, rotation);
    camera.uint32(3, static_cast<unsigned>(v->projection));
    camera.real(4, v->fov);
    camera.real(5, v->aspect_ratio);
    camera.boolean(6, v->constrain_aspect_ratio);
    camera.real(7, v->ortho_width);
    camera.real(8, v->engine_timestamp);
    out.message(1, camera);
    return out;
}
template <class T, class Encode>
Writer collection(const std::vector<T>& values, const std::vector<T>* baseline, Encode encode) {
    Writer out;
    std::size_t old = 0;
    for (const auto& value : values) {
        if (!value.id)
            throw wire::Error("invalid entity identity");
        if (baseline) {
            while (old < baseline->size() && (*baseline)[old].id < value.id)
                out.uint64(2, (*baseline)[old++].id);
            if (old < baseline->size() && (*baseline)[old].id == value.id) {
                if (value != (*baseline)[old])
                    out.message(1, encode(value));
                ++old;
                continue;
            }
        }
        out.message(1, encode(value));
    }
    if (baseline)
        while (old < baseline->size())
            out.uint64(2, (*baseline)[old++].id);
    return out;
}
Writer interval(const model::CaptureStamp& stamp) {
    Writer out;
    if (stamp.started_qpc)
        (void)out.fixed64(1, stamp.started_qpc);
    if (stamp.read_finished_qpc) {
        if (!*stamp.read_finished_qpc)
            throw wire::Error("invalid read clock");
        (void)out.fixed64(2, *stamp.read_finished_qpc);
    }
    if (stamp.finished_qpc) {
        if (!*stamp.finished_qpc)
            throw wire::Error("invalid finish clock");
        (void)out.fixed64(3, *stamp.finished_qpc);
    }
    return out;
}
} // namespace
wire::Writer update(const model::StreamState& state, const model::StreamState* baseline) {
    if (!state.stamp.sequence || !state.stamp.epoch || !state.config_revision || !state.period_ns)
        throw wire::Error("invalid capture identity");
    if (baseline && (baseline->stream != state.stream || baseline->stamp.epoch != state.stamp.epoch ||
                     baseline->stamp.sequence >= state.stamp.sequence))
        throw wire::Error("invalid delta base");
    Writer out, meta, body;
    meta.uint64(1, state.stamp.epoch);
    meta.uint64(2, state.stamp.sequence);
    meta.uint64(3, state.config_revision);
    meta.uint64(4, state.period_ns);
    meta.uint64(5, 1);
    meta.message(6, interval(state.stamp));
    out.message(1, meta);
    if (baseline)
        body.uint64(1, baseline->stamp.sequence);
    Writer changes;
    bool changed = true;
    if (state.stream == model::Stream::Positions) {
        if (!state.position_context || (baseline && !baseline->position_context))
            throw wire::Error("missing position context");
        validate_context(*state.position_context, std::get<std::vector<model::EntityPosition>>(state.value));
        const auto* old = baseline ? &std::get<std::vector<model::EntityPosition>>(baseline->value) : nullptr;
        changes = collection(std::get<std::vector<model::EntityPosition>>(state.value), old, position);
        if (!baseline || state.position_context != baseline->position_context)
            changes.message(3, context(*state.position_context));
        changed = !changes.bytes.empty();
    } else if (state.stream == model::Stream::Skeletons) {
        const auto* old = baseline ? &std::get<std::vector<model::EntitySkeleton>>(baseline->value) : nullptr;
        changes = collection(std::get<std::vector<model::EntitySkeleton>>(state.value), old, skeleton);
        changed = !changes.bytes.empty();
    } else {
        const auto& camera = std::get<std::optional<model::Camera>>(state.value);
        changed = !baseline || camera != std::get<std::optional<model::Camera>>(baseline->value);
        changes = view(camera);
    }
    if (!baseline || changed)
        body.message(static_cast<unsigned>(state.stream) + (baseline ? 1 : 0), changes);
    out.message(baseline ? 3 : 2, body);
    return out;
}
wire::Writer diagnostic(const model::StreamState& state, const wire::Writer& data) {
    Writer out;
    out.message(1, data);
    if (!state.diagnostic)
        throw wire::Error("missing diagnostic projection");
    const auto& d = *state.diagnostic;
    Writer source;
    if (d.directory_generation) {
        source.uint32(1, 0);
        source.uint64(2, d.directory_generation);
        source.uint64(4, d.slots);
        source.uint64(5, d.empty);
        source.uint64(6, d.invalid);
        source.uint64(7, d.duplicates);
    }
    if (d.classification_generation)
        source.uint64(8, d.classification_generation);
    source.uint64(10, d.planned);
    source.uint64(11, d.unexecuted);
    source.uint64(12, d.metadata_retries);
    out.message(2, source);
    for (const auto& e : d.entities) {
        Writer entity;
        entity.uint64(1, e.id);
        entity.uint64(2, e.actor);
        entity.uint64(3, e.type);
        entity.uint32(4, e.index);
        entity.uint32(5, e.serial);
        entity.uint64(6, e.outer);
        entity.uint32(7, e.status);
        entity.uint32(8, e.failed_at);
        for (auto status : e.joints)
            entity.uint32(9, status); // repeated uint32 的非 packed 形式同样符合 protobuf。
        if (e.leader)
            entity.boolean(10, *e.leader);
        out.message(3, entity);
    }
    return out;
}
transport::Record envelope(unsigned body, const Writer& value, std::uint64_t event,
                           std::optional<std::uint64_t> mapping) {
    Writer out;
    out.uint32(1, 1);
    out.uint64(2, event);
    const auto stamp = out.fixed64(3, 0);
    if (mapping)
        out.uint64(4, *mapping);
    out.message(body, value);
    const auto prefix = transport::encode_length(static_cast<std::uint32_t>(out.bytes.size()));
    if (!prefix)
        throw wire::Error("record limit");
    transport::Record record;
    record.bytes.reserve(4 + out.bytes.size());
    record.bytes.insert(record.bytes.end(), prefix->begin(), prefix->end());
    record.bytes.insert(record.bytes.end(), out.bytes.begin(), out.bytes.end());
    record.stamp_offsets.push_back(4 + stamp);
    return record;
}
} // namespace valorant::synchronization::v1
