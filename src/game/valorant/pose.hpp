#pragma once
#include "capture/source.hpp"
#include "game/valorant/directory.hpp"
#include "game/valorant/rig_catalog.hpp"
#include "game/valorant/scene.hpp"

namespace valorant::game {
// 只标识失败发生在哪一段，不把读取失败、缺失和来源变化混为同一个原因。
enum class PoseFailureStage {
    None,            // 共同来源有效；单点缺失仍由 joint_status 单独说明。
    Layout,          // 当前格式描述尚未配置或未支持。
    Character,       // 输入人物的身份或原生类型核验。
    Mesh,            // 人物 Mesh 引用或网格组件类型核验。
    Ready,           // 原生姿态读取前置状态。
    Asset,           // 实际使用的骨骼资产引用。
    Rig,             // 资产身份、名称或参考骨架解释。
    WorldTransform,  // 原组件世界变换及其选择分支。
    Leader,          // Leader 弱引用与组件身份。
    Buffer,          // 当前姿态缓冲选择或数组描述。
    Mapping,         // Leader 骨点映射数组描述。
    SourceClosing,   // 本次使用的 Mesh、资产、Leader、缓冲或变换链关闭核验。
    IdentityClosing, // 人物和组件身份、类型解释来源的关闭核验。
    RigClosing,      // 同批共享资产、名称密钥或拓扑的关闭核验。
    WorldClosing,    // 世界和主关卡上下文的关闭核验。
    SharedSources    // 对象注册表或共享元类型等批次来源。
};

struct CharacterPose {
    ObjectIdentity identity;             // 输入角色身份，在本批重新核验。
    ResolutionStatus status = ResolutionStatus::NotConfigured; // 骨架解释和共同来源的结果。
    PoseFailureStage failed_at = PoseFailureStage::None; // 失败阶段；不增加诊断专用目标读取。
    std::optional<model::Skeleton> skeleton; // 只有共同来源成功才保留；单点缺失由下列状态说明。
    std::array<ResolutionStatus, model::main_joint_count> joint_status{}; // 每个主干点的读取终态。
    bool leader_pose = false;            // 本次是否实际采用有效 Leader 及映射。
};
struct PoseBatch {
    model::CaptureStamp stamp;
    ResolutionStatus source_status = ResolutionStatus::NotConfigured;
    PoseFailureStage failed_at = PoseFailureStage::None; // 仅表示共享来源的失败阶段，非逐人物汇总。
    std::size_t assets = 0;              // 同批共享解析的骨骼资产数。
    std::size_t metadata_source_retries = 0; // 名称来源暂时变化而重新取得完整证据的次数；不含普通缺页。
    std::vector<CharacterPose> characters;
};
class PoseReader {
  public:
    PoseReader(memory::IReadChannel& channel, const Profile& profile, ModuleView image, const capture::IClock& clock)
        : channel_(channel), profile_(profile), image_(image), clock_(clock) {}
    [[nodiscard]] PoseBatch read(const ActorDirectory&, std::span<const ObjectIdentity>,
                                 std::uint64_t sequence, std::uint64_t epoch) const;
  private:
    memory::IReadChannel& channel_;
    const Profile& profile_;
    ModuleView image_;
    const capture::IClock& clock_;
};
} // namespace valorant::game
