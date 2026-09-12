#pragma once
// 汇率与战时配给
#include "core/GameState.h"

namespace gf {

/// 更新各所结算币价（以 credits 为锚）
void fxUpdate(GameState& st);

/// 直接汇率：1 单位 from 币 = ? 单位 to 币
[[nodiscard]] Fixed fxRate(const GameState& st, int fromExch, int toExch);

/// 配给强度对需求的抑制（0..1）
[[nodiscard]] Fixed rationingDampen(const GameState& st);

}  // namespace gf
