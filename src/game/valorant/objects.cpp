#include "game/valorant/objects.hpp"
#include "game/valorant/reading.hpp"
#include <array>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace valorant::game {
namespace {
using memory::Address;
using Status = ResolutionStatus;

bool configured(const offsets::Identity& layout) noexcept {
    const auto& object = layout.object;
    const auto& registry = layout.objects;
    return layout.name.bytes.value == sizeof(NameValue) && object.internal_index.value && object.type.value &&
           object.name.value && object.outer.value && registry.form == offsets::ObjectStorage::Chunked &&
           registry.objects_per_chunk.value && *registry.objects_per_chunk.value && registry.item_bytes.value &&
           registry.item_object.value && registry.item_flags.value && registry.item_serial.value &&
           registry.weak_reject_flags && *registry.weak_reject_flags &&
           std::uint64_t{*registry.item_object.value} + sizeof(Address) <= *registry.item_bytes.value &&
           std::uint64_t{*registry.item_flags.value} + sizeof(std::uint32_t) <= *registry.item_bytes.value &&
           std::uint64_t{*registry.item_serial.value} + sizeof(std::uint32_t) <= *registry.item_bytes.value;
}
} // namespace

Resolution<ObjectRegistryView> read_object_registry(memory::IReadChannel& channel, const offsets::Identity& identity,
                                                    Address storage) noexcept {
    const auto& layout = identity.objects;
    if (!configured(identity) || !layout.data.value || !layout.count.value || !layout.capacity.value ||
        !layout.chunk_count.value || !layout.chunk_capacity.value)
        return {Status::NotConfigured};
    if (!memory::user_range(storage))
        return {Status::InvalidData};
    Address data = 0;
    std::int32_t count = 0, capacity = 0, chunks = 0, chunk_capacity = 0;
    std::array requests{
        detail::field_read(storage, layout.data, &data, sizeof(data)),
        detail::field_read(storage, layout.count, &count, sizeof(count)),
        detail::field_read(storage, layout.capacity, &capacity, sizeof(capacity)),
        detail::field_read(storage, layout.chunk_count, &chunks, sizeof(chunks)),
        detail::field_read(storage, layout.chunk_capacity, &chunk_capacity, sizeof(chunk_capacity)),
    };
    if (!detail::complete_fields(channel, requests))
        return {Status::ReadUnavailable};
    const auto per_chunk = *layout.objects_per_chunk.value;
    if (count < 0 || capacity < count || chunks < 0 || chunk_capacity < chunks ||
        std::uint64_t{static_cast<std::uint32_t>(count)} > std::uint64_t{static_cast<std::uint32_t>(chunks)} * per_chunk ||
        std::uint64_t{static_cast<std::uint32_t>(capacity)} > std::uint64_t{static_cast<std::uint32_t>(chunk_capacity)} * per_chunk ||
        (chunks && (!memory::user_range(data, std::size_t{static_cast<std::uint32_t>(chunks)} * sizeof(Address)) || (data & 7))))
        return {Status::InvalidData};
    return {Status::Complete, {storage, data, static_cast<std::uint32_t>(count), static_cast<std::uint32_t>(chunks)}};
}

ResolutionStatus confirm_object_registry(memory::IReadChannel& channel, const offsets::Identity& layout,
                                         const ObjectRegistryView& first) noexcept {
    const auto closing = read_object_registry(channel, layout, first.storage);
    if (!closing.complete())
        return closing.status;
    // 同一表正常扩展不撤销已核验的槽位；表替换或范围回退需要重新建立这一批的来源。
    if (closing.value.chunks != first.chunks || closing.value.slots < first.slots ||
        closing.value.chunk_count < first.chunk_count)
        return Status::SourceChanged;
    return Status::Complete;
}

