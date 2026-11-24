#include "app/server.hpp"
#include "app/application.hpp"
#include "app/composition.hpp"
#include "game/valorant/capture_service.hpp"
#include "platform/windows/qpc_clock.hpp"
#include "runtime/recovery.hpp"
#include "synchronization/v1/service.hpp"
#include "transport/tcp_server.hpp"
#include <algorithm>
#include <atomic>
#include <new>
#include <ostream>
#include <system_error>
#include <windows.h>

#include <bcrypt.h>

namespace valorant::app {
namespace {
struct Event {
    HANDLE value;
    explicit Event(const wchar_t* name = nullptr) : value(CreateEventW(nullptr, TRUE, FALSE, name)) {
        if (!value)
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "event");
    }
    ~Event() { CloseHandle(value); }
};
std::atomic<HANDLE> console_stop = nullptr;
BOOL WINAPI on_console(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT || signal == CTRL_CLOSE_EVENT) {
        if (auto event = console_stop.load(std::memory_order_acquire))
            SetEvent(event);
        return TRUE;
    }
    return FALSE;
}
struct Console {
    explicit Console(HANDLE event) {
        console_stop = event;
        SetConsoleCtrlHandler(on_console, TRUE);
    }
    ~Console() {
        console_stop.store(nullptr, std::memory_order_release);
        SetConsoleCtrlHandler(on_console, FALSE);
    }
};
void random(std::span<std::byte> bytes) {
    if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(bytes.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0 ||
        std::none_of(bytes.begin(), bytes.end(), [](std::byte b) { return b != std::byte{}; }))
        throw std::runtime_error("identity random");
}
} // namespace
int serve(const ServerOptions& options, std::ostream& output, std::ostream& errors) {
    Event stop(options.stop_event.empty() ? nullptr : options.stop_event.c_str()), wake;
    Console console(stop.value);
    const platform::windows::QpcClock clock;
    synchronization::v1::Identity identity;
    random(identity.instance);
    random(identity.clock_domain);
    std::unique_ptr<game::CaptureService> capture_owner;
    runtime::RecoveryBackoff initialization;
    while (!capture_owner && WaitForSingleObject(stop.value, 0) != WAIT_OBJECT_0) {
        try {
            capture_owner = std::make_unique<game::CaptureService>(create_backend(), options.pid, clock,
                                                                   [event = wake.value] { SetEvent(event); });
        } catch (const std::system_error& error) {
            if (error.code() != std::errc::resource_unavailable_try_again &&
                error.code() != std::errc::not_enough_memory)
                throw;
        } catch (const std::bad_alloc&) {
        }
        if (!capture_owner) {
            errors << "{\"service\":\"unavailable\",\"reason\":1008}\n" << std::flush;
            const auto now = capture::nanoseconds(clock);
            const auto until = initialization.retry_at(now);
            WaitForSingleObject(stop.value, static_cast<DWORD>((until - now + 999999) / 1000000));
        }
    }
    if (!capture_owner)
        return static_cast<int>(ExitCode::Success);
    auto& capture = *capture_owner;
    synchronization::v1::Service protocol(capture, clock, identity, options.maximum_connections);
    runtime::RecoveryBackoff recovery;
    while (WaitForSingleObject(stop.value, 0) != WAIT_OBJECT_0) {
        std::uint64_t listening_since = 0;
        try {
            transport::TcpServer server(options.port, options.maximum_connections, options.bind);
            server.run(protocol, clock, stop.value, wake.value, [&](readers::transport::ServiceStatus status) {
                if (status.state == readers::transport::ServiceState::Listening) {
                    listening_since = capture::nanoseconds(clock);
                    output << "{\"service\":\"listening\",\"bind\":\"" << options.bind
                           << "\",\"port\":" << status.bound_port
                           << ",\"pid\":" << (options.pid ? std::to_string(*options.pid) : "null")
                           << ",\"capture_workers\":2,\"protocol\":1}\n"
                           << std::flush;
                } else if (status.state == readers::transport::ServiceState::Recovering) {
                    errors << "{\"service\":\"unavailable\",\"reason\":1009,\"transport\":"
                           << static_cast<unsigned>(status.error.code) << ",\"native\":" << status.error.native_status
                           << "}\n"
                           << std::flush;
                }
            });
        } catch (const std::exception&) {
            if (listening_since) {
                recovery.progress(listening_since);
                recovery.progress(capture::nanoseconds(clock));
            }
            errors << "{\"service\":\"unavailable\",\"reason\":1009}\n" << std::flush;
            const auto until = recovery.retry_at(capture::nanoseconds(clock));
            // 共享层自行恢复端口/socket 故障；仅服务对象构造、启动或 Failed 才走这里重建。
            while (capture::nanoseconds(clock) < until && WaitForSingleObject(stop.value, 0) != WAIT_OBJECT_0) {
                ResetEvent(wake.value);
                capture.step();
                WaitForSingleObject(stop.value, 2);
            }
        }
    }
    return static_cast<int>(ExitCode::Success);
}
} // namespace valorant::app
