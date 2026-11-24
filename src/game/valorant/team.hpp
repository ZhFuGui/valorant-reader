#pragma once
#include "game/valorant/components.hpp"
#include "model/context.hpp"

namespace valorant::game {
struct TeamSample {
    std::optional<model::TeamIdentity> identity;
    bool comparable = false; // 当前组件确实使用已核验的原生关系实现，不传给消费端。
};
// 一批内共享 PlayerState/组件证据；关闭后不可继续读取，失败不影响人物坐标。
class TeamReader {
  public:
    TeamReader(memory::IReadChannel& channel, const Profile& profile, ModuleView image, Observation& observation)
        : channel_(channel), layout_(profile.layout), image_(image), observation_(observation) {}
    [[nodiscard]] Resolution<TeamSample> read_character(const ObjectIdentity&);
    [[nodiscard]] Resolution<TeamSample> read_player_state(const ObjectIdentity&);
    void close_sources();
    [[nodiscard]] Resolution<TeamSample> result(memory::Address) const;
    [[nodiscard]] static model::Relationship relationship(const TeamSample& character,
                                                          const TeamSample& reference) noexcept;

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
    struct Record {
        Resolution<TeamSample> sample;
        ResolutionStatus closing = ResolutionStatus::Complete;
        std::vector<Proof> proofs;
        std::vector<Link> links;
        std::optional<ComponentSet> components;
        memory::Address dependency = 0, component = 0, vtable = 0, function = 0;
    };
    [[nodiscard]] Resolution<ObjectMatch> object(Record&, memory::Address, const offsets::Root&);
    [[nodiscard]] Resolution<memory::Address> reference(Record&, memory::Address, const offsets::Member&);
    [[nodiscard]] Resolution<TeamSample> component(Record&, memory::Address);
    [[nodiscard]] Resolution<TeamSample> owned(Record&, memory::Address);
    memory::IReadChannel& channel_;
    const offsets::Table& layout_;
    ModuleView image_;
    Observation& observation_;
    std::unordered_map<memory::Address, Record> records_;
    bool closed_ = false;
};
} // namespace valorant::game