Resolution<ObjectIdentity> ObjectReader::observe(Address address) const noexcept {
    if (!configured(layout_))
        return {Status::NotConfigured};
    if (!memory::user_range(address) || (address & 7))
        return {Status::InvalidData};
    ObjectIdentity result;
    result.address = address;
    std::int32_t index = -1;
    std::array header{
        detail::field_read(address, layout_.object.internal_index, &index, sizeof(index)),
        detail::field_read(address, layout_.object.type, &result.type, sizeof(result.type)),
        detail::field_read(address, layout_.object.name, &result.name, sizeof(result.name)),
        detail::field_read(address, layout_.object.outer, &result.outer, sizeof(result.outer)),
    };
    if (!detail::complete_fields(channel_, header))
        return {Status::ReadUnavailable};
    if (index < 0 || static_cast<std::uint32_t>(index) >= registry_.slots ||
        !memory::user_range(result.type) || (result.type & 7) ||
        (result.outer && (!memory::user_range(result.outer) || (result.outer & 7))))
        return {Status::InvalidData};
    result.index = static_cast<std::uint32_t>(index);
    const auto& layout = layout_.objects;
    const auto chunk_index = result.index / *layout.objects_per_chunk.value;
    const auto within = result.index % *layout.objects_per_chunk.value;
    if (chunk_index >= registry_.chunk_count ||
        !memory::user_range(registry_.chunks, (std::size_t{chunk_index} + 1) * sizeof(Address)))
        return {Status::InvalidData};
    const auto chunk = memory::read_value<Address>(channel_, registry_.chunks + std::size_t{chunk_index} * sizeof(Address));
    if (!chunk)
        return {Status::ReadUnavailable};
    const auto offset = std::size_t{within} * *layout.item_bytes.value;
    if (!memory::user_range(*chunk, offset + *layout.item_bytes.value) || (*chunk & 7))
        return {Status::InvalidData};
    result.registry_chunk = *chunk;
    Address object = 0;
    std::uint32_t flags = 0;
    const auto item = *chunk + offset;
    std::array record{
        detail::field_read(item, layout.item_object, &object, sizeof(object)),
        detail::field_read(item, layout.item_serial, &result.weak_serial, sizeof(result.weak_serial)),
        detail::field_read(item, layout.item_flags, &flags, sizeof(flags)),
    };
    if (!detail::complete_fields(channel_, record))
        return {Status::ReadUnavailable};
    if (object != address)
        return {Status::SourceChanged};
    if (flags & *layout.weak_reject_flags)
        return {Status::InvalidData};
    return {Status::Complete, result};
}

ResolutionStatus ObjectReader::confirm(const ObjectIdentity& first) const noexcept {
    const auto closing = observe(first.address);
    if (!closing.complete())
        return closing.status == Status::InvalidData ? Status::SourceChanged : closing.status;
    return closing.value == first ? Status::Complete : Status::SourceChanged;
}

