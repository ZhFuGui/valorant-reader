#include "game/valorant/pose.hpp"
#include "game/valorant/reading.hpp"
#include <algorithm>
#include <array>
#include <utility>

namespace valorant::game {
namespace {
using Status = ResolutionStatus;
using memory::Address;
constexpr auto points = model::main_joint_count;

struct PoseSource {
    ObjectIdentity component, owner;
    Address asset = 0;
    WeakObjectReference weak;
    std::optional<ObjectIdentity> leader;
    SceneValue world;
    std::int32_t read_index = -1;
    ArrayView buffer, bone_map;
    std::array<std::int32_t, points> indices{};
};

class Sampler {
  public:
    Sampler(memory::IReadChannel& channel, const offsets::Table& layout, Observation& observation,
            SceneReader& scene, RigCatalog& rigs)
        : channel_(channel), layout_(layout), observation_(observation), scene_(scene), rigs_(rigs) {}

    Status sample(const ObjectIdentity& actor, CharacterPose& output, PoseSource& source) {
        const auto& mesh = layout_.pose.component;
        output.failed_at = PoseFailureStage::Layout;
        if (layout_.foundation.transform.translation.value != 0x20u ||
            layout_.foundation.transform.translation_format != offsets::RealFormat::Float64 ||
            mesh.transform_stride.value != 96u || mesh.buffer_count.value != 2u || mesh.buffer_stride.value != 16u ||
            layout_.identity.weak_object.bytes.value != 8u || layout_.identity.weak_object.index.value != 0u ||
            layout_.identity.weak_object.serial.value != 4u)
            return Status::UnsupportedCode;
        output.failed_at = PoseFailureStage::Character;
        const auto identity = observation_.require(actor.address, layout_.actor.character.native_class);
        if (!identity.complete()) return identity.status;
        if (identity.value != actor) return Status::SourceChanged;
        output.failed_at = PoseFailureStage::Mesh;
        const auto component = read_reference(channel_, actor.address, layout_.actor.character.mesh);
        if (!component.complete()) return component.status;
        const auto component_id = observation_.require(component.value, mesh.native_class);
        if (!component_id.complete()) return component_id.status;
        source.component = component_id.value;
        output.failed_at = PoseFailureStage::Ready;
        auto status = ready(component.value);
        if (status != Status::Complete) return status;
        output.failed_at = PoseFailureStage::Asset;
        const auto asset = selected_asset(component.value);
        if (!asset.complete()) return asset.status;
        source.asset = asset.value;
        output.failed_at = PoseFailureStage::Rig;
        const auto& rig = rigs_.observe(asset.value);
        if (rig.status != Status::Complete) return rig.status;
        output.failed_at = PoseFailureStage::WorldTransform;
        const auto world = scene_.read(component.value);
        if (!world.complete()) return world.status;
        source.world = world.value;
        output.failed_at = PoseFailureStage::Leader;
        const auto weak = weak_reference(component.value);
        if (!weak.complete()) return weak.status;
        source.weak = weak.value;
        const auto leader = observation_.objects().resolve_weak(weak.value);
        if (!leader.complete()) return leader.status;
        source.leader = leader.value;
        source.owner = source.component;
        if (leader.value) {
            const auto typed = observation_.require(leader.value->address, mesh.native_class);
            if (!typed.complete()) return typed.status;
            if (typed.value != *leader.value) return Status::SourceChanged;
            source.owner = typed.value;
        }
        output.failed_at = PoseFailureStage::Buffer;
        const auto buffer = read_buffer(source.owner.address, source.read_index);
        if (!buffer.complete()) return buffer.status;
        source.buffer = buffer.value;
        if (!buffer.value.count) return Status::ReadUnavailable;
        output.skeleton = model::Skeleton{rig.joints, {}};
        output.leader_pose = leader.value.has_value();
        output.joint_status.fill(Status::InvalidData);
        std::vector<memory::ReadRequest> mappings;
        if (leader.value) {
            output.failed_at = PoseFailureStage::Mapping;
            const auto map = read_array_view(channel_, detail::field_address(component.value, mesh.leader_bone_map, 16),
                                             layout_.foundation.array, sizeof(std::int32_t));
            if (!map.complete()) return map.status;
            source.bone_map = map.value;
            for (std::size_t i = 0; i < points; ++i) {
                source.indices[i] = -1;
                if (rig.joints[i].native_index < static_cast<std::uint32_t>(map.value.count))
                    mappings.push_back({map.value.data + std::size_t{rig.joints[i].native_index}*4,
                                        &source.indices[i], 4, {}});
            }
            channel_.read_many(mappings);
        } else {
            for (std::size_t i = 0; i < points; ++i)
                source.indices[i] = static_cast<std::int32_t>(rig.joints[i].native_index);
        }
        std::array<model::Vector3, points> translations{};
        std::array<bool, points> requested{};
        std::vector<memory::ReadRequest> requests;
        requests.reserve(points);
        for (std::size_t i = 0; i < points; ++i) {
            if (leader.value) {
                const auto mapping = std::find_if(mappings.begin(), mappings.end(), [&](const auto& request) {
                    return request.destination == &source.indices[i];
                });
                if (mapping != mappings.end() && !mapping->result.complete(4)) {
                    output.joint_status[i] = Status::ReadUnavailable;
                    continue;
                }
            }
            if (source.indices[i] < 0 || source.indices[i] >= buffer.value.count) continue;
            requested[i] = true;
            requests.push_back({buffer.value.data + static_cast<std::size_t>(source.indices[i])*96 + 0x20,
                                &translations[i], sizeof(model::Vector3), {}});
        }
        channel_.read_many(requests);
        std::size_t request_index = 0;
        for (std::size_t i = 0; i < points; ++i) {
            if (!requested[i]) continue;
            const auto& request = requests[request_index++];
            if (!request.result.complete(request.bytes)) output.joint_status[i] = Status::ReadUnavailable;
            else {
                const auto point = model::transform_point(source.world.transform, translations[i]);
                if (model::finite(translations[i]) && model::finite(point)) {
                    output.skeleton->world_positions[i] = point;
                    output.joint_status[i] = Status::Complete;
                }
            }
        }
        // 及时关闭本次动态来源，避免等待其他角色的慢资产解析才核验双缓冲。
        output.failed_at = PoseFailureStage::SourceClosing;
        return close(actor, source, output);
    }

