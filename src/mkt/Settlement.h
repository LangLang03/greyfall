#pragma once
// 交割、托管与保险
#include "core/GameState.h"
#include "mkt/MarketState.h"

namespace gf {

struct TickReport;

/// 开立托管/信用证
[[nodiscard]] bool escrowOpen(GameState& st, u32 payer, u32 payee, Fixed amount, u8 res, i64 qty, int termTicks,
                              std::string* err);

/// 释放托管
[[nodiscard]] bool escrowRelease(GameState& st, u32 escrowId, bool toPayee, std::string* err);

/// 保险：为持仓购买保险，降低尾部损失
[[nodiscard]] bool insurePosition(GameState& st, u8 res, i64 cover, std::string* err);

/// 每 tick 的保险成本结算
void insuranceSettle(GameState& st);

/// 交割结算阶段（保证金结算之后调用）
void settlementPhase(GameState& st, TickReport& rep);

}  // namespace gf
