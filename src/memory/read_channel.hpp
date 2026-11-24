#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <readers/memory/read_plan.hpp>
#include <span>
#include <type_traits>

namespace valorant::memory {
using Address = readers::memory::Address;
using ReadStatus = readers::memory::ReadStatus;
using ReadResult = readers::memory::ReadResult;
using ReadRequest = readers::memory::ReadRequest;

[[nodiscard]] constexpr bool valid_range(Address address, std::size_t bytes) noexcept {
    return !bytes || (address && bytes - 1 <= std::numeric_limits<Address>::max() - address);
}

using readers::memory::user_range;

class IReadChannel {
  public:
    virtual ~IReadChannel() = default;
    virtual ReadResult read(Address, void* destination, std::size_t bytes) noexcept = 0;
    // 默认实现供离线通道使用；原生合批由后端覆写，不向采集层泄漏后端 ABI。
    virtual void read_many(std::span<ReadRequest> requests) noexcept {
        for (auto& request : requests)
            request.result = read(request.address, request.destination, request.bytes);
    }
};

template <class T> [[nodiscard]] std::optional<T> read_value(IReadChannel& channel, Address address) noexcept {
    static_assert(std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>);
    T value{};
    if (!valid_range(address, sizeof(T)) || !channel.read(address, &value, sizeof(T)).complete(sizeof(T)))
        return {};
    return value;
}
} // namespace valorant::memory
