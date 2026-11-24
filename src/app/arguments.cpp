#include "app/arguments.hpp"
#include "transport/ipv4.hpp"
#include <limits>
#include <stdexcept>

namespace valorant::app {
namespace {
std::uint32_t number(std::wstring_view text) {
    std::uint32_t value = 0;
    if (text.empty())
        throw std::invalid_argument("Empty numeric argument");
    for (auto digit : text) {
        if (digit < L'0' || digit > L'9' ||
            value > (std::numeric_limits<std::uint32_t>::max() - static_cast<unsigned>(digit - L'0')) / 10)
            throw std::invalid_argument("Invalid numeric argument");
        value = value * 10 + static_cast<unsigned>(digit - L'0');
    }
    return value;
}
} // namespace
Arguments parse_arguments(std::span<const std::wstring_view> values) {
    Arguments result;
    if (values.empty())
        return result;
    if (values.size() == 1 && (values[0] == L"--help" || values[0] == L"-h")) {
        result.command = Command::Help;
        return result;
    }
    if (values.size() == 1 && values[0] == L"--check") {
        result.command = Command::Check;
        return result;
    }
    if (values[0] == L"--inspect") {
        if (values.size() != 3 || values[1] != L"--pid" || !(result.inspect_pid = number(values[2])))
            throw std::invalid_argument("Invalid inspection arguments");
        result.command = Command::Inspect;
        return result;
    }
    const std::size_t begin = values[0] == L"--serve" ? 1 : 0;
    if ((values.size() - begin) % 2)
        throw std::invalid_argument("Incomplete server arguments");
    unsigned seen = 0;
    for (std::size_t i = begin; i < values.size(); i += 2) {
        const auto key = values[i], value = values[i + 1];
        unsigned bit = 0;
        if (key == L"--pid") {
            bit = 1;
            const auto pid = number(value);
            if (!pid)
                throw std::invalid_argument("Invalid selected PID");
            result.server.pid = pid;
        } else if (key == L"--bind") {
            bit = 2;
            result.server.bind.clear();
            for (auto character : value) {
                if (character > 127)
                    throw std::invalid_argument("Invalid bind address");
                result.server.bind.push_back(static_cast<char>(character));
            }
            if (!transport::parse_ipv4(result.server.bind))
                throw std::invalid_argument("Invalid bind address");
        } else if (key == L"--port") {
            bit = 4;
            const auto port = number(value);
            if (!port || port > 65535)
                throw std::invalid_argument("Invalid port");
            result.server.port = static_cast<std::uint16_t>(port);
        } else if (key == L"--stop-event") {
            bit = 8;
            if (value.empty() || value.size() > 240)
                throw std::invalid_argument("Invalid stop event");
            result.server.stop_event = value;
        } else if (key == L"--max-connections") {
            bit = 16;
            const auto count = number(value);
            if (!count || count > 60)
                throw std::invalid_argument("Invalid connection budget");
            result.server.maximum_connections = count;
        } else
            throw std::invalid_argument("Unknown server argument");
        if (seen & bit)
            throw std::invalid_argument("Duplicate server argument");
        seen |= bit;
    }
    return result;
}
} // namespace valorant::app
