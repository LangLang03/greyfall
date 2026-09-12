#pragma once
// 背叛微积分：背叛前算清 EV（含信誉现值损失、战争成本、第三方联盟扩散）
#include "ai/ForwardSimulator.h"
#include "core/GameState.h"
#include "util/Fixed.h"

namespace gf {

struct BetrayalEV {
    Fixed gain = Fixed(0);          // PV(背叛收益, T=8)
    Fixed complianceValue = Fixed(0);  // PV(履约价值)
    Fixed reputationCost = Fixed(0);   // 全体观感损失 → 未来贸易与援助折损
    Fixed warCost = Fixed(0);          // E[warCost]
    Fixed exploitGain = Fixed(0);      // if playerModel.可欺
    Fixed total = Fixed(0);
    Fixed threshold = Fixed(0);
    bool shouldBetray = false;
    std::string decomposition;         // logs --phase ai 的可复盘分解
};

/// 计算背叛某目标的 EV
[[nodiscard]] BetrayalEV betrayalCalculus(const GameState& st, u32 actor, u32 target);

/// 把结果写入 EmpireMind 并打印可复盘日志
void betrayalLog(GameState& st, u32 actor, u32 target, const BetrayalEV& ev);

/// intel --threat 中显示"它为什么想背刺我"
[[nodiscard]] std::string betrayalReport(const GameState& st, u32 actor, u32 target);

}  // namespace gf
