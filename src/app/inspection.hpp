#pragma once
#include <cstdint>
#include <iosfwd>

namespace valorant::app {
int inspect(std::uint32_t pid, std::ostream& output);
} // namespace valorant::app
