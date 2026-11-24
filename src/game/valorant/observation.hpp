#pragma once
#include "game/valorant/types.hpp"
#include <memory>

namespace valorant::game {
struct ObjectMatch {
    ObjectIdentity identity;
    bool matches = false; // 已成功识别对象和类型；false 不是读取失败。
};
// 一次属性批次独占的身份与类型证据；不保存动态属性，不跨批复用。
class Observation {
  public:
    Observation(memory::IReadChannel& channel, const Profile& profile, ModuleView image) noexcept
        : channel_(channel), layout_(profile.layout), roots_(channel, profile, image) {}
    Observation(const Observation&) = delete;
    Observation& operator=(const Observation&) = delete;
    [[nodiscard]] ResolutionStatus open();
    [[nodiscard]] Resolution<ObjectIdentity> require(memory::Address, const offsets::Root& native_type);
    [[nodiscard]] Resolution<ObjectMatch> match(memory::Address, const offsets::Root& native_type);
    [[nodiscard]] ResolutionStatus close();
    // close 后只查本批已完成的证据，不产生新读取。
    [[nodiscard]] ResolutionStatus confirm(const ObjectIdentity&, const offsets::Root& native_type) const;
    [[nodiscard]] ResolutionStatus confirm(const ObjectMatch&, const offsets::Root& native_type) const;
    [[nodiscard]] std::uint32_t object_slots() const noexcept { return registry_.slots; }
    [[nodiscard]] const ObjectReader& objects() const { return *objects_; }
    [[nodiscard]] const RootResolver& roots() const noexcept { return roots_; }

  private:
    struct Anchor { memory::Address value = 0; ResolutionStatus status = ResolutionStatus::NotConfigured; };
    struct Object { ObjectIdentity identity; ResolutionStatus status = ResolutionStatus::NotConfigured; };
    memory::IReadChannel& channel_;
    const offsets::Table& layout_;
    RootResolver roots_;
    ObjectRegistryView registry_;
    std::unique_ptr<ObjectReader> objects_;
    std::unique_ptr<TypeCatalog> types_;
    std::unordered_map<const offsets::Root*, Anchor> anchors_;
    std::unordered_map<memory::Address, Object> records_;
    ResolutionStatus status_ = ResolutionStatus::NotConfigured;
    bool opened_ = false, closed_ = false;
};

struct ArrayView {
    memory::Address data = 0;    // 当前原生数组数据区。
    std::int32_t count = 0, capacity = 0; // 计数与容量分别核验，不截断。
    bool operator==(const ArrayView&) const = default;
};
[[nodiscard]] Resolution<memory::Address> read_reference(memory::IReadChannel&, memory::Address,
                                                         const offsets::Member&, bool allow_null = false) noexcept;
[[nodiscard]] Resolution<ArrayView> read_array_view(memory::IReadChannel&, memory::Address,
                                                   const offsets::Array&, std::size_t stride) noexcept;
} // namespace valorant::game
