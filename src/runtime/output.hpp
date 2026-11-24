#pragma once
#include "model/snapshot.hpp"
#include <memory>

namespace valorant::runtime {
// 发布只接收已完成且不再修改的结果；编码、订阅和 socket 由后续适配层实现。
class IOutput {
  public:
    virtual ~IOutput() = default;
    virtual void publish(std::shared_ptr<const model::Snapshot>) = 0;
    virtual void invalidate(std::uint64_t epoch) = 0;
};
} // namespace valorant::runtime
