#pragma once
#include "game/valorant/observation.hpp"

namespace valorant::game {
struct ComponentSet {
    memory::Address source = 0, heap_bits = 0;
    ArrayView slots;
    std::int32_t bit_count = 0, bit_capacity = 0, free_count = 0;
    std::vector<std::uint32_t> bits;
    std::vector<std::uint32_t> indices;
    std::vector<memory::Address> objects; // 原生稀疏槽位升序，不按地址重排。
};
// 只解释容器与成员引用，不承担团队、人物或名称规则。
class ComponentReader {
  public:
    ComponentReader(memory::IReadChannel& channel, const offsets::SparseSet& layout, std::uint32_t object_slots)
        : channel_(channel), layout_(layout), object_slots_(object_slots) {}
    [[nodiscard]] Resolution<ComponentSet> read(memory::Address source) const;
    [[nodiscard]] ResolutionStatus confirm(const ComponentSet&) const;

  private:
    [[nodiscard]] Resolution<ComponentSet> header(memory::Address) const;
    [[nodiscard]] ResolutionStatus payload(ComponentSet&) const;
    memory::IReadChannel& channel_;
    const offsets::SparseSet& layout_;
    std::uint32_t object_slots_;
};
} // namespace valorant::game
