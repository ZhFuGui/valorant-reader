#pragma once
#include "model/geometry.hpp"
#include <cstdint>

namespace valorant::model {
struct Rotation {
    double pitch = 0, yaw = 0, roll = 0; // 原生欧拉角，单位为度，不以控制旋转代替相机旋转。
    bool operator==(const Rotation&) const = default;
};
enum class Projection { Unknown, Perspective, Orthographic };
struct Camera {
    Vector3 location;                  // 相机缓存的世界位置，与身体位置分开。
    Rotation rotation;                 // 相机缓存的已求值旋转。
    float engine_timestamp = 0;        // 原生缓存更新时间，不属于 QPC 时间域。
    float fov = 0;                     // 原生 FOV 值；最终渲染轴向/窗口处理仍需消费者另行核验。
    float aspect_ratio = 0;            // 原生期望画幅比例。
    float ortho_width = 0;             // 正交宽度，仅在相应投影模式下解释。
    bool constrain_aspect_ratio = false; // 原生画幅约束位。
    std::uint8_t raw_projection = 0;   // 未识别枚举值保留原值，不猜测成透视。
    Projection projection = Projection::Unknown;
    bool operator==(const Camera&) const = default;
};
} // namespace valorant::model
