#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>

namespace valorant::runtime {
class RecoveryBackoff {
  public:
    [[nodiscard]] std::uint64_t retry_at(std::uint64_t now) noexcept {
        constexpr std::array<unsigned, 6> milliseconds{100, 200, 500, 1000, 2000, 5000};
        const auto index = step_;
        step_ = (std::min)(step_ + 1, milliseconds.size() - 1);
        healthy_since_.reset();
        return now + std::uint64_t{milliseconds[index]} * 1000000 + now % 17000000;
    }
    void progress(std::uint64_t now) noexcept {
        if (!healthy_since_)
            healthy_since_ = now;
        if (now >= *healthy_since_ && now - *healthy_since_ >= 5000000000ULL)
            step_ = 0;
    }

  private:
    std::size_t step_ = 0;
    std::optional<std::uint64_t> healthy_since_;
};
} // namespace valorant::runtime
