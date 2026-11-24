#pragma once
#include <cstdint>
#include <optional>

namespace valorant::model {
struct CaptureStamp {
    std::uint64_t sequence = 0;          // 当前采集流内递增的采样序号。
    std::uint64_t epoch = 0;             // 会话或世界上下文变化后的代次。
    std::uint64_t qpc_frequency = 0;     // 本地 QPC 频率，跨机转换须另行协商。
    std::uint64_t started_qpc = 0;       // 本次首次读取之前的采集起点。
    std::optional<std::uint64_t> read_finished_qpc; // 最后一次读取及必要读取核验结束。
    std::optional<std::uint64_t> finished_qpc;      // 本地整理与提交完成。
};

// 预留的持续采集组合入口；当前坐标、视角和姿态读取器各自返回独立批次。
struct Snapshot {
    CaptureStamp stamp;
};
} // namespace valorant::model
