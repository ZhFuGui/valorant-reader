#pragma once
#include "memory/backend.hpp"

namespace valorant::runtime {
struct BindingHealth {
    memory::ProcessState state = memory::ProcessState::Unavailable;
    unsigned uncertain_checks = 0;
};
// 正常只观察已绑定对象；连续不确定时才独立查同 PID 的创建时间，不用新实例顶替旧实例。
[[nodiscard]] BindingHealth observe_binding(memory::IBackend&, const memory::ISession&, unsigned uncertain_checks);
} // namespace valorant::runtime
