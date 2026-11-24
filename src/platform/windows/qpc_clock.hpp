#pragma once
#include "capture/source.hpp"
#include <readers/clock.hpp>

namespace valorant::platform::windows {
class QpcClock final : public capture::IClock {
  public:
    QpcClock();
    std::uint64_t now() const noexcept override;
    std::uint64_t frequency() const noexcept override { return frequency_; }

  private:
    readers::QpcClock clock_;
    std::uint64_t frequency_ = 0;
};
} // namespace valorant::platform::windows
