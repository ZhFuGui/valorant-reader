#pragma once
#include "game/valorant/observation.hpp"
#include "model/geometry.hpp"

namespace valorant::game {
struct SceneValue {
    ObjectIdentity component;      // 实际使用的场景组件身份。
    memory::Address selector = 0;  // 本批已识别的原生分支函数，不执行该函数。
    memory::Address selected = 0;  // 本批实际使用的变换记录来源。
    model::Transform transform;    // position_only 时仅 translation 有效。
};

// 当前批次独占；相同常量选择器只解析、关闭一次。
class SceneReader {
  public:
    SceneReader(memory::IReadChannel& channel, const Profile& profile, ModuleView image, Observation& observation)
        : channel_(channel), layout_(profile.layout), image_(image), observation_(observation) {}
    [[nodiscard]] Resolution<SceneValue> read(memory::Address component, bool position_only = false);
    [[nodiscard]] ResolutionStatus confirm(const SceneValue&) const;
    void close();
    [[nodiscard]] ResolutionStatus confirm_selector(const SceneValue&) const;

  private:
    struct Selection { memory::Address primary, alternate, selector; };
    struct Selector { Resolution<bool> initial; ResolutionStatus closing = ResolutionStatus::NotConfigured; };
    Resolution<Selection> selection(memory::Address) const;
    Resolution<bool> predicate(memory::Address) const;
    memory::IReadChannel& channel_;
    const offsets::Table& layout_;
    ModuleView image_;
    Observation& observation_;
    std::unordered_map<memory::Address, Selector> selectors_;
    bool closed_ = false;
};
} // namespace valorant::game
