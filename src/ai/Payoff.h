#pragma once
// Payoff —— 效用函数与风险惩罚（EV = Σ δ^t·u - riskPenalty·VaR - allianceRetaliation）
#include "core/GameState.h"
#include "domain/Mind.h"
#include "util/Fixed.h"

namespace gf {

/// 某主体在给定权重下的效用
[[nodiscard]] Fixed payoffUtility(const GameState& st, u32 actor, const std::array<Fixed, kGoalDim>& w);

/// 效用分量（供 logs --phase ai 的 EV 分解）
struct PayoffBreakdown {
    std::array<Fixed, kGoalDim> components{};
    Fixed total = Fixed(0);
};
[[nodiscard]] PayoffBreakdown payoffBreakdown(const GameState& st, u32 actor,
                                              const std::array<Fixed, kGoalDim>& w);

/// 下行风险 VaR（95% 分位的历史损失近似）
[[nodiscard]] Fixed payoffVaR(const GameState& st, u32 actor, int window = 12);

/// 联盟报复的扩散成本：由扩散图（盟友/联邦/条约）估算
[[nodiscard]] Fixed payoffAllianceRetaliation(const GameState& st, u32 actor, u32 target);

}  // namespace gf
