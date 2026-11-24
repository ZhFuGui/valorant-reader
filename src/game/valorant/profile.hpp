#pragma once
#include "game/valorant/offsets.hpp"
#include <cstdint>
#include <optional>
#include <string_view>

namespace valorant::game {
enum class ProfileState { Unconfigured, UnderAnalysis, Validated };

struct BuildIdentity {
    std::uint32_t timestamp = 0;     // PE TimeDateStamp，仅与其他身份字段联合使用。
    std::uint32_t image_bytes = 0;   // PE SizeOfImage。
    std::uint32_t checksum = 0;      // 原始运行映像的 PE CheckSum；不能使用重建 dump 时被清零的值。
    bool operator==(const BuildIdentity&) const = default;
};

struct Profile {
    std::string_view revision;            // 经核验的游戏版本或本地规则版本。
    std::optional<BuildIdentity> image;   // 对应模块的构建身份；缺失表示资料尚未接入。
    offsets::Table layout;                // 当前构建的引擎布局；未定位成员保持缺失。
    ProfileState state = ProfileState::Unconfigured; // 分析中的候选布局不能作为就绪配置。

    [[nodiscard]] bool ready() const noexcept {
        return state == ProfileState::Validated && image && image->image_bytes > 0 && !revision.empty() &&
               layout.has_world_discovery_layout();
    }
};

// 当前构建的分析结果；未验证的根解析器和布局保持缺失，不复用其他游戏的值。
const Profile& current_profile() noexcept;
} // namespace valorant::game
