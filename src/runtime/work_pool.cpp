#include "runtime/work_pool.hpp"
namespace valorant::runtime {
WorkPool::WorkPool(std::function<void()> wake):wake_(std::move(wake)) {
    try { for(std::size_t i=0;i<workers_.size();++i) workers_[i]=std::thread([this,i]{run(i);}); }
    catch(...) { {std::lock_guard guard(mutex_);stopping_=true;} ready_.notify_all();
        for(auto& thread:workers_) if(thread.joinable()) thread.join(); throw; }
}
WorkPool::~WorkPool() {
    {std::lock_guard guard(mutex_);stopping_=true;} ready_.notify_all();
    for(auto& thread:workers_) if(thread.joinable()) thread.join();
}
bool WorkPool::submit(Work work) {
    {std::lock_guard guard(mutex_);if(stopping_ || busy_==2) return false;
        queue_[(first_+count_)%2]=std::move(work);++count_;++busy_;}
    ready_.notify_one(); return true;
}
std::size_t WorkPool::busy() const {std::lock_guard guard(mutex_);return busy_;}
void WorkPool::run(std::size_t index) {
    for(;;) {
        Work work;
        {std::unique_lock lock(mutex_);ready_.wait(lock,[&]{return stopping_ || count_;});
            if(stopping_) return;
            work=std::move(queue_[first_]);first_=(first_+1)%2;--count_;}
        work(index); // 调用方提供 noexcept 收尾：异常转换为已预分配票据的失败终态。
        {std::lock_guard guard(mutex_);--busy_;}
        wake_();
    }
}
} // namespace valorant::runtime
