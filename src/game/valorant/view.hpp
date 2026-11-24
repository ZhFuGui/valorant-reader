#pragma once
#include "capture/source.hpp"
#include "game/valorant/observation.hpp"
#include "model/camera.hpp"

namespace valorant::game {
struct WorldContext {
    memory::Address engine = 0, viewport = 0, world = 0;
    bool operator==(const WorldContext&) const = default;
};
struct ViewBatch {
    model::CaptureStamp stamp;
    Resolution<model::Camera> camera; // 相机独立终态；无需身体、人物目录或骨骼批次。
    std::optional<WorldContext> context; // 当前相机实际核验的世界来源，用于跨流 epoch 隔离，不传入消费模型。
};
class ViewReader {
  public:
    ViewReader(memory::IReadChannel& channel, const Profile& profile, ModuleView image, const capture::IClock& clock)
        : channel_(channel), profile_(profile), image_(image), clock_(clock) {}
    [[nodiscard]] ViewBatch read(std::uint64_t sequence, std::uint64_t epoch, std::uint32_t local_slot = 0) const;
  private:
    memory::IReadChannel& channel_;
    const Profile& profile_;
    ModuleView image_;
    const capture::IClock& clock_;
};
} // namespace valorant::game
