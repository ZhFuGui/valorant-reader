#pragma once
#include <cmath>

namespace valorant::model {
struct Vector3 {
    double x = 0, y = 0, z = 0; // 原生世界或组件空间坐标，具体空间由所属字段说明。
    bool operator==(const Vector3&) const = default;
};
struct Quaternion {
    double x = 0, y = 0, z = 0, w = 1; // 分量顺序为 XYZW。
};
struct Transform {
    Quaternion rotation;       // 当前组件到世界空间的旋转。
    Vector3 translation;       // 世界空间平移。
    Vector3 scale{1, 1, 1};     // 旋转前施加的三轴缩放。
};
inline bool finite(Vector3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
inline bool valid(const Transform& value) noexcept {
    const auto& q = value.rotation;
    const auto norm = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
    return finite(value.translation) && finite(value.scale) && std::isfinite(norm) && std::abs(norm - 1) < 0.01;
}
// 输入已经是组件空间位置；不能再次沿参考骨架累计父骨变换。
inline Vector3 transform_point(const Transform& t, Vector3 point) noexcept {
    const Vector3 v{point.x*t.scale.x, point.y*t.scale.y, point.z*t.scale.z};
    const auto& q = t.rotation;
    const Vector3 twice{2*(q.y*v.z-q.z*v.y), 2*(q.z*v.x-q.x*v.z), 2*(q.x*v.y-q.y*v.x)};
    return {v.x + q.w*twice.x + q.y*twice.z - q.z*twice.y + t.translation.x,
            v.y + q.w*twice.y + q.z*twice.x - q.x*twice.z + t.translation.y,
            v.z + q.w*twice.z + q.x*twice.y - q.y*twice.x + t.translation.z};
}
} // namespace valorant::model
