#pragma once
#include "memory/read_channel.hpp"
#include <memory>
#include <optional>
#include <readers/memory/process.hpp>

namespace valorant::memory {
using ProcessIdentity = readers::memory::ProcessIdentity;

enum class ProcessState { Current, Unavailable, Exited, Replaced, AddressSpaceChanged };

class ISession {
  public:
    virtual ~ISession() = default;
    virtual ProcessIdentity identity() const noexcept = 0;
    virtual Address image_base() const noexcept = 0; // 绑定时核验的主映像起点，不向采集层暴露 CR3 或内核对象。
    virtual ProcessState state() const = 0;
    // 通道按 worker 独占；会话状态只能由运行层在安全边界替换。
    virtual std::unique_ptr<IReadChannel> open_reader() const = 0;
};

class IBackend {
  public:
    virtual ~IBackend() = default;
    // 缺失 PID 时按构造时配置的完整映像名发现；正常已绑定期间不调用发现接口。
    virtual std::optional<ProcessIdentity> find_process(std::optional<std::uint32_t> selected_pid) = 0;
    virtual std::unique_ptr<ISession> attach(ProcessIdentity) = 0;
};
} // namespace valorant::memory
