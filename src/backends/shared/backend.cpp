#include "backends/shared/backend.hpp"
#include "memory/read_error.hpp"
#include <new>
#include <string>
#include <utility>

namespace valorant::backends {
namespace {
namespace shared = readers::memory;

[[noreturn]] void fail(shared::Error error) {
    if (error.code == shared::ErrorCode::ResourceExhausted)
        throw std::bad_alloc{};
    if (error.code == shared::ErrorCode::InvalidArgument)
        throw std::invalid_argument("Invalid memory request");
    throw memory::ReadUnavailable("Memory backend: " + std::to_string(static_cast<unsigned>(error.code)) + ":" +
                                  std::to_string(error.native_status));
}

memory::ProcessState process_state(shared::SessionState state) noexcept {
    using State = memory::ProcessState;
    switch (state) {
    case shared::SessionState::Current:
        return State::Current;
    case shared::SessionState::Unavailable:
        return State::Unavailable;
    case shared::SessionState::Exited:
        return State::Exited;
    case shared::SessionState::Replaced:
        return State::Replaced;
    case shared::SessionState::NeedsRebind:
        return State::AddressSpaceChanged;
    }
    return State::Unavailable;
}

class Channel final : public memory::IReadChannel {
  public:
    explicit Channel(std::unique_ptr<shared::ReadChannel> channel) : channel_(std::move(channel)) {
        if (!channel_)
            throw memory::ReadUnavailable("Memory channel is unavailable");
    }
    memory::ReadResult read(memory::Address address, void* output, std::size_t bytes) noexcept override {
        return channel_->read(address, output, bytes);
    }
    void read_many(std::span<memory::ReadRequest> requests) noexcept override { channel_->read_many(requests); }

  private:
    std::unique_ptr<shared::ReadChannel> channel_;
};

class Session final : public memory::ISession {
  public:
    explicit Session(std::unique_ptr<shared::Session> session) : session_(std::move(session)) {}
    memory::ProcessIdentity identity() const noexcept override { return session_->identity(); }
    memory::Address image_base() const noexcept override { return session_->image_base(); }
    memory::ProcessState state() const override { return process_state(session_->check()); }
    std::unique_ptr<memory::IReadChannel> open_reader() const override {
        return std::make_unique<Channel>(session_->open_reader());
    }

  private:
    std::unique_ptr<shared::Session> session_;
};

class Backend final : public memory::IBackend {
  public:
    Backend(std::unique_ptr<shared::Memory> source, std::string_view target_image)
        : source_(std::move(source)), target_image_(target_image) {
        if (!source_ || target_image_.empty())
            throw std::invalid_argument("A memory backend and target image are required");
    }
    std::optional<memory::ProcessIdentity> find_process(std::optional<std::uint32_t> pid) override {
        if (pid && !*pid)
            throw std::invalid_argument("Invalid target PID");
        pending_.reset();
        auto result = source_->try_open({target_image_, pid});
        if (!result) {
            if (result.error().code == shared::ErrorCode::NotFound)
                return {};
            fail(result.error());
        }
        pending_ = std::move(result).value();
        if (!pending_)
            throw memory::ReadUnavailable("Memory session is unavailable");
        return pending_->identity();
    }
    std::unique_ptr<memory::ISession> attach(memory::ProcessIdentity process) override {
        if (!process.pid || !process.creation_time)
            throw std::invalid_argument("A complete process identity is required");
        // 发现已得到完整会话，直接交接；只保留这一份待交接所有权，不重复发现或绑定。
        if (!pending_ || pending_->identity() != process) {
            pending_.reset();
            auto result = source_->try_open({target_image_, process.pid});
            if (!result)
                fail(result.error());
            pending_ = std::move(result).value();
        }
        if (!pending_ || pending_->identity() != process) {
            pending_.reset();
            throw memory::ReadUnavailable("Process identity changed while binding");
        }
        // 发现与交接之间允许目标退出或换页表；旧会话不能因身份字段仍相同而被直接安装。
        if (pending_->check() != shared::SessionState::Current) {
            pending_.reset();
            throw memory::ReadUnavailable("Memory binding changed before handoff");
        }
        return std::make_unique<Session>(std::move(pending_));
    }

  private:
    std::unique_ptr<shared::Memory> source_;
    const std::string target_image_;
    std::unique_ptr<shared::Session> pending_; // 只由会话管理任务访问，不保存游戏动态值。
};
} // namespace

std::unique_ptr<memory::IBackend> make_shared_backend(std::unique_ptr<shared::Memory> source,
                                                      std::string_view target_image) {
    return std::make_unique<Backend>(std::move(source), target_image);
}
} // namespace valorant::backends
