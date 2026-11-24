#include "game/valorant/types.hpp"
#include "game/valorant/reading.hpp"
#include <algorithm>
#include <array>
#include <vector>

namespace valorant::game {
namespace {
using memory::Address;
using Status = ResolutionStatus;
}

ResolutionStatus TypeCatalog::initialize() {
    if (native_class_ || closed_ || !layout_.type.super.value || !layout_.type.default_object.value)
        return Status::NotConfigured;
    const auto root = roots_.pointer_slot(layout_.type.native_class);
    if (!root.complete())
        return root.status;
    native_class_ = root.value;
    return load(native_class_, true).status;
}

std::array<memory::ReadRequest, 2> TypeCatalog::fields(Address address, Address& super, Address& default_object) const noexcept {
    return {detail::field_read(address, layout_.type.super, &super, sizeof(super)),
            detail::field_read(address, layout_.type.default_object, &default_object, sizeof(default_object))};
}

const TypeObservation& TypeCatalog::observe(Address address) { return load(address, false); }

const TypeObservation& TypeCatalog::load(Address address, bool native_meta) {
    const auto found = records_.find(address);
    if (found != records_.end())
        return found->second.observation;
    if (!native_class_ || closed_) {
        static const TypeObservation missing{Status::NotConfigured, {}, 0, 0};
        return missing;
    }
    auto& record = records_.try_emplace(address).first->second.observation;
    const auto identity = objects_.observe(address);
    if (!identity.complete()) {
        record.status = identity.status;
        return record;
    }
    record.identity = identity.value;
    if (native_meta && record.identity.type != native_class_) {
        record.status = Status::UnsupportedCode;
        return record;
    }
    if (!native_meta) {
        const auto meta = load_meta(record.identity.type);
        if (meta != Status::Complete) {
            record.status = meta;
            return record;
        }
    }
    auto requests = fields(address, record.super, record.default_object);
    if (!detail::complete_fields(channel_, requests)) {
        record.status = Status::ReadUnavailable;
        return record;
    }
    if ((record.super && (!memory::user_range(record.super) || (record.super & 7))) ||
        (record.default_object && (!memory::user_range(record.default_object) || (record.default_object & 7))))
        return record;
    record.status = Status::Complete;
    return record;
}

ResolutionStatus TypeCatalog::load_meta(Address type) {
    std::vector<Address> seen;
    while (type != native_class_) {
        if (!type || std::find(seen.begin(), seen.end(), type) != seen.end())
            return Status::InvalidData;
        seen.push_back(type);
        const auto& record = load(type, true);
        if (record.status != Status::Complete)
            return record.status;
        if (record.identity.type != native_class_)
            return Status::UnsupportedCode;
        type = record.super;
    }
    return Status::Complete;
}

Resolution<bool> TypeCatalog::derives_from(Address type, Address base) {
    if (!base)
        return {Status::InvalidData};
    std::vector<Address> seen;
    while (type) {
        if (std::find(seen.begin(), seen.end(), type) != seen.end())
            return {Status::InvalidData};
        seen.push_back(type);
        const auto& record = observe(type);
        if (record.status != Status::Complete)
            return {record.status};
        if (type == base)
            return {Status::Complete, true};
        type = record.super;
    }
    return {Status::Complete, false};
}

ResolutionStatus TypeCatalog::close() {
    if (closed_)
        return shared_status_;
    closed_ = true;
    for (auto& [address, entry] : records_) {
        const auto& record = entry.observation;
        if (record.status != Status::Complete) {
            entry.closing = record.status;
            continue;
        }
        Address super = 0, default_object = 0;
        auto requests = fields(address, super, default_object);
        if (!detail::complete_fields(channel_, requests))
            entry.closing = Status::ReadUnavailable;
        else if (super != record.super || default_object != record.default_object)
            entry.closing = Status::SourceChanged;
        else
            entry.closing = objects_.confirm(record.identity);
    }
    const auto root = roots_.pointer_slot(layout_.type.native_class);
    if (!root.complete())
        return shared_status_ = root.status;
    if (root.value != native_class_)
        return shared_status_ = Status::SourceChanged;
    const auto meta = records_.find(native_class_);
    return shared_status_ = meta == records_.end() ? Status::NotConfigured : meta->second.closing;
}

ResolutionStatus TypeCatalog::confirm_meta(Address type) const {
    std::vector<Address> seen;
    while (type != native_class_) {
        if (!type || std::find(seen.begin(), seen.end(), type) != seen.end())
            return Status::InvalidData;
        seen.push_back(type);
        const auto found = records_.find(type);
        if (found == records_.end())
            return Status::InvalidData;
        if (found->second.closing != Status::Complete)
            return found->second.closing;
        const auto& record = found->second.observation;
        if (record.identity.type != native_class_)
            return Status::UnsupportedCode;
        type = record.super;
    }
    return Status::Complete;
}

ResolutionStatus TypeCatalog::confirm_chain(Address type, Address stop) const {
    if (!closed_)
        return Status::NotConfigured;
    if (shared_status_ != Status::Complete)
        return shared_status_;
    if (!type)
        return Status::InvalidData;
    std::vector<Address> seen;
    while (type) {
        if (std::find(seen.begin(), seen.end(), type) != seen.end())
            return Status::InvalidData;
        seen.push_back(type);
        const auto found = records_.find(type);
        if (found == records_.end())
            return Status::InvalidData;
        const auto& entry = found->second;
        if (entry.closing != Status::Complete)
            return entry.closing;
        const auto meta = confirm_meta(entry.observation.identity.type);
        if (meta != Status::Complete)
            return meta;
        if (type == stop)
            return Status::Complete;
        type = entry.observation.super;
    }
    // 指定的基类必须确实出现在已关闭的祖先链中，走到链尾不等于达到该基类。
    return stop ? Status::InvalidData : Status::Complete;
}
} // namespace valorant::game
