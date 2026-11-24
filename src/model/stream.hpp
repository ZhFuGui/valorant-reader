#pragma once
#include "model/camera.hpp"
#include "model/context.hpp"
#include "model/skeleton.hpp"
#include "model/snapshot.hpp"
#include <memory>
#include <variant>
#include <vector>

namespace valorant::model {
enum class Stream : unsigned { Positions = 1, Skeletons = 2, View = 3 };
inline constexpr std::size_t stream_index(Stream value) noexcept { return static_cast<unsigned>(value)-1; }
struct EntityPosition {
    std::uint64_t id = 0;
    std::optional<Vector3> value;
    std::optional<TeamIdentity> team;
    Relationship relationship = Relationship::Unknown;
    bool operator==(const EntityPosition&) const = default;
};
using MainPoints = std::array<std::optional<Vector3>, main_joint_count>;
struct EntitySkeleton {
    std::uint64_t id = 0;
    std::optional<MainPoints> value;
    bool operator==(const EntitySkeleton&) const = default;
};
struct Evidence {
    std::uint64_t id = 0, actor = 0, type = 0, outer = 0;
    std::uint32_t index = 0, serial = 0, status = 0, failed_at = 0;
    std::optional<bool> leader;
    std::vector<std::uint32_t> joints;
};
struct Diagnostics {
    std::uint64_t directory_generation = 0, classification_generation = 0;
    std::uint64_t slots = 0, empty = 0, invalid = 0, duplicates = 0;
    std::uint64_t planned = 0, unexecuted = 0, metadata_retries = 0;
    std::vector<Evidence> entities;
};
struct StreamState {
    Stream stream = Stream::Positions;
    CaptureStamp stamp;
    std::uint64_t config_revision = 0, period_ns = 0, generation = 0;
    std::variant<std::vector<EntityPosition>, std::vector<EntitySkeleton>, std::optional<Camera>> value;
    std::shared_ptr<const Diagnostics> diagnostic; // 仅有效调试需求时构造，不参与消费状态比较。
    std::optional<PositionContext> position_context; // 仅 Positions；本批完整替换，其他流不借用。
};
enum class Availability : unsigned { Waiting = 1, Ready = 2, Unavailable = 3, Stopping = 4 };
struct StreamView {
    Availability availability = Availability::Waiting;
    std::uint64_t epoch = 0, generation = 1, completed = 0, failed = 0, missed = 0;
    std::uint32_t reason = 1009;
    std::shared_ptr<const StreamState> state;
};
} // namespace valorant::model
