#pragma once
#include "model/geometry.hpp"
#include <array>
#include <cstdint>
#include <optional>

namespace valorant::model {
enum class JointRole {
    BodyRoot, Pelvis, LowerSpine, Chest, Neck, Head,
    LeftClavicle, LeftShoulder, LeftElbow, LeftHand, RightClavicle, RightShoulder, RightElbow, RightHand,
    LeftHip, LeftKnee, LeftFoot, LeftToe, RightHip, RightKnee, RightFoot, RightToe, Count
};
inline constexpr std::size_t main_joint_count = static_cast<std::size_t>(JointRole::Count);
struct JointLayout {
    JointRole role = JointRole::BodyRoot; // 主干骨点语义，不能直接用原生数组下标充当语义。
    std::uint32_t native_index = 0;      // 从当前资产名称与拓扑确认的原生下标。
    std::optional<JointRole> parent;     // 原生祖先链中最近的已选主干点；不是随意补画的解剖连线。
};
struct Skeleton {
    std::array<JointLayout, main_joint_count> layout;
    std::array<std::optional<Vector3>, main_joint_count> world_positions; // 缺值保留缺失，不填原点或历史姿态。
};
} // namespace valorant::model
