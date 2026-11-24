#pragma once
#include "game/valorant/objects.hpp"
#include <array>
#include <unordered_map>

namespace valorant::game {
struct TypeObservation {
    ResolutionStatus status = ResolutionStatus::InvalidData; // 开启观察的结果，不代表已完成关闭核验。
    ObjectIdentity identity;              // 类型对象本身的已观察身份。
    memory::Address super = 0;            // 该 UClass 的 SuperStruct。
    memory::Address default_object = 0;   // 该 UClass 的默认对象，允许尚未创建而为零。
};

// 当前批次独占的类型来源目录，不含 Actor 类别、相机或骨骼业务规则。
class TypeCatalog {
  public:
    TypeCatalog(memory::IReadChannel& channel, const offsets::Identity& layout, const ObjectReader& objects,
                const RootResolver& roots) noexcept
        : channel_(channel), layout_(layout), objects_(objects), roots_(roots) {}
    [[nodiscard]] ResolutionStatus initialize();
    [[nodiscard]] const TypeObservation& observe(memory::Address);
    [[nodiscard]] Resolution<bool> derives_from(memory::Address type, memory::Address base);
    // 关闭后不再读取新类型；返回共享元类型来源状态，具体类型依赖由 confirm_chain 分别确认。
    [[nodiscard]] ResolutionStatus close();
    [[nodiscard]] ResolutionStatus confirm_chain(memory::Address, memory::Address stop = 0) const;
    [[nodiscard]] memory::Address native_class() const noexcept { return native_class_; }
    [[nodiscard]] std::size_t size() const noexcept { return records_.size(); }

  private:
    struct Record {
        TypeObservation observation;
        ResolutionStatus closing = ResolutionStatus::NotConfigured;
    };
    std::array<memory::ReadRequest, 2> fields(memory::Address, memory::Address&, memory::Address&) const noexcept;
    const TypeObservation& load(memory::Address, bool native_meta);
    ResolutionStatus load_meta(memory::Address);
    ResolutionStatus confirm_meta(memory::Address) const;
    memory::IReadChannel& channel_;
    const offsets::Identity& layout_;
    const ObjectReader& objects_;
    const RootResolver& roots_;
    memory::Address native_class_ = 0;
    bool closed_ = false;
    ResolutionStatus shared_status_ = ResolutionStatus::NotConfigured;
    std::unordered_map<memory::Address, Record> records_;
};
} // namespace valorant::game
