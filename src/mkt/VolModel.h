#pragma once
// GARCH(1,1) 波动率聚集 + 跳跃项（定点实现）
#include "mkt/MarketState.h"
#include "util/Fixed.h"

namespace gf {

/// σ²_t = ω + α·r²_{t-1} + β·σ²_{t-1}（α+β = 0.94），并叠加跳跃项
void volUpdate(VolState& v, Fixed ret, Fixed dt = Fixed(1));

/// 事件驱动的跳跃：直接抬高方差并记录偏差
void volInjectJump(VolState& v, Fixed magnitude);

/// 当前 σ
[[nodiscard]] Fixed volSigma(const VolState& v);

/// 方差（σ²）的定点表示
[[nodiscard]] Fixed volVariance(const VolState& v);

/// 把收益率序列的 EWMA 更新到 Book.sigma（每 tick 调用）
void volApplyToBook(VolState& v, Book& b);

}  // namespace gf
