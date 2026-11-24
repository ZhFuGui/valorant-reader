#pragma once
#include "capture/clock.hpp"
#include "memory/backend.hpp"
#include "model/snapshot.hpp"
#include <memory>

namespace valorant::capture {
class ISource {
  public:
    virtual ~ISource() = default;
    virtual model::Snapshot capture(std::uint64_t sequence, std::uint64_t epoch) = 0;
};

class ISourceFactory {
  public:
    virtual ~ISourceFactory() = default;
    // 先销毁采集器及其读取任务，再释放所借用的会话。
    virtual std::unique_ptr<ISource> open(const memory::ISession&, const IClock&) const = 0;
};
} // namespace valorant::capture
