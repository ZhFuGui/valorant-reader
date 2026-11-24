#include "app/composition.hpp"
#include "backends/shared/backend.hpp"
#include "game/valorant/profile.hpp"

namespace valorant::app {
std::unique_ptr<memory::IBackend> create_backend() {
    return backends::make_shared_backend(readers::memory::create(),
                                         game::current_profile().layout.globals.engine.module);
}
} // namespace valorant::app
