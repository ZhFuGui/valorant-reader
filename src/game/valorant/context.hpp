#pragma once
#include "game/valorant/observation.hpp"
#include "model/context.hpp"

namespace valorant::game {
struct ContextSample {
    Resolution<model::MapIdentity> map;
    Resolution<std::optional<ObjectIdentity>> local_character;    // 空值表示已确认没有受控 Character。
    Resolution<std::optional<ObjectIdentity>> local_player_state; // 独立于人物身体和相机。
};
// 借用当前批次 Observation：read → close_sources → Observation.close → finish。
class ContextReader {
  public:
    ContextReader(memory::IReadChannel& channel, const Profile& profile, Observation& observation)
        : channel_(channel), layout_(profile.layout), observation_(observation) {}
    void read(memory::Address world);
    void close_sources();
    [[nodiscard]] ContextSample finish() const; // 关闭后只查询已记录证据，不再读内存。
    [[nodiscard]] const ContextSample& opened() const noexcept { return value_; }

  private:
    struct Proof {
        ObjectMatch object;
        const offsets::Root* type;
    };
    struct Link {
        memory::Address owner;
        const offsets::Member* member;
        memory::Address value;
    };
    struct Group {
        ResolutionStatus status = ResolutionStatus::Complete;
        std::vector<Proof> objects;
        std::vector<Link> links;
    };
    Resolution<ObjectMatch> object(Group&, memory::Address, const offsets::Root&);
    Resolution<memory::Address> link(Group&, memory::Address, const offsets::Member&);
    ResolutionStatus confirm(const Group&) const;
    void read_map(memory::Address);
    void read_local(memory::Address);
    memory::IReadChannel& channel_;
    const offsets::Table& layout_;
    Observation& observation_;
    ContextSample value_;
    Group map_, path_, body_, state_;
    std::optional<NamePoolView> names_;
    memory::Address players_ = 0, player_ = 0;
    bool read_ = false, closed_ = false;
};
} // namespace valorant::game
