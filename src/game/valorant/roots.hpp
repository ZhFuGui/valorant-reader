#pragma once
#include "game/valorant/profile.hpp"
#include "memory/read_channel.hpp"
#include <string_view>

namespace valorant::game {
enum class ResolutionStatus { Complete, NotConfigured, BuildMismatch, ReadUnavailable, InvalidData, SourceChanged, UnsupportedCode };

template<class T> struct Resolution {
    ResolutionStatus status = ResolutionStatus::NotConfigured; // 失败终态由调用方重试，不在解析器内重绑进程。
    T value{};                                               // 仅 Complete 时有效；密钥的零值仍是有效值。
    [[nodiscard]] bool complete() const noexcept { return status == ResolutionStatus::Complete; }
};

struct ModuleView {
    memory::Address base = 0;             // 已绑定进程内的当前模块基址。
    std::string_view name;                // 借用会话模块目录的名称，调用期间须保持有效。
    BuildIdentity identity;               // 会话从原始运行映像核对的身份，不能来自重建 dump 的清零头。
};

struct NamePoolView {
    memory::Address allocator = 0; // 当前会话已解析的名称分配器，不能跨进程实例使用。
    std::uint32_t key = 0;         // 已核验来源的文本密钥，零值有效。
};

// 每个读取 worker 独占通道及解析器。所有操作只读、一次尝试，无循环等待和跨帧动态值缓存。
class RootResolver {
  public:
    RootResolver(memory::IReadChannel& channel, const Profile& profile, ModuleView image) noexcept
        : channel_(channel), profile_(profile), image_(image) {}

    [[nodiscard]] Resolution<memory::Address> engine() const noexcept;
    [[nodiscard]] Resolution<std::uint32_t> name_key() const noexcept;
    [[nodiscard]] Resolution<memory::Address> object_storage() const noexcept;
    [[nodiscard]] Resolution<NamePoolView> name_pool() const noexcept;
    [[nodiscard]] Resolution<memory::Address> pointer_slot(const offsets::Root&) const noexcept;

  private:
    [[nodiscard]] ResolutionStatus validate_module() const noexcept;
    memory::IReadChannel& channel_;
    const Profile& profile_;
    ModuleView image_;
};
} // namespace valorant::game
