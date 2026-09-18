#pragma once
// Tick 流水线：1 tick = 1 季度，14 个阶段
#include <string>
#include <vector>

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
    /// 本 tick 实际执行过的阶段名（按执行顺序）。
    ///
    /// 用途：**顺序守护**。`advanceOneTick` 的阶段顺序是载荷 ——
    /// `TickPipeline.cpp` 里留下过两次真实事故的记录（研究被夹在市场之后
    /// 导致 AI 永远分不到预算；决议在市场之后决策导致「缺钱」候选长期 20~31 项）。
    /// 但这些约束此前只写在注释里，没有任何机制阻止回归。
    /// 实测：把阶段顺序改错后，靠副作用构造的断言**不会失败** ——
    /// 因为最小测试世界里市场几乎没有活动。所以直接把顺序本身记录下来，
    /// 让测试可以对它做精确断言（见 tests/test_tickorder.cpp）。
    std::vector<std::string> phaseTrace;
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
