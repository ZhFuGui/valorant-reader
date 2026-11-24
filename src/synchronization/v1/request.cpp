#include "synchronization/v1/request.hpp"

namespace valorant::synchronization::v1 {
namespace {
struct Fields {
    wire::Reader reader;
    std::uint64_t seen = 0;
    explicit Fields(std::span<const std::byte> bytes) : reader(bytes) {}
    bool next(wire::Field& f) {
        if (!reader.next(f))
            return false;
        if (f.number >= 64 || (seen & (1ull << f.number)))
            throw wire::Error("duplicate or unknown field");
        seen |= 1ull << f.number;
        return true;
    }
};
std::uint64_t integer(const wire::Field& f) {
    f.require(0);
    return f.scalar;
}
unsigned small(const wire::Field& f) {
    const auto v = integer(f);
    if (v > 0xffffffffu)
        throw wire::Error("integer overflow");
    return static_cast<unsigned>(v);
}
double real(const wire::Field& f) {
    f.require(1);
    return std::bit_cast<double>(f.scalar);
}
} // namespace
Request request(std::span<const std::byte> bytes) {
    if (bytes.size() > 4096)
        throw wire::Error("control limit");
    Request result;
    Fields outer(bytes);
    wire::Field field;
    std::span<const std::byte> body;
    bool version = false;
    while (outer.next(field)) {
        if (field.number == 1) {
            if (small(field) != 1)
                throw wire::Error("protocol version");
            version = true;
        } else if (field.number == 2) {
            if (integer(field))
                throw wire::Error("client event sequence");
        } else if (field.number == 11 || field.number == 12 || field.number == 18 || field.number == 20 ||
                   field.number == 22) {
            if (result.kind)
                throw wire::Error("multiple request bodies");
            field.require(2);
            result.kind = field.number;
            body = field.bytes;
        } else
            throw wire::Error("client direction");
    }
    if (!version || !result.kind)
        throw wire::Error("missing request body");
    Fields inner(body);
    while (inner.next(field)) {
        if (field.number == 1) {
            result.id = integer(field);
            continue;
        }
        if (result.kind == 11) {
            switch (field.number) {
            case 2:
                result.version = small(field);
                break;
            case 3:
                field.require(2);
                result.schema.assign(field.bytes.begin(), field.bytes.end());
                break;
            case 4:
                result.stream = small(field);
                break;
            case 5:
                result.hz = real(field);
                break;
            case 6:
                result.projection = small(field);
                break;
            default:
                throw wire::Error("unknown subscribe field");
            }
        } else if (result.kind == 12) {
            if (field.number == 2)
                result.revision = integer(field);
            else if (field.number == 3)
                result.hz = real(field);
            else
                throw wire::Error("unknown rate field");
        } else if (result.kind == 18 || result.kind == 20) {
            if (field.number == 2)
                result.revision = integer(field);
            else if (field.number == 3 && result.kind == 20)
                result.clock = integer(field);
            else
                throw wire::Error("unknown clock field");
        } else
            throw wire::Error("unknown resync field");
    }
    if (!result.id)
        throw wire::Error("missing request identity");
    return result;
}
} // namespace valorant::synchronization::v1
