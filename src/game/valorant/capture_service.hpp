#pragma once
#include "capture/clock.hpp"
#include "capture/provider.hpp"
#include "memory/backend.hpp"
#include <functional>
#include <memory>
namespace valorant::game {
class CaptureService final : public capture::IProvider {
  public:
    CaptureService(std::unique_ptr<memory::IBackend>, std::optional<std::uint32_t> pid, const capture::IClock&,
                   std::function<void()> wake);
    ~CaptureService();
    capture::Rate subscribe(std::uint64_t, model::Stream, double, bool) override;
    void unsubscribe(std::uint64_t) override;
    model::StreamView view(model::Stream) const override;
    std::uint64_t barrier(model::Stream) const override;
    void step() override;
    unsigned wait_ms() const override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace valorant::game
