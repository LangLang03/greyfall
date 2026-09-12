#pragma once
// 信用、债务与违约
#include "core/GameState.h"

namespace gf {

struct TickReport;

/// 信用评级随现金比率、声誉、regimeRisk 演化
void creditUpdate(GameState& st);

/// 借贷利差：由信用评级决定
[[nodiscard]] Fixed borrowingSpread(const GameState& st, u32 borrower);

/// 借款
[[nodiscard]] bool borrowCredits(GameState& st, u32 borrower, Fixed amount, int termTicks, std::string* err);

/// 还款
[[nodiscard]] bool repayCredits(GameState& st, u32 borrower, Fixed amount, std::string* err);

/// 违约概率
[[nodiscard]] Fixed defaultProbability(const GameState& st, u32 borrower);

/// 债务结算：利息、到期、违约（含托管释放）
void debtSettle(GameState& st, TickReport& rep);

/// 玩家债务总额
[[nodiscard]] Fixed totalDebt(const GameState& st, u32 borrower);

}  // namespace gf
