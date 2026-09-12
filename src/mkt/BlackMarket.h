#pragma once
// 黑市：溢价、配给、违禁品
#include "core/GameState.h"

namespace gf {

/// 更新黑市价格与溢价
void blackMarketUpdate(GameState& st);

/// 黑市溢价（相对白市）
[[nodiscard]] Fixed blackMarketPremiumOf(const GameState& st, u8 res);

/// 某标的在黑市是否被禁（白市不可交易但黑市可）
[[nodiscard]] bool blackMarketTradable(const GameState& st, u8 res);

/// 以黑市价成交（走 BZ 交易所的簿）
[[nodiscard]] Fixed blackMarketPriceOf(const GameState& st, u8 res);

/// 监管突袭：黑市持仓被查没的概率
[[nodiscard]] Fixed blackMarketRaidRisk(const GameState& st, u32 actor);

}  // namespace gf
