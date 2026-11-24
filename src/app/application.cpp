#include "app/application.hpp"
#include "app/arguments.hpp"
#include "app/composition.hpp"
#include "app/inspection.hpp"
#include "app/server.hpp"
#include "game/valorant/profile.hpp"
#include "platform/windows/qpc_clock.hpp"
#include <ostream>

namespace valorant::app {
int run(std::span<const std::wstring_view> arguments, std::ostream& output, std::ostream& errors) {
    Arguments options;
    try {
        options = parse_arguments(arguments);
    } catch (const std::invalid_argument&) {
        errors << "Invalid arguments. Use --help.\n";
        return static_cast<int>(ExitCode::InvalidArguments);
    }
    if (options.command == Command::Help) {
        output << "valorant-reader [--serve] [--pid <PID>] [--bind <IPv4>] [--port <port>] "
                  "[--max-connections <1..60>] [--stop-event <name>]\n"
                  "Default: serve and discover VALORANT-Win64-Shipping.exe automatically.\n"
                  "--check reports readiness without accessing a game process.\n"
                  "--inspect --pid <PID> performs one read-only inspection.\n"
                  "--pid pins one process instance for controlled diagnostics.\n";
        return static_cast<int>(ExitCode::Success);
    }
    if (options.command == Command::Serve)
        return serve(options.server, output, errors);
    if (options.command == Command::Inspect)
        return inspect(options.inspect_pid, output);
    const platform::windows::QpcClock clock;
    const auto backend = create_backend();
    const auto& profile = game::current_profile();
    output << "{\"application\":\"valorant-reader\",\"architecture\":\"x64\","
              "\"backend\":\"hyper-mem\",\"backend_implemented\":"
           << (backend ? "true" : "false") << ',' << "\"backend_ready\":false,"
           << "\"profile_ready\":" << (profile.ready() ? "true" : "false") << ','
           << "\"capture_implemented\":true,\"subscription_implemented\":true,"
              "\"capture_ready\":false,\"subscription_ready\":false,\"qpc_frequency\":"
           << clock.frequency() << "}\n";
    return static_cast<int>(ExitCode::NotConfigured);
}
} // namespace valorant::app
