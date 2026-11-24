#pragma once
#include "model/stream.hpp"
#include "synchronization/v1/wire.hpp"
#include "transport/record.hpp"

namespace valorant::synchronization::v1 {
wire::Writer update(const model::StreamState&, const model::StreamState* baseline);
wire::Writer diagnostic(const model::StreamState&, const wire::Writer& update);
transport::Record envelope(unsigned body, const wire::Writer&, std::uint64_t event,
                           std::optional<std::uint64_t> mapping);
} // namespace valorant::synchronization::v1
