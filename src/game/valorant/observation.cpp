#include "game/valorant/observation.hpp"
#include "game/valorant/reading.hpp"
#include <array>
#include <limits>

namespace valorant::game {
using Status = ResolutionStatus;
using memory::Address;

ResolutionStatus Observation::open() {
    if (opened_ || closed_)
        return Status::NotConfigured;
    opened_ = true;
    const auto storage = roots_.object_storage();
    if (!storage.complete())
        return status_ = storage.status;
    const auto registry = read_object_registry(channel_, layout_.identity, storage.value);
    if (!registry.complete())
        return status_ = registry.status;
    registry_ = registry.value;
    objects_ = std::make_unique<ObjectReader>(channel_, layout_.identity, registry_);
    types_ = std::make_unique<TypeCatalog>(channel_, layout_.identity, *objects_, roots_);
    return status_ = types_->initialize();
}

Resolution<ObjectIdentity> Observation::require(Address address, const offsets::Root& native_type) {
    const auto value = match(address, native_type);
    if (!value.complete())
        return {value.status};
    if (!value.value.matches)
        return {Status::InvalidData};
    return {Status::Complete, value.value.identity};
}

Resolution<ObjectMatch> Observation::match(Address address, const offsets::Root& native_type) {
    if (closed_ || status_ != Status::Complete)
        return {closed_ ? Status::NotConfigured : status_};
    auto [anchor_it, added] = anchors_.try_emplace(&native_type);
    auto& anchor = anchor_it->second;
    if (added) {
        const auto value = roots_.pointer_slot(native_type);
        anchor.status = value.status;
        if (value.complete()) {
            anchor.value = value.value;
            const auto& record = types_->observe(anchor.value);
            anchor.status = record.status;
            if (record.status == Status::Complete && record.identity.type != types_->native_class())
                anchor.status = Status::InvalidData;
        }
    }
    if (anchor.status != Status::Complete)
        return {anchor.status};
    auto [it, inserted] = records_.try_emplace(address);
    auto& record = it->second;
    if (inserted) {
        const auto observed = objects_->observe(address);
        record.status = observed.status;
        if (observed.complete())
            record.identity = observed.value;
    }
    if (record.status != Status::Complete)
        return {record.status};
    const auto type = types_->derives_from(record.identity.type, anchor.value);
    if (!type.complete())
        return {type.status};
    if (types_->observe(record.identity.type).default_object == address)
        return {Status::InvalidData};
    return {Status::Complete, {record.identity, type.value}};
}

ResolutionStatus Observation::close() {
    if (closed_)
        return status_;
    closed_ = true;
    if (status_ != Status::Complete)
        return status_;
    for (auto& [address, record] : records_) {
        (void)address;
        if (record.status == Status::Complete)
            record.status = objects_->confirm(record.identity);
    }
    status_ = types_->close();
    for (auto& [slot, anchor] : anchors_) {
        if (anchor.status != Status::Complete)
            continue;
        const auto value = roots_.pointer_slot(*slot);
        anchor.status = !value.complete() ? value.status : value.value != anchor.value ? Status::SourceChanged
                                              : types_->confirm_chain(anchor.value, anchor.value);
    }
    const auto storage = roots_.object_storage();
    if (!storage.complete())
        status_ = storage.status;
    else if (storage.value != registry_.storage)
        status_ = Status::SourceChanged;
    else if (status_ == Status::Complete)
        status_ = confirm_object_registry(channel_, layout_.identity, registry_);
    return status_;
}

ResolutionStatus Observation::confirm(const ObjectIdentity& identity, const offsets::Root& native_type) const {
    return confirm(ObjectMatch{identity, true}, native_type);
}

ResolutionStatus Observation::confirm(const ObjectMatch& match, const offsets::Root& native_type) const {
    const auto& identity = match.identity;
    if (!closed_)
        return Status::NotConfigured;
    if (status_ != Status::Complete)
        return status_;
    const auto anchor = anchors_.find(&native_type);
    const auto object = records_.find(identity.address);
    if (anchor == anchors_.end() || object == records_.end())
        return Status::NotConfigured;
    if (anchor->second.status != Status::Complete)
        return anchor->second.status;
    if (object->second.status != Status::Complete)
        return object->second.status;
    if (object->second.identity != identity)
        return Status::SourceChanged;
    return types_->confirm_chain(identity.type, match.matches ? anchor->second.value : 0);
}

Resolution<Address> read_reference(memory::IReadChannel& channel, Address owner, const offsets::Member& member,
                                    bool allow_null) noexcept {
    if (!member.value)
        return {Status::NotConfigured};
    const auto address = detail::field_address(owner, member, sizeof(Address));
    if (!address)
        return {Status::InvalidData};
    const auto value = memory::read_value<Address>(channel, address);
    if (!value)
        return {Status::ReadUnavailable};
    if ((!*value && allow_null) || (memory::user_range(*value) && !(*value & 7)))
        return {Status::Complete, *value};
    return {Status::InvalidData};
}

Resolution<ArrayView> read_array_view(memory::IReadChannel& channel, Address address, const offsets::Array& layout,
                                     std::size_t stride) noexcept {
    if (!stride || !layout.data.value || !layout.count.value || !layout.capacity.value)
        return {Status::NotConfigured};
    ArrayView value;
    std::array requests{detail::field_read(address, layout.data, &value.data, sizeof(value.data)),
                        detail::field_read(address, layout.count, &value.count, sizeof(value.count)),
                        detail::field_read(address, layout.capacity, &value.capacity, sizeof(value.capacity))};
    if (!detail::complete_fields(channel, requests))
        return {Status::ReadUnavailable};
    if (value.count < 0 || value.capacity < value.count ||
        static_cast<std::uint32_t>(value.capacity) > (std::numeric_limits<std::size_t>::max)()/stride)
        return {Status::InvalidData};
    if (value.capacity && !memory::user_range(value.data, static_cast<std::size_t>(value.capacity)*stride))
        return {Status::InvalidData};
    return {Status::Complete, value};
}
} // namespace valorant::game
