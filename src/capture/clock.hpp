#pragma once
#include <cstdint>
namespace valorant::capture {
class IClock {
  public:
    virtual ~IClock() = default;
    virtual std::uint64_t now() const noexcept = 0;
    virtual std::uint64_t frequency() const noexcept = 0;
};
inline std::uint64_t nanoseconds(const IClock& clock) noexcept {
    const auto ticks = clock.now(), frequency = clock.frequency();
    return (ticks/frequency)*1000000000ULL + static_cast<std::uint64_t>(
        static_cast<long double>(ticks%frequency)*1000000000.0L/frequency);
}
} // namespace valorant::capture
