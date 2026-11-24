#pragma once
#include <iosfwd>
#include <span>
#include <string_view>

namespace valorant::app {
enum class ExitCode : int {
    Success = 0, RuntimeFailure = 1, InvalidArguments = 2, NotConfigured = 3, ObservationUnavailable = 4
};
int run(std::span<const std::wstring_view> arguments, std::ostream& output, std::ostream& errors);
} // namespace valorant::app
