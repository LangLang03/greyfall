#pragma once
// FraudDetect —— 一致性异常检测：判断"他为何要让我看到这个"（costly signaling）
#include "ai/OmniscientReader.h"
#include "domain/Provenance.h"
#include "util/Fixed.h"

namespace gf {

struct FraudFinding {
    std::string field;
    Fixed score = Fixed(0);
    std::string reason;
    ProvChannel channel = ProvChannel::Forgery;
    Fixed estimatedSignalCost = Fixed(0);
};

/// 对某个主体的观测做异常打分
[[nodiscard]] std::vector<FraudFinding> fraudDetect(const GameState& st, u32 observer, u32 subject,
                                                    const Observable& obs, const Observable& prev);

/// 综合可疑度（0..1）：进入 ToModel 的 contamination
[[nodiscard]] Fixed fraudOverallScore(const std::vector<FraudFinding>& findings);

/// 把发现写进日志
void fraudLog(GameState& st, u32 observer, const std::vector<FraudFinding>& findings);

/// 成本信号解读：他为此付出了多少？
[[nodiscard]] Fixed signalCostEstimate(const GameState& st, u32 subject, ProvChannel ch);

}  // namespace gf
