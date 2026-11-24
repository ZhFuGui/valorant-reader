#pragma once
#include "memory/backend.hpp"
#include <readers/memory.hpp>
#include <string_view>

namespace valorant::backends {
// 只适配公开 API；构造不发现目标，所选后端由产品导入的属性表决定。
[[nodiscard]] std::unique_ptr<memory::IBackend> make_shared_backend(std::unique_ptr<readers::memory::Memory>,
                                                                    std::string_view target_image);
} // namespace valorant::backends
