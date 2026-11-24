#pragma once
#include "game/valorant/roots.hpp"
#include "model/skeleton.hpp"
#include <span>
#include <string>

namespace valorant::game {
struct NamedBone {
    std::string name;                // 已由当前资产名称来源解出的原生名称。
    std::int32_t parent = -1;        // 原生父骨下标；负一是根。
};
using MainRig = std::array<model::JointLayout, model::main_joint_count>;
// 纯 CPU 规则；要求名称、唯一性和实际祖先关系全部符合，未知模型不猜数字下标。
[[nodiscard]] Resolution<MainRig> select_main_rig(std::span<const NamedBone>);
} // namespace valorant::game