void ObjectReader::observe_many(std::span<const Address> addresses, std::span<Resolution<ObjectIdentity>> results) const {
    if (addresses.size() != results.size())
        throw std::invalid_argument("Object observation ranges differ");
    if (addresses.size() == 1) {
        results[0] = observe(addresses[0]);
        return;
    }
    for (auto& result : results) result = {Status::NotConfigured};
    if (addresses.empty() || !configured(layout_)) return;
    constexpr auto absent = (std::numeric_limits<std::size_t>::max)();
    struct Pending {
        ObjectIdentity identity;
        std::int32_t index = -1;
        std::size_t header = (std::numeric_limits<std::size_t>::max)();
        std::size_t chunk = (std::numeric_limits<std::size_t>::max)();
        std::size_t item = (std::numeric_limits<std::size_t>::max)();
        Address object = 0;
        std::uint32_t flags = 0;
    };
    struct Chunk {
        std::uint32_t index;
        Address address = 0;
        memory::ReadResult result;
    };
    std::vector<Pending> pending(addresses.size());
    std::vector<memory::ReadRequest> requests;
    if (addresses.size() > requests.max_size()/4)
        throw std::length_error("Object observation plan exceeds allocation range");
    requests.reserve(addresses.size()*4);
    for (std::size_t i = 0; i < addresses.size(); ++i) {
        auto& node = pending[i];
        node.identity.address = addresses[i];
        results[i].status = Status::InvalidData;
        if (!memory::user_range(addresses[i]) || (addresses[i] & 7)) continue;
        node.header = requests.size();
        requests.push_back(detail::field_read(addresses[i], layout_.object.internal_index, &node.index, sizeof(node.index)));
        requests.push_back(detail::field_read(addresses[i], layout_.object.type, &node.identity.type, sizeof(node.identity.type)));
        requests.push_back(detail::field_read(addresses[i], layout_.object.name, &node.identity.name, sizeof(node.identity.name)));
        requests.push_back(detail::field_read(addresses[i], layout_.object.outer, &node.identity.outer, sizeof(node.identity.outer)));
        results[i].status = Status::ReadUnavailable;
    }
    if (!requests.empty()) channel_.read_many(requests);
    const auto complete = [&](std::size_t first, std::size_t count) {
        for (std::size_t j = 0; j < count; ++j)
            if (!requests[first+j].result.complete(requests[first+j].bytes)) return false;
        return true;
    };
    const auto& registry = layout_.objects;
    std::vector<Chunk> chunks;
    std::unordered_map<std::uint32_t, std::size_t> chunk_slots;
    for (std::size_t i = 0; i < pending.size(); ++i) {
        auto& node = pending[i];
        if (node.header == absent || !complete(node.header, 4)) continue;
        const auto& identity = node.identity;
        if (node.index < 0 || static_cast<std::uint32_t>(node.index) >= registry_.slots ||
            !memory::user_range(identity.type) || (identity.type & 7) ||
            (identity.outer && (!memory::user_range(identity.outer) || (identity.outer & 7)))) {
            results[i].status = Status::InvalidData;
            continue;
        }
        node.identity.index = static_cast<std::uint32_t>(node.index);
        const auto chunk = node.identity.index / *registry.objects_per_chunk.value;
        if (chunk >= registry_.chunk_count || !memory::user_range(registry_.chunks, (std::size_t{chunk}+1)*sizeof(Address))) {
            results[i].status = Status::InvalidData;
            continue;
        }
        const auto [slot, added] = chunk_slots.try_emplace(chunk, chunks.size());
        if (added) chunks.push_back({chunk, 0, {}});
        node.chunk = slot->second;
    }
    // 先固定全部目标槽位，再建立持有其地址的请求，避免 vector 扩容使输出指针失效。
    requests.clear();
    for (auto& chunk : chunks)
        requests.push_back({registry_.chunks + std::size_t{chunk.index}*sizeof(Address), &chunk.address, sizeof(Address), {}});
    if (!requests.empty()) channel_.read_many(requests);
    for (std::size_t i = 0; i < chunks.size(); ++i) chunks[i].result = requests[i].result;
    requests.clear();
    for (std::size_t i = 0; i < pending.size(); ++i) {
        auto& node = pending[i];
        if (node.chunk == absent) continue;
        const auto& chunk = chunks[node.chunk];
        if (!chunk.result.complete(sizeof(Address))) continue;
        const auto offset = std::size_t{node.identity.index % *registry.objects_per_chunk.value} * *registry.item_bytes.value;
        if (!memory::user_range(chunk.address, offset + *registry.item_bytes.value) || (chunk.address & 7)) {
            results[i].status = Status::InvalidData;
            continue;
        }
        node.identity.registry_chunk = chunk.address;
        const auto item = chunk.address + offset;
        node.item = requests.size();
        requests.push_back(detail::field_read(item, registry.item_object, &node.object, sizeof(node.object)));
        requests.push_back(detail::field_read(item, registry.item_serial, &node.identity.weak_serial, sizeof(node.identity.weak_serial)));
        requests.push_back(detail::field_read(item, registry.item_flags, &node.flags, sizeof(node.flags)));
    }
    if (!requests.empty()) channel_.read_many(requests);
    for (std::size_t i = 0; i < pending.size(); ++i) {
        const auto& node = pending[i];
        if (node.item == absent || !complete(node.item, 3)) continue;
        if (node.object != node.identity.address)
            results[i].status = Status::SourceChanged;
        else if (node.flags & *registry.weak_reject_flags)
            results[i].status = Status::InvalidData;
        else
            results[i] = {Status::Complete, node.identity};
    }
}

