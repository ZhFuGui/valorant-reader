#include "game/valorant/roots.hpp"
#include "game/valorant/pointer_codec.hpp"
#include "game/valorant/reading.hpp"
#include <array>
#include <span>

namespace valorant::game {
namespace {
using memory::Address;
using Status = ResolutionStatus;

using detail::user_range;
using detail::field_address;
using detail::field_read;

Resolution<Address> root_address(const offsets::Root& root, const ModuleView& image, offsets::RootForm form,
                                 std::size_t bytes) noexcept {
    if (root.module != image.name || !root.rva || root.form != form)
        return {Status::NotConfigured};
    if (*root.rva >= image.identity.image_bytes || bytes > image.identity.image_bytes - *root.rva)
        return {Status::InvalidData};
    const auto address = image.base + *root.rva;
    return user_range(address, bytes) ? Resolution<Address>{Status::Complete, address}
                                      : Resolution<Address>{Status::InvalidData};
}

Status read_fields(memory::IReadChannel& channel, std::span<memory::ReadRequest> requests) noexcept {
    for (const auto& request : requests)
        if (!request.address)
            return Status::NotConfigured;
    return detail::complete_fields(channel, requests) ? Status::Complete : Status::ReadUnavailable;
}

Resolution<PointerState> read_state(memory::IReadChannel& channel, Address address, const offsets::PointerState& layout) noexcept {
    PointerState state;
    std::array requests{
        field_read(address, layout.values, state.values.data(), sizeof(state.values)),
        field_read(address, layout.seed, &state.seed, sizeof(state.seed)),
        field_read(address, layout.revision, &state.revision, sizeof(state.revision)),
        field_read(address, layout.updating, &state.updating, sizeof(state.updating)),
    };
    const auto status = read_fields(channel, requests);
    if (status != Status::Complete)
        return {status};
    if (state.updating)
        return {Status::SourceChanged};
    return {Status::Complete, state};
}

using Decode = std::uint64_t (*)(const PointerState&) noexcept;

Resolution<Address> stable_pointer(memory::IReadChannel& channel, Address address,
                                   const offsets::PointerState& layout, Decode decode) noexcept {
    const auto first = read_state(channel, address, layout);
    if (!first.complete())
        return {first.status};
    const auto second = read_state(channel, address, layout);
    if (!second.complete())
        return {second.status};
    // 调用计数在未改种子/条目时也会增长；仅核验实际参与解释的种子与选中来源。
    if (first.value.seed != second.value.seed)
        return {Status::SourceChanged};
    const auto before = decode(first.value);
    const auto after = decode(second.value);
    if (before != after)
        return {Status::SourceChanged};
    if (!user_range(after, 1))
        return {Status::InvalidData};
    return {Status::Complete, after};
}

std::optional<bool> constant_predicate(const std::array<std::uint8_t, 3>& code) noexcept {
    if (code == std::array<std::uint8_t, 3>{0x32, 0xc0, 0xc3} || code == std::array<std::uint8_t, 3>{0x33, 0xc0, 0xc3})
        return false;
    if (code == std::array<std::uint8_t, 3>{0xb0, 0x01, 0xc3})
        return true;
    return {};
}

Address decode_engine(Address value) noexcept {
    constexpr Address bytes = (Address{0xff} << 8) | (Address{0xff} << 48);
    const auto low = ((value >> 48) & 0xff) ^ 0x37;
    const auto high = ((value >> 8) & 0xff) ^ 0x37;
    return (value & ~bytes) | (low << 8) | (high << 48);
}

Status validate_directory(memory::IReadChannel& channel, Address address, const offsets::ObjectRegistry& layout) noexcept {
    if (layout.form != offsets::ObjectStorage::Chunked || !layout.objects_per_chunk.value || !*layout.objects_per_chunk.value)
        return Status::NotConfigured;
    Address data = 0;
    std::int32_t count = 0, capacity = 0, chunks = 0, chunk_capacity = 0;
    std::array requests{
        field_read(address, layout.data, &data, sizeof(data)),
        field_read(address, layout.count, &count, sizeof(count)),
        field_read(address, layout.capacity, &capacity, sizeof(capacity)),
        field_read(address, layout.chunk_count, &chunks, sizeof(chunks)),
        field_read(address, layout.chunk_capacity, &chunk_capacity, sizeof(chunk_capacity)),
    };
    const auto status = read_fields(channel, requests);
    if (status != Status::Complete)
        return status;
    if (count < 0 || capacity < count || chunks < 0 || chunk_capacity < chunks)
        return Status::InvalidData;
    const auto per_chunk = *layout.objects_per_chunk.value;
    if (std::uint64_t{static_cast<std::uint32_t>(capacity)} > std::uint64_t{static_cast<std::uint32_t>(chunk_capacity)} * per_chunk ||
        std::uint64_t{static_cast<std::uint32_t>(count)} > std::uint64_t{static_cast<std::uint32_t>(chunks)} * per_chunk ||
        (chunks && !user_range(data, std::size_t{static_cast<std::uint32_t>(chunks)} * sizeof(Address))))
        return Status::InvalidData;
    return Status::Complete;
}
} // namespace

ResolutionStatus RootResolver::validate_module() const noexcept {
    if (!profile_.image || image_.name.empty() || profile_.state == ProfileState::Unconfigured)
        return Status::NotConfigured;
    if (*profile_.image != image_.identity)
        return Status::BuildMismatch;
    if (!image_.identity.image_bytes || !user_range(image_.base, image_.identity.image_bytes))
        return Status::InvalidData;
    return Status::Complete;
}

Resolution<Address> RootResolver::engine() const noexcept {
    const auto status = validate_module();
    if (status != Status::Complete)
        return {status};
    const auto& globals = profile_.layout.globals;
    const auto selector = root_address(globals.engine_selector, image_, offsets::RootForm::PointerSlot, sizeof(Address));
    if (!selector.complete())
        return {selector.status};
    const auto function = memory::read_value<Address>(channel_, selector.value);
    if (!function)
        return {Status::ReadUnavailable};
    if (*function < image_.base || *function - image_.base >= image_.identity.image_bytes ||
        3 > image_.identity.image_bytes - (*function - image_.base))
        return {Status::UnsupportedCode};
    const auto code = memory::read_value<std::array<std::uint8_t, 3>>(channel_, *function);
    if (!code)
        return {Status::ReadUnavailable};
    const auto branch = constant_predicate(*code);
    if (!branch)
        return {Status::UnsupportedCode};
    const auto source = root_address(*branch ? globals.engine_encoded : globals.engine, image_,
                                     offsets::RootForm::PointerSlot, sizeof(Address));
    if (!source.complete())
        return {source.status};
    const auto raw = memory::read_value<Address>(channel_, source.value);
    const auto closing_function = memory::read_value<Address>(channel_, selector.value);
    const auto closing_code = memory::read_value<std::array<std::uint8_t, 3>>(channel_, *function);
    const auto closing_raw = memory::read_value<Address>(channel_, source.value);
    if (!raw || !closing_function || !closing_code || !closing_raw)
        return {Status::ReadUnavailable};
    if (*function != *closing_function || *code != *closing_code || *raw != *closing_raw)
        return {Status::SourceChanged};
    const auto value = *branch ? decode_engine(*raw) : *raw;
    return user_range(value, 1) ? Resolution<Address>{Status::Complete, value} : Resolution<Address>{Status::InvalidData};
}

Resolution<std::uint32_t> RootResolver::name_key() const noexcept {
    const auto status = validate_module();
    if (status != Status::Complete)
        return {status};
    const auto& identity = profile_.layout.identity;
    const auto source = root_address(identity.names.text_key, image_, offsets::RootForm::InlineObject, 1);
    if (!source.complete())
        return {source.status};
    const auto first = stable_pointer(channel_, source.value, identity.pointer_state, decode_name_key_pointer);
    if (!first.complete())
        return {first.status};
    const auto key = memory::read_value<std::uint32_t>(channel_, first.value);
    const auto second = stable_pointer(channel_, source.value, identity.pointer_state, decode_name_key_pointer);
    if (!second.complete())
        return {second.status};
    if (!key)
        return {Status::ReadUnavailable};
    if (first.value != second.value)
        return {Status::SourceChanged};
    const auto closing = memory::read_value<std::uint32_t>(channel_, second.value);
    if (!closing)
        return {Status::ReadUnavailable};
    if (*key != *closing)
        return {Status::SourceChanged};
    return {Status::Complete, *key};
}

Resolution<Address> RootResolver::object_storage() const noexcept {
    const auto status = validate_module();
    if (status != Status::Complete)
        return {status};
    const auto& identity = profile_.layout.identity;
    const auto root = root_address(profile_.layout.globals.objects, image_, offsets::RootForm::InlineObject, 1);
    if (!root.complete())
        return {root.status};
    const auto source = field_address(root.value, identity.objects.storage_state, 1);
    if (!source)
        return {Status::NotConfigured};
    const auto first = stable_pointer(channel_, source, identity.pointer_state, decode_object_storage_pointer);
    if (!first.complete())
        return {first.status};
    const auto directory = validate_directory(channel_, first.value, identity.objects);
    if (directory != Status::Complete)
        return {directory};
    const auto second = stable_pointer(channel_, source, identity.pointer_state, decode_object_storage_pointer);
    if (!second.complete())
        return {second.status};
    if (first.value != second.value)
        return {Status::SourceChanged};
    return first;
}

Resolution<NamePoolView> RootResolver::name_pool() const noexcept {
    const auto status = validate_module();
    if (status != Status::Complete)
        return {status};
    const auto root = root_address(profile_.layout.globals.names, image_, offsets::RootForm::InlineObject, 1);
    if (!root.complete())
        return {root.status};
    const auto allocator = field_address(root.value, profile_.layout.identity.names.allocator, 1);
    if (!allocator)
        return {Status::NotConfigured};
    const auto key = name_key();
    if (!key.complete())
        return {key.status};
    return {Status::Complete, {allocator, key.value}};
}

Resolution<Address> RootResolver::pointer_slot(const offsets::Root& root) const noexcept {
    const auto status = validate_module();
    if (status != Status::Complete)
        return {status};
    const auto source = root_address(root, image_, offsets::RootForm::PointerSlot, sizeof(Address));
    if (!source.complete())
        return {source.status};
    const auto first = memory::read_value<Address>(channel_, source.value);
    const auto closing = memory::read_value<Address>(channel_, source.value);
    if (!first || !closing)
        return {Status::ReadUnavailable};
    if (*first != *closing)
        return {Status::SourceChanged};
    if (!user_range(*first, 1) || (*first & 7))
        return {Status::InvalidData};
    return {Status::Complete, *first};
}
} // namespace valorant::game
