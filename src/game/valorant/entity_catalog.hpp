#pragma once
#include "game/valorant/classification.hpp"
#include <unordered_map>
#include <unordered_set>

namespace valorant::game {
struct RosterEntry {
    std::uint64_t id=0;
    std::optional<ObjectIdentity> identity; // 本轮分类未确认时只保留身份占位，不保留动态值。
};
class EntityCatalog {
  public:
    void clear() { records_.clear();members_.clear();next_=1; }
    void retain(const ActorDirectory&);
    std::vector<RosterEntry> classify(const ClassificationBatch&);
    bool contains(std::uint64_t) const;
    [[nodiscard]] std::optional<std::uint64_t> find(const ObjectIdentity&) const;
  private:
    struct Record {std::uint64_t id;ObjectIdentity identity;};
    std::uint64_t next_=1;
    std::unordered_map<memory::Address,Record> records_;
    std::unordered_set<memory::Address> members_;
};
} // namespace valorant::game
