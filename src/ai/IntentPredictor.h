#pragma once
// IntentPredictor —— 从 OmniscientReader 的观测推出玩家未来 K 期净需求向量
//
// 例：玩家需 4000 alloys 造巨构 ⇒ AI 判断可获利，则在关键档位挂出提价卖单/囤现货。
// 玩家可用 counterintel（污染观测）或 decoy（假工程）诱导 AI 误判并高买，即可反杀。
#include "ai/OmniscientReader.h"
#include "domain/Observable.h"
#include "ai/ComputeBudget.h"
#include "util/Fixed.h"

namespace gf {

/// 生成 top-M 条策略路径并预测净需求
[[nodiscard]] IntentPrediction predictIntent(const GameState& st, u32 subject, const ComputeBudget& budget);

/// 把预测写进日志与 EmpireMind.playerPattern
void intentLog(GameState& st, u32 observer, const IntentPrediction& p);

/// 人类可读摘要
[[nodiscard]] std::string intentSummary(const IntentPrediction& p, int limit = 6);

}  // namespace gf
