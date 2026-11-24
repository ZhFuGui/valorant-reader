#include "game/valorant/components.hpp"
#include "game/valorant/reading.hpp"
#include <bit>

namespace valorant::game {
using Status = ResolutionStatus;
using memory::Address;
Resolution<ComponentSet> ComponentReader::header(Address source) const {
    if (!layout_.element_stride.value || *layout_.element_stride.value < sizeof(Address) ||
        !layout_.inline_bit_capacity.value)
        return {Status::NotConfigured};
    const auto array = read_array_view(channel_, source, layout_.elements, *layout_.element_stride.value);
    if (!array.complete())
        return {array.status};
    ComponentSet value;
    value.source = source;
    value.slots = array.value;
    std::array reads{detail::field_read(source, layout_.heap_bits, &value.heap_bits, sizeof(Address)),
                     detail::field_read(source, layout_.bit_count, &value.bit_count, 4),
                     detail::field_read(source, layout_.bit_capacity, &value.bit_capacity, 4),
                     detail::field_read(source, layout_.free_count, &value.free_count, 4)};
    if (!detail::complete_fields(channel_, reads))
        return {Status::ReadUnavailable};
    // 稀疏槽位取自同一 UObject 集合，注册表的真实槽位空间也是格式边界，不是业务数量配额。
    if (value.bit_count != value.slots.count || value.bit_capacity < value.bit_count || value.free_count < 0 ||
        value.free_count > value.bit_count || static_cast<std::uint32_t>(value.bit_count) > object_slots_ ||
        (!value.heap_bits && static_cast<std::uint32_t>(value.bit_capacity) > *layout_.inline_bit_capacity.value))
        return {Status::InvalidData};
    return {Status::Complete, std::move(value)};
}
ResolutionStatus ComponentReader::payload(ComponentSet& value) const {
    const auto count = static_cast<std::uint32_t>(value.bit_count);
    value.bits.resize((std::size_t{count} + 31) / 32);
    const auto bytes = value.bits.size() * sizeof(std::uint32_t);
    const auto bits =
        value.heap_bits ? value.heap_bits : detail::field_address(value.source, layout_.inline_bits, bytes);
    if (bytes && (!memory::user_range(bits, bytes) || !channel_.read(bits, value.bits.data(), bytes).complete(bytes)))
        return Status::ReadUnavailable;
    // 最后一个字的容量尾位不属于有效范围，原生迭代也不消费这些位。
    if (count % 32)
        value.bits.back() &= (std::uint32_t{1} << (count % 32)) - 1;
    for (std::uint32_t word = 0; word < value.bits.size(); ++word) {
        auto allocated = value.bits[word];
        while (allocated) {
            value.indices.push_back(word * 32 + std::countr_zero(allocated));
            allocated &= allocated - 1;
        }
    }
    if (value.indices.size() != count - static_cast<std::uint32_t>(value.free_count))
        return Status::SourceChanged;
    value.objects.resize(value.indices.size());
    std::vector<memory::ReadRequest> reads;
    reads.reserve(value.objects.size());
    for (std::size_t i = 0; i < value.objects.size(); ++i)
        reads.push_back({value.slots.data + std::uint64_t{value.indices[i]} * *layout_.element_stride.value,
                         &value.objects[i],
                         sizeof(Address),
                         {}});
    if (!detail::complete_fields(channel_, reads))
        return Status::ReadUnavailable;
    for (auto object : value.objects)
        if (object && (!memory::user_range(object) || (object & 7)))
            return Status::UnsupportedCode; // 不把未核验的编码对象句柄当直接指针。
    return Status::Complete;
}
Resolution<ComponentSet> ComponentReader::read(Address source) const {
    auto value = header(source);
    if (value.complete())
        value.status = payload(value.value);
    return value;
}
ResolutionStatus ComponentReader::confirm(const ComponentSet& expected) const {
    auto current = read(expected.source);
    if (!current.complete())
        return current.status;
    const auto& value = current.value;
    return value.slots == expected.slots && value.heap_bits == expected.heap_bits &&
                   value.bit_count == expected.bit_count && value.bit_capacity == expected.bit_capacity &&
                   value.free_count == expected.free_count && value.bits == expected.bits &&
                   value.objects == expected.objects
               ? Status::Complete
               : Status::SourceChanged;
}
} // namespace valorant::game
