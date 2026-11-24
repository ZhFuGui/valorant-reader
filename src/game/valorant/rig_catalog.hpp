#pragma once
#include "game/valorant/observation.hpp"
#include "game/valorant/rig.hpp"
#include <vector>

namespace valorant::game {
struct AssetRig {
    ResolutionStatus status = ResolutionStatus::NotConfigured;
    ObjectIdentity identity;
    MainRig joints{};                         // 只在 status 为 Complete 时有效。
};
// 只共享同批资产的解释数据和关闭证据，不保存上一批动态姿态。
class RigCatalog {
  public:
    RigCatalog(memory::IReadChannel& channel, const Profile& profile, Observation& observation)
        : channel_(channel), layout_(profile.layout), observation_(observation) {}
    [[nodiscard]] const AssetRig& observe(memory::Address asset);
    void close();
    [[nodiscard]] ResolutionStatus confirm(memory::Address asset) const;
    [[nodiscard]] std::size_t size() const noexcept { return records_.size(); }
    [[nodiscard]] std::size_t source_retries() const noexcept { return source_retries_; }
  private:
    struct BoneInfo { NameValue name; std::int32_t parent; bool operator==(const BoneInfo&) const = default; };
    static_assert(sizeof(BoneInfo) == 16);
    struct Record {
        AssetRig rig;
        memory::Address source = 0;
        ArrayView array;
        std::vector<BoneInfo> bones;
        ResolutionStatus closing = ResolutionStatus::NotConfigured;
    };
    ResolutionStatus load(memory::Address, Record&);
    Resolution<NamePoolView> read_pool();
    memory::IReadChannel& channel_;
    const offsets::Table& layout_;
    Observation& observation_;
    std::unordered_map<memory::Address, Record> records_;
    std::optional<Resolution<NamePoolView>> pool_;
    std::size_t source_retries_ = 0;
    bool closed_ = false;
};
} // namespace valorant::game
