#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace valorant::model {
struct MapIdentity {
    std::string package_path; // 原生 UPackage 完整名称，不提供消费端底图换算参数。
    std::string world_name;   // 当前 UWorld 的原生名称，与包路径分别表达。
    bool operator==(const MapIdentity&) const = default;
};
enum class LocalCharacterState : unsigned { Unknown = 0, Absent = 1, Unresolved = 2, Resolved = 3 };
struct LocalCharacter {
    LocalCharacterState state = LocalCharacterState::Unknown;
    std::optional<std::uint64_t> entity_id; // 仅 Resolved 存在，作用域为同一 Reader 实例及 epoch。
    bool operator==(const LocalCharacter&) const = default;
};
struct TeamIdentity {
    std::uint8_t bomb_team = 0; // 已核验的 BombTeam 原生字节；零合法，0xfe 的关系仍是未知。
    bool operator==(const TeamIdentity&) const = default;
};
enum class Relationship : unsigned { Unknown = 0, Self = 1, Ally = 2, Enemy = 3 };
struct PositionContext {
    std::optional<MapIdentity> map;
    LocalCharacter local;
    std::optional<TeamIdentity> reference_team; // 本地控制器的 PlayerState，绝不借用观战目标。
    bool operator==(const PositionContext&) const = default;
};
} // namespace valorant::model