  private:
    Resolution<Address> selected_asset(Address component) const {
        const auto first = read_reference(channel_, component, layout_.pose.component.skeletal_mesh, true);
        if (!first.complete() || first.value) return first;
        const auto alternate = read_reference(channel_, component, layout_.pose.component.skinned_asset, true);
        if (alternate.complete() && !alternate.value) return {Status::ReadUnavailable};
        return alternate;
    }
    Status ready(Address component) const {
        const auto& flag = layout_.pose.component.transform_ready;
        if (!flag.storage.value || !flag.mask || !*flag.mask) return Status::NotConfigured;
        const auto address = detail::field_address(component, flag.storage, 1);
        const auto value = address ? memory::read_value<std::uint8_t>(channel_, address) : std::nullopt;
        if (!value) return Status::ReadUnavailable;
        return (*value & *flag.mask) ? Status::Complete : Status::ReadUnavailable;
    }
    Resolution<WeakObjectReference> weak_reference(Address component) const {
        const auto address = detail::field_address(component, layout_.pose.component.leader_pose, sizeof(WeakObjectReference));
        const auto value = address ? memory::read_value<WeakObjectReference>(channel_, address) : std::nullopt;
        return value ? Resolution<WeakObjectReference>{Status::Complete, *value}
                     : Resolution<WeakObjectReference>{Status::ReadUnavailable};
    }
    Resolution<ArrayView> read_buffer(Address component, std::int32_t& index) const {
        const auto address = detail::field_address(component, layout_.pose.component.read_buffer, 4);
        const auto value = address ? memory::read_value<std::int32_t>(channel_, address) : std::nullopt;
        if (!value) return {Status::ReadUnavailable};
        index = *value;
        if (index < 0 || index >= 2) return {Status::InvalidData};
        const auto headers = detail::field_address(component, layout_.pose.component.pose_buffers, 32);
        if (!headers) return {Status::NotConfigured};
        return read_array_view(channel_, headers + static_cast<std::size_t>(index)*16, layout_.foundation.array, 96);
    }
    Status close(const ObjectIdentity& actor, const PoseSource& source, CharacterPose& output) const {
        const auto mesh = read_reference(channel_, actor.address, layout_.actor.character.mesh);
        if (!mesh.complete()) return mesh.status;
        if (mesh.value != source.component.address) return Status::SourceChanged;
        const auto asset = selected_asset(source.component.address);
        if (!asset.complete()) return asset.status;
        if (asset.value != source.asset) return Status::SourceChanged;
        const auto weak = weak_reference(source.component.address);
        if (!weak.complete()) return weak.status;
        if (weak.value != source.weak) return Status::SourceChanged;
        const auto leader = observation_.objects().resolve_weak(weak.value);
        if (!leader.complete()) return leader.status;
        if (leader.value != source.leader) return Status::SourceChanged;
        std::int32_t index = -1;
        const auto buffer = read_buffer(source.owner.address, index);
        if (index >= 0 && index != source.read_index) return Status::SourceChanged;
        if (!buffer.complete()) return buffer.status;
        if (buffer.value != source.buffer) return Status::SourceChanged;
        if (source.leader) {
            const auto map = read_array_view(channel_, detail::field_address(source.component.address,
                       layout_.pose.component.leader_bone_map, 16), layout_.foundation.array, 4);
            if (!map.complete()) return map.status;
            if (map.value != source.bone_map) return Status::SourceChanged;
            std::array<std::int32_t, points> values{};
            std::vector<memory::ReadRequest> requests;
            for (std::size_t i = 0; i < points; ++i)
                if (output.joint_status[i] == Status::Complete)
                    requests.push_back({map.value.data + std::size_t{output.skeleton->layout[i].native_index}*4,
                                        &values[i], 4, {}});
            channel_.read_many(requests);
            for (const auto& request : requests) {
                const auto i = static_cast<std::size_t>(static_cast<const std::int32_t*>(request.destination) - values.data());
                if (!request.result.complete(4)) output.joint_status[i] = Status::ReadUnavailable;
                else if (values[i] != source.indices[i]) output.joint_status[i] = Status::SourceChanged;
                if (output.joint_status[i] != Status::Complete) output.skeleton->world_positions[i].reset();
            }
        }
        const auto& flag = layout_.actor.base.being_destroyed;
        const auto address = detail::field_address(actor.address, flag.storage, 1);
        const auto destroyed = address ? memory::read_value<std::uint8_t>(channel_, address) : std::nullopt;
        if (!flag.mask || !*flag.mask) return Status::NotConfigured;
        if (!destroyed) return Status::ReadUnavailable;
        if (*destroyed & *flag.mask) return Status::SourceChanged;
        const auto status = scene_.confirm(source.world);
        return status != Status::Complete ? status : ready(source.component.address);
    }
    memory::IReadChannel& channel_;
    const offsets::Table& layout_;
    Observation& observation_;
    SceneReader& scene_;
    RigCatalog& rigs_;
};
} // namespace

PoseBatch PoseReader::read(const ActorDirectory& directory, std::span<const ObjectIdentity> inputs,
                           std::uint64_t sequence, std::uint64_t epoch) const {
    PoseBatch batch;
    batch.stamp = {sequence, epoch, clock_.frequency(), clock_.now(), {}, {}};
    batch.characters.resize(inputs.size());
    Observation observation(channel_, profile_, image_);
    SceneReader scene(channel_, profile_, image_, observation);
    RigCatalog rigs(channel_, profile_, observation);
    Sampler sampler(channel_, profile_.layout, observation, scene, rigs);
    std::vector<PoseSource> sources(inputs.size());
    auto shared = observation.open();
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        auto& output = batch.characters[i];
        output.identity = inputs[i];
        output.failed_at = PoseFailureStage::SharedSources;
        output.status = shared == Status::Complete ? sampler.sample(inputs[i], output, sources[i]) : shared;
    }
    rigs.close();
    scene.close();
    shared = observation.close();
    if (shared != Status::Complete)
        batch.failed_at = PoseFailureStage::SharedSources;
    if (shared == Status::Complete) {
        shared = DirectoryReader(channel_, profile_, image_).confirm_context(directory);
        if (shared != Status::Complete)
            batch.failed_at = PoseFailureStage::WorldClosing;
    }
    batch.stamp.read_finished_qpc = clock_.now();
    batch.source_status = shared;
    batch.assets = rigs.size();
    batch.metadata_source_retries = rigs.source_retries();
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        auto& output = batch.characters[i];
        const auto& source = sources[i];
        if (shared != Status::Complete) {
            output.status = shared;
            output.failed_at = batch.failed_at;
        }
        if (output.status == Status::Complete) {
            for (const auto status : {observation.confirm(inputs[i], profile_.layout.actor.character.native_class),
                    observation.confirm(source.component, profile_.layout.pose.component.native_class),
                    observation.confirm(source.owner, profile_.layout.pose.component.native_class),
                    observation.confirm(source.world.component, profile_.layout.actor.scene_component.native_class)})
                if (status != Status::Complete) {
                    output.status = status;
                    output.failed_at = PoseFailureStage::IdentityClosing;
                    break;
                }
            if (output.status == Status::Complete) {
                output.status = scene.confirm_selector(source.world);
                output.failed_at = PoseFailureStage::SourceClosing;
            }
            if (output.status == Status::Complete) {
                output.status = rigs.confirm(source.asset);
                output.failed_at = PoseFailureStage::RigClosing;
            }
        }
        if (output.status != Status::Complete) {
            output.skeleton.reset();
            output.joint_status.fill(output.status);
            output.leader_pose = false;
        } else
            output.failed_at = PoseFailureStage::None;
    }
    batch.stamp.finished_qpc = clock_.now();
    return batch;
}
} // namespace valorant::game
