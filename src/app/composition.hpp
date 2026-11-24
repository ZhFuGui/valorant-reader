#pragma once
#include "memory/backend.hpp"

namespace valorant::app {
// Hyper-mem 是唯一后端；构造只组合依赖，显式查找或绑定时才执行主机握手。
std::unique_ptr<memory::IBackend> create_backend();
} // namespace valorant::app
