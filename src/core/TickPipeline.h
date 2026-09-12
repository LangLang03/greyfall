#pragma once
// Tick 流水线：1 tick = 1 季度，14 个阶段
#include "core/GameState.h"

namespace gf {

struct TickReport {
    u64 tick = 0;
    i64 fills = 0;
    Fixed notional = Fixed(0);
    int aiActions = 0;
    int eventsFired = 0;
    int cluesDiscovered = 0;
    bool pendingRaised = false;
};

/// 推进一个 tick（完整 14 阶段）
TickReport advanceOneTick(GameState& st);

/// 推进 n 个 tick；遇到待抉择事件时提前停止并返回 5（stopOnPending=false 则强制结算）
int advanceTicks(GameState& st, int n, bool stopOnPending = true);

/// 无输出自动对局（selftest / bots 用）；不停止于 pending，直接自动选择
int runHeadlessTicks(GameState& st, int n);

// ---- 各阶段（供命令与测试单独调用） ----
void phaseActionPoints(GameState& st);
void phaseResolvePending(GameState& st, TickReport& rep);
void phaseMarket(GameState& st, TickReport& rep);
void phaseVolMargin(GameState& st, TickReport& rep);
void phaseReadPlayer(GameState& st);
void phaseUpdateModel(GameState& st);
void phaseAiActions(GameState& st, TickReport& rep);
void phaseFederation(GameState& st);
void phaseDomestic(GameState& st);
void phaseCombat(GameState& st, TickReport& rep);
void phaseEvents(GameState& st, TickReport& rep);
void phaseClues(GameState& st, TickReport& rep);
void phasePlot(GameState& st);
void phaseEconomy(GameState& st);
void phaseCommit(GameState& st);
/// 把所有 0..1 量纲夹取到有效区间（tick 结束与抉择结算后各调用一次）
void clampInvariants(GameState& st);

}  // namespace gf
