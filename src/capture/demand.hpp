#pragma once
#include "model/stream.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>

namespace valorant::capture {
struct Rate {
    double requested = 0, category = 0;
    std::uint64_t period_ns = 0, revision = 1;
};
class Demand {
  public:
    Rate set(std::uint64_t id, model::Stream stream, double hz, bool diagnostic) {
        if (!id || static_cast<unsigned>(stream)<1 || static_cast<unsigned>(stream)>3 ||
            !std::isfinite(hz) || hz<1 || hz>300) throw std::invalid_argument("1005");
        values_.insert_or_assign(id, Entry{stream,hz,diagnostic});
        ++revision_;
        return {hz,maximum(stream),period(hz),revision_};
    }
    void erase(std::uint64_t id) { if (values_.erase(id)) ++revision_; }
    double maximum(model::Stream stream) const noexcept {
        double hz=0; for (const auto& [id,v]:values_) { (void)id; if(v.stream==stream) hz=(std::max)(hz,v.hz); } return hz;
    }
    double foundation() const noexcept {
        double hz=0; for(const auto& [id,v]:values_) { (void)id; hz=(std::max)(hz,v.hz); } return hz+1;
    }
    bool diagnostic(model::Stream stream) const noexcept {
        for(const auto& [id,v]:values_) { (void)id; if(v.stream==stream && v.diagnostic) return true; } return false;
    }
    std::uint64_t revision() const noexcept { return revision_; }
    static std::uint64_t period(double hz) { return static_cast<std::uint64_t>(std::llround(1000000000.0/hz)); }
  private:
    struct Entry { model::Stream stream; double hz; bool diagnostic; };
    std::unordered_map<std::uint64_t,Entry> values_;
    std::uint64_t revision_ = 1;
};

// 固定时间线，每次只留下一个待机会；繁忙时不会堆积历史任务或突发补采。
struct Timeline {
    std::uint64_t period = 0, next = 0, missed = 0;
    bool pending = false;
    void configure(std::uint64_t ns, std::uint64_t now) noexcept {
        if (period==ns) return;
        period=ns; next=now; if(!ns) pending=false;
    }
    void advance(std::uint64_t now) noexcept {
        if(!period || now<next) return;
        const auto opportunities=(now-next)/period+1;
        missed += opportunities-(pending?0:1);
        pending=true; next+=opportunities*period;
    }
};
} // namespace valorant::capture
