#include "game/valorant/entity_catalog.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace valorant::game {
namespace {
bool same_life(const ObjectIdentity& a,const ObjectIdentity& b) {
    return a.address==b.address && a.index==b.index && a.type==b.type && a.name==b.name &&
        a.outer==b.outer && a.registry_chunk==b.registry_chunk && (!a.weak_serial || a.weak_serial==b.weak_serial);
}
}
void EntityCatalog::retain(const ActorDirectory& directory) {
    members_.clear();members_.reserve(directory.coverage.unique);
    for(const auto& slot:directory.slots) if(slot.state==ActorSlotState::Unique) members_.insert(slot.address);
    std::erase_if(records_,[&](const auto& row){return !members_.contains(row.first);});
}
std::vector<RosterEntry> EntityCatalog::classify(const ClassificationBatch& batch) {
    std::vector<RosterEntry> result;
    for(const auto& actor:batch.actors) {
        if(!members_.contains(actor.address)) continue;
        auto old=records_.find(actor.address);
        if(actor.state==ClassificationState::Classified && actor.identity &&
           (actor.kind==ActorKind::Character || actor.kind==ActorKind::ShooterCharacter)) {
            if(old==records_.end() || !same_life(old->second.identity,*actor.identity)) {
                if(next_==(std::numeric_limits<std::uint64_t>::max)()) throw std::overflow_error("1008");
                old=records_.insert_or_assign(actor.address,Record{next_++,*actor.identity}).first;
            } else old->second.identity=*actor.identity;
            result.push_back({old->second.id,actor.identity});
        } else if(actor.state==ClassificationState::Classified || actor.state==ClassificationState::NotActor ||
                  actor.state==ClassificationState::DefaultObject || actor.state==ClassificationState::Destroying) {
            records_.erase(actor.address);
        } else if(old!=records_.end()) result.push_back({old->second.id,{}});
    }
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    return result;
}
bool EntityCatalog::contains(std::uint64_t id) const {
    return std::any_of(records_.begin(),records_.end(),[&](const auto& row){return row.second.id==id;});
}
std::optional<std::uint64_t> EntityCatalog::find(const ObjectIdentity& identity) const {
    const auto row = records_.find(identity.address);
    if (row == records_.end() || !members_.contains(identity.address) || row->second.identity != identity)
        return {};
    return row->second.id;
}
} // namespace valorant::game
