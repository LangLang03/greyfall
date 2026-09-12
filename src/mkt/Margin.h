#pragma once
// 保证金与强平级联
#include "core/GameState.h"
#include "mkt/MarketState.h"

namespace gf {

struct TickReport;

/// 初始保证金：m0 + m1·σ·√T·lev
[[nodiscard]] Fixed marginRequired(Fixed notional, Fixed sigma, int leverage, int termTicks);

/// 逐日盯市：更新 equity / cash，返回是否触发追保
bool marginMarkToMarket(GameState& st);

/// 强平级联：吃穿簿深度 → 触发其他持仓者追保（批次循环，深度上限 8）
int marginCascade(GameState& st, TickReport& rep);

/// 玩家可用保证金余量（0..1 以上为安全）
[[nodiscard]] Fixed marginRatio(const GameState& st);

}  // namespace gf
