#pragma once
#include "game/valorant/roots.hpp"
#include <vector>

namespace valorant::game {
enum class ActorSlotState { Unique, Empty, InvalidAddress, Duplicate };

struct ActorSlot {
    memory::Address address = 0;                       // 此来源槽位的原始 Actor 引用，不对外作为实体 ID。
    std::uint32_t first_slot = 0;                       // 重复项指向首次出现的槽位；其他项等于自身下标。
    ActorSlotState state = ActorSlotState::Empty;       // 名单层终态，不代表类型已识别或属性已读取。
};

struct DirectoryCoverage {
    std::uint32_t slots = 0;             // 成功复制的完整来源槽位数。
    std::uint32_t unique = 0;            // 地址格式有效且首次出现的槽位数，仍需逐对象生命周期核验。
    std::uint32_t empty = 0;             // 空引用槽位数。
    std::uint32_t invalid = 0;           // 非法用户地址或不符合对象对齐的引用数。
    std::uint32_t duplicates = 0;        // 已保留来源关系的重复地址数。
};

struct ActorDirectory {
    memory::Address engine = 0;          // 开始观察时实际使用的引擎来源。
    memory::Address viewport = 0;        // 开始观察时实际使用的视口来源。
    memory::Address world = 0;           // 本次所属世界，仅供采集内部核验。
    memory::Address level = 0;           // 本次主 PersistentLevel，不扫描其他关卡。
    DirectoryCoverage coverage;         // 全部槽位的守恒统计，不把有效地址数称为活体人数。
    std::vector<ActorSlot> slots;        // 保持原始顺序；分类和属性读取由后续独立采集器处理。
};

class DirectoryReader {
  public:
    DirectoryReader(memory::IReadChannel& channel, const Profile& profile, ModuleView image) noexcept
        : channel_(channel), profile_(profile), roots_(channel, profile, image) {}

    // 一次完整复制并核验头、根与归属；失败不返回部分名单，不在内部无限重试。
    [[nodiscard]] Resolution<ActorDirectory> read() const;
    // 仅复验世界上下文，不隐式再复制整张名单；正常成员变化在后续对象观察中分别处理。
    [[nodiscard]] ResolutionStatus confirm_context(const ActorDirectory&) const noexcept;

  private:
    memory::IReadChannel& channel_;
    const Profile& profile_;
    RootResolver roots_;
};
} // namespace valorant::game
