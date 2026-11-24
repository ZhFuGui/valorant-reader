#pragma once
#include "game/valorant/names.hpp"

namespace valorant::game {
struct ObjectRegistryView {
    memory::Address storage = 0;       // 本批已解析的对象存储描述，不跨批复用。
    memory::Address chunks = 0;        // 本批观察到的分块表，关闭时必须重新核验。
    std::uint32_t slots = 0;           // 原生槽位范围，包含空闲槽位。
    std::uint32_t chunk_count = 0;     // 此次可索引的分块数，不等于对象数量。
};

struct ObjectIdentity {
    memory::Address address = 0;       // 本次对象地址，不作为消费者的永久实体 ID。
    std::uint32_t index = 0;           // 已与对象注册槽位交叉核验的索引。
    std::uint32_t weak_serial = 0;     // 原生弱序号；零表示未分配，不能单独证明没有地址复用。
    memory::Address type = 0;          // 本次 ClassPrivate 引用，类型解释还需核验 UClass 来源。
    NameValue name;                    // 原始名称值参与短期身份核验，不在这里解码文本。
    memory::Address outer = 0;         // 本次所属对象；最外层对象允许为零。
    memory::Address registry_chunk = 0; // 实际使用的分块来源，确认时不可换成另一块。
    bool operator==(const ObjectIdentity&) const = default;
};

struct WeakObjectReference {
    std::int32_t index = -1;            // 对象注册表槽位，负数表示没有目标。
    std::uint32_t serial = 0;          // 零表示没有可用弱引用。
    bool operator==(const WeakObjectReference&) const = default;
};
static_assert(sizeof(WeakObjectReference) == 8);

[[nodiscard]] Resolution<ObjectRegistryView> read_object_registry(memory::IReadChannel&, const offsets::Identity&,
                                                                 memory::Address storage) noexcept;
[[nodiscard]] ResolutionStatus confirm_object_registry(memory::IReadChannel&, const offsets::Identity&,
                                                       const ObjectRegistryView&) noexcept;

class ObjectReader {
  public:
    ObjectReader(memory::IReadChannel& channel, const offsets::Identity& layout, ObjectRegistryView registry) noexcept
        : channel_(channel), layout_(layout), registry_(registry) {}
    // observe 只建立开启观察；实际使用其字段后必须 confirm，不能据此声明原子快照或永久生命周期。
    [[nodiscard]] Resolution<ObjectIdentity> observe(memory::Address) const noexcept;
    [[nodiscard]] ResolutionStatus confirm(const ObjectIdentity&) const noexcept;
    // 跨对象分阶段合批；每个输入保留独立终态，分块来源只在本次调用内共享。
    // 输入输出长度必须相同；分配失败抛出异常，不截断对象名单。
    void observe_many(std::span<const memory::Address>, std::span<Resolution<ObjectIdentity>>) const;
    void confirm_many(std::span<const ObjectIdentity>, std::span<ResolutionStatus>) const;
    // Complete + nullopt 表示原生弱引用无效；读取失败绝不能解释成“没有 Leader”。
    [[nodiscard]] Resolution<std::optional<ObjectIdentity>> resolve_weak(WeakObjectReference) const noexcept;

  private:
    memory::IReadChannel& channel_;
    const offsets::Identity& layout_;
    ObjectRegistryView registry_;
};
} // namespace valorant::game
