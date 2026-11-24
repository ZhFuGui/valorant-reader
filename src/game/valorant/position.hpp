#pragma once
#include "capture/source.hpp"
#include "game/valorant/directory.hpp"
#include "game/valorant/scene.hpp"
#include "game/valorant/context.hpp"
#include "game/valorant/team.hpp"

namespace valorant::game {
struct CharacterPosition {
    ObjectIdentity identity;                  // 输入的分类身份，采样期间重新核验。
    Resolution<model::Vector3> position;       // Actor 根组件的原生世界位置，不是脚底或网格原点。
    Resolution<TeamSample> team;              // 身份、位置和队伍分别失败，不相互伪造缺值。
};
struct PositionBatch {
    model::CaptureStamp stamp;
    ResolutionStatus source_status = ResolutionStatus::NotConfigured;
    std::vector<CharacterPosition> characters; // 每个输入对象一个终态；失败没有虚构坐标。
    ContextSample context;
    Resolution<TeamSample> reference_team;
};
class PositionReader {
  public:
    PositionReader(memory::IReadChannel& channel, const Profile& profile, ModuleView image, const capture::IClock& clock)
        : channel_(channel), profile_(profile), image_(image), clock_(clock) {}
    [[nodiscard]] PositionBatch read(const ActorDirectory&, std::span<const ObjectIdentity>,
                                     std::uint64_t sequence, std::uint64_t epoch) const;
  private:
    memory::IReadChannel& channel_;
    const Profile& profile_;
    ModuleView image_;
    const capture::IClock& clock_;
};
} // namespace valorant::game
