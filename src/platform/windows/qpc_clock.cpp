#include "platform/windows/qpc_clock.hpp"
#include <stdexcept>

namespace valorant::platform::windows {
QpcClock::QpcClock() {
    frequency_ = clock_.frequency();
    if (!frequency_)
        throw std::runtime_error("QPC is unavailable");
}

std::uint64_t QpcClock::now() const noexcept {
    return clock_.now();
}
} // namespace valorant::platform::windows
