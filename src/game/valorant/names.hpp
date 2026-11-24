#pragma once
#include "game/valorant/roots.hpp"
#include <string>

namespace valorant::game {
struct NameValue {
    std::uint32_t comparison_index = 0; // 原生 FName 比较索引。
    std::uint32_t number = 0;           // 内部实例数字，零表示没有数字后缀。
    std::uint32_t display_index = 0;    // 原生 FName 显示索引。
    bool operator==(const NameValue&) const = default;
};
static_assert(sizeof(NameValue) == 12);

struct NameEntry {
    std::uint32_t comparison_index = 0; // 名称项中的比较索引；显示索引与它可能不同。
    std::string text;                   // 已验证的 UTF-8 文本，不补造未知名称。
};

class NameReader {
  public:
    // pool 与 layout 借用所属会话的已核验来源；不在此建立隐式跨帧缓存。
    NameReader(memory::IReadChannel& channel, const offsets::NameStorage& layout, NamePoolView pool) noexcept
        : channel_(channel), layout_(layout), pool_(pool) {}
    [[nodiscard]] Resolution<NameEntry> entry(std::uint32_t index) const;
    [[nodiscard]] Resolution<std::string> name(NameValue value) const;

  private:
    memory::IReadChannel& channel_;
    const offsets::NameStorage& layout_;
    NamePoolView pool_;
};
} // namespace valorant::game
