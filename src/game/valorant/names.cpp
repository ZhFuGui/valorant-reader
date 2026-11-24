#include "game/valorant/names.hpp"
#include "game/valorant/reading.hpp"
#include <array>
#include <bit>
#include <cstring>
#include <utility>
#include <vector>

namespace valorant::game {
namespace {
using Status = ResolutionStatus;
using memory::Address;
struct PoolState { std::uint32_t block = 0, cursor = 0; };
struct Header { std::uint32_t comparison = 0; std::uint16_t value = 0; };

Resolution<PoolState> read_pool(memory::IReadChannel& channel, Address address, const offsets::NameStorage& layout) noexcept {
    PoolState state;
    std::array requests{
        detail::field_read(address, layout.current_block, &state.block, sizeof(state.block)),
        detail::field_read(address, layout.current_byte_cursor, &state.cursor, sizeof(state.cursor)),
    };
    if (!detail::complete_fields(channel, requests))
        return {Status::ReadUnavailable};
    if (state.block >= *layout.block_capacity.value || state.cursor > *layout.block_bytes.value)
        return {Status::InvalidData};
    return {Status::Complete, state};
}

Resolution<Header> read_header(memory::IReadChannel& channel, Address address, const offsets::NameStorage& layout) noexcept {
    Header header;
    std::array requests{
        detail::field_read(address, layout.entry_comparison_index, &header.comparison, sizeof(header.comparison)),
        detail::field_read(address, layout.entry_header, &header.value, sizeof(header.value)),
    };
    if (!detail::complete_fields(channel, requests))
        return {Status::ReadUnavailable};
    return {Status::Complete, header};
}

bool append_utf8(std::string& out, std::uint32_t scalar) {
    if (scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff))
        return false;
    if (scalar < 0x80) {
        out.push_back(static_cast<char>(scalar));
    } else if (scalar < 0x800) {
        out.push_back(static_cast<char>(0xc0 | (scalar >> 6)));
        out.push_back(static_cast<char>(0x80 | (scalar & 0x3f)));
    } else if (scalar < 0x10000) {
        out.push_back(static_cast<char>(0xe0 | (scalar >> 12)));
        out.push_back(static_cast<char>(0x80 | ((scalar >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (scalar & 0x3f)));
    } else {
        out.push_back(static_cast<char>(0xf0 | (scalar >> 18)));
        out.push_back(static_cast<char>(0x80 | ((scalar >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((scalar >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (scalar & 0x3f)));
    }
    return true;
}

std::uint16_t wide_unit(const std::vector<std::uint8_t>& data, std::size_t index) noexcept {
    std::uint16_t value;
    std::memcpy(&value, data.data() + index * 2, sizeof(value));
    return value;
}
} // namespace

Resolution<NameEntry> NameReader::entry(std::uint32_t index) const {
    if (!layout_.blocks.value || !layout_.current_block.value || !layout_.current_byte_cursor.value ||
        !layout_.entry_comparison_index.value || !layout_.entry_header.value || !layout_.entry_text.value ||
        layout_.entry_header_bytes.value != 2u || !layout_.entry_alignment.value || !*layout_.entry_alignment.value ||
        !layout_.block_bytes.value || !*layout_.block_bytes.value || !layout_.block_capacity.value || !*layout_.block_capacity.value ||
        !layout_.entry_max_units.value || !*layout_.entry_max_units.value || !layout_.block_shift || *layout_.block_shift >= 32 ||
        !layout_.offset_mask || !layout_.length_shift || *layout_.length_shift >= 16 || !layout_.wide_mask)
        return {Status::NotConfigured};
    if (layout_.text_encoding != offsets::NameTextEncoding::LengthXor32)
        return {Status::UnsupportedCode};
    if (!detail::user_range(pool_.allocator, 1))
        return {Status::InvalidData};
    const auto first = read_pool(channel_, pool_.allocator, layout_);
    if (!first.complete())
        return {first.status};
    const auto block = index >> *layout_.block_shift;
    const auto offset = std::uint64_t{index & *layout_.offset_mask} * *layout_.entry_alignment.value;
    if (block > first.value.block || offset >= *layout_.block_bytes.value)
        return {Status::InvalidData};
    const auto remaining = *layout_.block_bytes.value - offset;
    if (std::uint64_t{*layout_.entry_comparison_index.value} + sizeof(std::uint32_t) > remaining ||
        std::uint64_t{*layout_.entry_header.value} + sizeof(std::uint16_t) > remaining)
        return {Status::InvalidData};
    const auto blocks = detail::field_address(pool_.allocator, layout_.blocks, 1);
    if (!blocks || !memory::valid_range(blocks, std::size_t{block} * sizeof(Address) + sizeof(Address)))
        return {Status::InvalidData};
    const auto block_slot = blocks + std::uint64_t{block} * sizeof(Address);
    if (!detail::user_range(block_slot, sizeof(Address)))
        return {Status::InvalidData};
    const auto address = memory::read_value<Address>(channel_, block_slot);
    if (!address)
        return {Status::ReadUnavailable};
    if (!detail::user_range(*address, *layout_.block_bytes.value))
        return {Status::InvalidData};
    const auto source = *address + offset;
    const auto header = read_header(channel_, source, layout_);
    if (!header.complete())
        return {header.status};
    const auto units = static_cast<std::uint32_t>(header.value.value >> *layout_.length_shift);
    const bool wide = (header.value.value & *layout_.wide_mask) != 0;
    if (!units || units > *layout_.entry_max_units.value)
        return {Status::InvalidData};
    const auto bytes = std::size_t{units} * (wide ? 2u : 1u);
    const auto end = offset + *layout_.entry_text.value + bytes;
    if (end > *layout_.block_bytes.value || (block == first.value.block && end > first.value.cursor))
        return {Status::InvalidData};
    const auto text = detail::field_address(source, layout_.entry_text, bytes);
    if (!text)
        return {Status::InvalidData};
    std::vector<std::uint8_t> data(bytes);
    if (!channel_.read(text, data.data(), bytes).complete(bytes))
        return {Status::ReadUnavailable};
    // 原生宽字符分支复制 2*units 字节，但仅对前 units 个字节应用异或，不能扩大到整段。
    for (std::size_t i = 0; i < units; ++i)
        data[i] ^= static_cast<std::uint8_t>((pool_.key >> ((i % 4) * 8)) ^ units);
    const auto closing_block = memory::read_value<Address>(channel_, block_slot);
    const auto closing_header = read_header(channel_, source, layout_);
    const auto closing_pool = read_pool(channel_, pool_.allocator, layout_);
    if (!closing_block)
        return {Status::ReadUnavailable};
    if (!closing_header.complete())
        return {closing_header.status};
    if (!closing_pool.complete())
        return {closing_pool.status};
    if (*closing_block != *address || closing_header.value.comparison != header.value.comparison ||
        closing_header.value.value != header.value.value || closing_pool.value.block < first.value.block ||
        (closing_pool.value.block == first.value.block && closing_pool.value.cursor < first.value.cursor))
        return {Status::SourceChanged};
    NameEntry result;
    result.comparison_index = header.value.comparison;
    result.text.reserve(bytes);
    for (std::size_t i = 0; i < units; ++i) {
        std::uint32_t scalar = wide ? wide_unit(data, i) : data[i];
        if (!wide && scalar >= 0x80)
            return {Status::UnsupportedCode}; // 未证明该构建窄字节分支的扩展字符编码，不猜测转换。
        if (wide && scalar >= 0xd800 && scalar <= 0xdbff) {
            if (++i == units)
                return {Status::InvalidData};
            const auto low = wide_unit(data, i);
            if (low < 0xdc00 || low > 0xdfff)
                return {Status::InvalidData};
            scalar = 0x10000 + ((scalar - 0xd800) << 10) + (low - 0xdc00);
        }
        if (!append_utf8(result.text, scalar))
            return {Status::InvalidData};
    }
    return {Status::Complete, std::move(result)};
}

Resolution<std::string> NameReader::name(NameValue value) const {
    auto text = entry(value.display_index);
    if (!text.complete())
        return {text.status};
    if (text.value.comparison_index != value.comparison_index)
        return {Status::InvalidData};
    if (value.number) {
        // 原生 RVA 0x019a2474 先减一，再由 0x017dd930 按有符号 32 位数追加。
        text.value.text += '_';
        text.value.text += std::to_string(std::bit_cast<std::int32_t>(value.number - 1u));
    }
    return {Status::Complete, std::move(text.value.text)};
}
} // namespace valorant::game
