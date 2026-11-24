#include "runtime/binding_health.hpp"
#include "memory/read_error.hpp"

namespace valorant::runtime {
BindingHealth observe_binding(memory::IBackend& backend, const memory::ISession& session, unsigned uncertain_checks) {
    using State = memory::ProcessState;
    auto state = State::Unavailable;
    try {
        state = session.state();
    } catch (const std::runtime_error&) {
    }
    if (state != State::Unavailable)
        return {state, 0};
    if (++uncertain_checks < 3)
        return {state, uncertain_checks};
    try {
        const auto expected = session.identity();
        const auto current = backend.find_process(expected.pid);
        if (!current)
            return {State::Exited, 0};
        if (*current != expected)
            return {State::Replaced, 0};
    } catch (const std::runtime_error&) {
        // 独立链也暂时不可读时保留原身份，不能据此断言进程退出或重复重绑。
    }
    return {State::Unavailable, 0};
}
} // namespace valorant::runtime
