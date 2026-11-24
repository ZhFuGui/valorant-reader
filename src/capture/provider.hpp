#pragma once
#include "capture/demand.hpp"
namespace valorant::capture {
class IProvider {
  public:
    virtual ~IProvider() = default;
    virtual Rate subscribe(std::uint64_t, model::Stream, double, bool) = 0;
    virtual void unsubscribe(std::uint64_t) = 0;
    virtual model::StreamView view(model::Stream) const = 0;
    // 可跨线程调用的发布资格核验；不得读取 mutable 目录、做内存读取或等待 worker。
    virtual bool current(model::Stream, std::uint64_t generation, std::uint64_t epoch) const noexcept = 0;
    virtual std::uint64_t barrier(model::Stream) const = 0;
    virtual void step() = 0; // 所有者执行 CPU 提交及派发；这里不做慢读或绑定查询。
    virtual unsigned wait_ms() const = 0;
};
} // namespace valorant::capture
