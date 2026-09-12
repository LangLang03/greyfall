#pragma once
// 跨所套利：运费 / 封锁 / 关税造成的偏差与收敛
#include "core/GameState.h"
#include "mkt/MarketState.h"

namespace gf {

struct ArbOpportunity {
    u8 res = 0;
    u8 buyExch = 0;
    u8 sellExch = 0;
    Fixed grossGap = Fixed(0);   // (卖所买一 - 买所卖一)/中值
    Fixed netGap = Fixed(0);     // 扣除手续费 + 运费 + 关税 + 封锁惩罚
    i64 capacity = 0;            // 可套利容量
};

/// 计算某标的的最优跨所套利机会
[[nodiscard]] ArbOpportunity bestArbitrage(const GameState& st, u8 res);

/// 更新全部标的的套利残差与价格收敛
void arbitrageUpdate(GameState& st);

/// 全部机会（按净收益降序，最多 limit 条）
[[nodiscard]] std::vector<ArbOpportunity> arbitrageList(const GameState& st, int limit);

}  // namespace gf
