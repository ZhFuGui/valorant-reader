#pragma once
#include "game/valorant/directory.hpp"
#include "game/valorant/objects.hpp"
#include <array>

namespace valorant::game {
enum class ActorKind { OtherActor, Pawn, Character, ShooterCharacter, Count };
enum class ClassificationState {
    Unexecuted, Classified, NotActor, DefaultObject, Destroying, ReadUnavailable, SourceChanged, InvalidData, Unsupported, Count
};

struct ClassifiedActor {
    std::uint32_t source_slot = 0;       // 首次出现的名单槽位；重复来源由原目录保留。
    memory::Address address = 0;         // 本次计划尝试的地址，不作为永久实体 ID。
    ClassificationState state = ClassificationState::Unexecuted; // 此次唯一终态。
    std::optional<ActorKind> kind;       // 仅 Classified 时有效，不表示真人、BOT、敌我或存活状态。
    std::optional<ObjectIdentity> identity; // 仅 Classified 时保留本次核验过的身份，供本批属性任务使用。
};

struct ClassificationBatch {
    ResolutionStatus source_status = ResolutionStatus::NotConfigured; // 共享来源及世界上下文的关闭结果。
    std::uint32_t planned = 0;          // 本次完整唯一地址名单的任务数。
    std::array<std::uint32_t, static_cast<std::size_t>(ClassificationState::Count)> terminal_counts{};
    std::array<std::uint32_t, static_cast<std::size_t>(ActorKind::Count)> kind_counts{};
    std::size_t type_records = 0;       // 本批建立的类型记录数，含未能读全的记录；元数据不跨批复用。
    std::vector<ClassifiedActor> actors; // 每个唯一地址一个结果，不追加虚构实体。

    [[nodiscard]] bool complete() const noexcept {
        return source_status == ResolutionStatus::Complete && terminal_counts[0] == 0;
    }
};

class ClassificationReader {
  public:
    ClassificationReader(memory::IReadChannel& channel, const Profile& profile, ModuleView image) noexcept
        : channel_(channel), profile_(profile), image_(image) {}
    // 完整处理目录的所有唯一地址；必要核验失败时身份和分类保持缺失，不复用上一批结果。
    [[nodiscard]] ClassificationBatch read(const ActorDirectory&) const;

  private:
    memory::IReadChannel& channel_;
    const Profile& profile_;
    ModuleView image_;
};
} // namespace valorant::game
