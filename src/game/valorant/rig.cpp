#include "game/valorant/rig.hpp"
#include <algorithm>
#include <vector>

namespace valorant::game {
Resolution<MainRig> select_main_rig(std::span<const NamedBone> bones) {
    using Status = ResolutionStatus;
    using Role = model::JointRole;
    constexpr std::array<std::string_view, model::main_joint_count> names{
        "Splitter", "Pelvis", "Spine1", "Spine4", "Neck", "Head", "L_Clavicle", "L_Shoulder", "L_Elbow", "L_Hand",
        "R_Clavicle", "R_Shoulder", "R_Elbow", "R_Hand", "L_Hip", "L_Knee", "L_Foot", "L_Toe",
        "R_Hip", "R_Knee", "R_Foot", "R_Toe"};
    constexpr std::array<int, model::main_joint_count> parents{-1,0,0,2,3,4,3,6,7,8,3,10,11,12,1,14,15,16,1,18,19,20};
    if (bones.empty())
        return {Status::UnsupportedCode};
    std::vector<unsigned char> visited(bones.size());
    for (std::size_t i = 0; i < bones.size(); ++i) {
        if (bones[i].parent < -1 || (bones[i].parent >= 0 && static_cast<std::size_t>(bones[i].parent) >= bones.size()))
            return {Status::InvalidData};
        std::vector<std::size_t> path;
        auto node = static_cast<std::int64_t>(i);
        while (node >= 0 && visited[static_cast<std::size_t>(node)] != 2) {
            const auto current = static_cast<std::size_t>(node);
            if (visited[current] == 1)
                return {Status::InvalidData};
            visited[current] = 1;
            path.push_back(current);
            node = bones[current].parent;
            if (node < -1 || (node >= 0 && static_cast<std::size_t>(node) >= bones.size()))
                return {Status::InvalidData};
        }
        for (const auto entry : path) visited[entry] = 2;
    }
    MainRig result;
    std::array<bool, model::main_joint_count> found{};
    for (std::size_t i = 0; i < bones.size(); ++i) {
        const auto entry = std::find(names.begin(), names.end(), bones[i].name);
        if (entry == names.end()) continue;
        const auto slot = static_cast<std::size_t>(entry - names.begin());
        if (found[slot]) return {Status::UnsupportedCode};
        found[slot] = true;
        result[slot] = {static_cast<Role>(slot), static_cast<std::uint32_t>(i), {}};
    }
    if (std::find(found.begin(), found.end(), false) != found.end())
        return {Status::UnsupportedCode};
    for (std::size_t i = 0; i < result.size(); ++i) {
        auto node = bones[result[i].native_index].parent;
        int parent_role = -1;
        while (node >= 0) {
            const auto found_parent = std::find_if(result.begin(), result.end(), [&](const auto& entry) {
                return entry.native_index == static_cast<std::uint32_t>(node);
            });
            if (found_parent != result.end()) {
                parent_role = static_cast<int>(found_parent - result.begin());
                break;
            }
            node = bones[static_cast<std::size_t>(node)].parent;
        }
        if (parent_role != parents[i]) return {Status::UnsupportedCode};
        if (parent_role >= 0) result[i].parent = static_cast<Role>(parent_role);
    }
    return {Status::Complete, result};
}
} // namespace valorant::game