void ObjectReader::confirm_many(std::span<const ObjectIdentity> inputs, std::span<ResolutionStatus> results) const {
    if (inputs.size() != results.size())
        throw std::invalid_argument("Object confirmation ranges differ");
    if (inputs.size() == 1) {
        results[0] = confirm(inputs[0]);
        return;
    }
    std::vector<Address> addresses;
    addresses.reserve(inputs.size());
    for (const auto& input : inputs) addresses.push_back(input.address);
    std::vector<Resolution<ObjectIdentity>> observations(inputs.size());
    observe_many(addresses, observations);
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        const auto& observed = observations[i];
        results[i] = !observed.complete() ? (observed.status == Status::InvalidData ? Status::SourceChanged : observed.status)
                                         : observed.value == inputs[i] ? Status::Complete : Status::SourceChanged;
    }
}

Resolution<std::optional<ObjectIdentity>> ObjectReader::resolve_weak(WeakObjectReference weak) const noexcept {
    if (!configured(layout_))
        return {Status::NotConfigured};
    if (!weak.serial || weak.index < 0)
        return {Status::Complete, std::nullopt};
    if (static_cast<std::uint32_t>(weak.index) >= registry_.slots) {
        // 批次开启后注册表可能扩展；不能拿旧范围将刚出现的有效 Leader 判成不存在。
        const auto current = read_object_registry(channel_, layout_, registry_.storage);
        if (!current.complete()) return {current.status};
        if (current.value.chunks != registry_.chunks || static_cast<std::uint32_t>(weak.index) < current.value.slots)
            return {Status::SourceChanged};
        return {Status::Complete, std::nullopt};
    }
    const auto& layout = layout_.objects;
    const auto index = static_cast<std::uint32_t>(weak.index);
    const auto chunk_index = index / *layout.objects_per_chunk.value;
    if (chunk_index >= registry_.chunk_count)
        return {Status::InvalidData};
    const auto chunk = memory::read_value<Address>(channel_, registry_.chunks + std::size_t{chunk_index}*sizeof(Address));
    if (!chunk)
        return {Status::ReadUnavailable};
    const auto offset = std::size_t{index % *layout.objects_per_chunk.value} * *layout.item_bytes.value;
    if (!memory::user_range(*chunk, offset + *layout.item_bytes.value) || (*chunk & 7))
        return {Status::InvalidData};
    const auto item = *chunk + offset;
    Address object = 0;
    std::uint32_t serial = 0, flags = 0;
    std::array requests{detail::field_read(item, layout.item_object, &object, sizeof(object)),
                        detail::field_read(item, layout.item_serial, &serial, sizeof(serial)),
                        detail::field_read(item, layout.item_flags, &flags, sizeof(flags))};
    if (!detail::complete_fields(channel_, requests))
        return {Status::ReadUnavailable};
    if (serial != weak.serial || !object || (flags & *layout.weak_reject_flags))
        return {Status::Complete, std::nullopt};
    const auto identity = observe(object);
    if (!identity.complete())
        return {identity.status};
    if (identity.value.index != index || identity.value.weak_serial != weak.serial || identity.value.registry_chunk != *chunk)
        return {Status::SourceChanged};
    return {Status::Complete, identity.value};
}
} // namespace valorant::game
