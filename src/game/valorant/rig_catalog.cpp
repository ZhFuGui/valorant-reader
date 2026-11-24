#include "game/valorant/rig_catalog.hpp"
#include "game/valorant/reading.hpp"

namespace valorant::game {
using Status = ResolutionStatus;
using memory::Address;

const AssetRig& RigCatalog::observe(Address asset) {
    const auto found = records_.find(asset);
    if (found != records_.end()) return found->second.rig;
    if (closed_) {
        static const AssetRig missing;
        return missing;
    }
    auto& record = records_.try_emplace(asset).first->second;
    record.rig.status = load(asset, record);
    return record.rig;
}

ResolutionStatus RigCatalog::load(Address asset, Record& record) {
    if (layout_.pose.reference_skeleton.bone_info_stride.value != sizeof(BoneInfo) ||
        layout_.pose.bone_info.name.value != 0u || layout_.pose.bone_info.parent.value != 12u)
        return Status::UnsupportedCode;
    const auto identity = observation_.require(asset, layout_.pose.asset.native_class);
    if (!identity.complete()) return identity.status;
    record.rig.identity = identity.value;
    const auto skeleton = detail::field_address(asset, layout_.pose.asset.reference_skeleton, 1);
    record.source = detail::field_address(skeleton, layout_.pose.reference_skeleton.bone_info, 16);
    if (!skeleton || !record.source) return Status::NotConfigured;
    const auto array = read_array_view(channel_, record.source, layout_.foundation.array, sizeof(BoneInfo));
    if (!array.complete()) return array.status;
    record.array = array.value;
    record.bones.resize(static_cast<std::size_t>(array.value.count));
    const auto bytes = record.bones.size()*sizeof(BoneInfo);
    if (bytes && !channel_.read(array.value.data, record.bones.data(), bytes).complete(bytes))
        return Status::ReadUnavailable;
    if (!pool_) pool_ = read_pool();
    if (!pool_->complete()) return pool_->status;
    NameReader names(channel_, layout_.identity.names, pool_->value);
    std::vector<NamedBone> decoded;
    decoded.reserve(record.bones.size());
    for (const auto& bone : record.bones) {
        const auto name = names.name(bone.name);
        if (!name.complete()) return name.status;
        decoded.push_back({name.value, bone.parent});
    }
    const auto selected = select_main_rig(decoded);
    if (selected.complete()) record.rig.joints = selected.value;
    return selected.status;
}

Resolution<NamePoolView> RigCatalog::read_pool() {
    auto pool = observation_.roots().name_pool();
    // 已观察到原生来源正在更新时仅重取一次完整证据；不放宽锁/种子/密钥核验。
    // 普通缺页、非法数据及未知规则不立即重试，也不重绑进程或复用上一批来源。
    if (pool.status == Status::SourceChanged) {
        ++source_retries_;
        pool = observation_.roots().name_pool();
    }
    return pool;
}

void RigCatalog::close() {
    if (closed_) return;
    closed_ = true;
    Status pool_status = Status::NotConfigured;
    if (pool_ && pool_->complete()) {
        const auto closing = read_pool();
        pool_status = !closing.complete() ? closing.status
            : closing.value.allocator != pool_->value.allocator || closing.value.key != pool_->value.key
                ? Status::SourceChanged : Status::Complete;
    }
    for (auto& [asset, record] : records_) {
        (void)asset;
        record.closing = record.rig.status;
        if (record.closing != Status::Complete) continue;
        record.closing = pool_status;
        if (pool_status != Status::Complete) continue;
        std::vector<BoneInfo> closing(record.bones.size());
        const auto bytes = closing.size()*sizeof(BoneInfo);
        if (bytes && !channel_.read(record.array.data, closing.data(), bytes).complete(bytes))
            record.closing = Status::ReadUnavailable;
        else if (closing != record.bones)
            record.closing = Status::SourceChanged;
        const auto header = read_array_view(channel_, record.source, layout_.foundation.array, sizeof(BoneInfo));
        if (!header.complete()) record.closing = header.status;
        else if (header.value != record.array) record.closing = Status::SourceChanged;
    }
}

ResolutionStatus RigCatalog::confirm(Address asset) const {
    const auto found = records_.find(asset);
    if (!closed_ || found == records_.end()) return Status::NotConfigured;
    const auto& record = found->second;
    if (record.closing != Status::Complete) return record.closing;
    return observation_.confirm(record.rig.identity, layout_.pose.asset.native_class);
}
} // namespace valorant::game
