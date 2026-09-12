#pragma once
// 内幕交易：事件公布前的知情建仓，以及玩家反向利用
#include "core/GameState.h"
#include "mkt/MarketState.h"

namespace gf {

/// 生成内幕信号：AI 在自己相关的事件公布前建仓
void insiderEmitSignals(GameState& st);

/// 判断某主体在给定资源上是否有知情痕迹（用于 AI --threat / 玩家侦察）
[[nodiscard]] Fixed insiderTrace(const GameState& st, u32 actor, u8 res);

/// 玩家 spy --mission insider 的结果文本
[[nodiscard]] std::string insiderReport(const GameState& st, u32 target);

/// 消费信号（事件公布时调用）
void insiderConsume(GameState& st);

}  // namespace gf
