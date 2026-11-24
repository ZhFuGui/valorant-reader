#pragma once
#include <array>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace valorant::runtime {
class WorkPool {
  public:
    using Work=std::function<void(std::size_t)>;
    explicit WorkPool(std::function<void()> wake);
    ~WorkPool();
    WorkPool(const WorkPool&)=delete;
    WorkPool& operator=(const WorkPool&)=delete;
    bool submit(Work);
    std::size_t busy() const;
  private:
    void run(std::size_t);
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::array<Work,2> queue_;
    std::array<std::thread,2> workers_;
    std::size_t first_=0,count_=0,busy_=0;
    bool stopping_=false;
    std::function<void()> wake_;
};
} // namespace valorant::runtime
