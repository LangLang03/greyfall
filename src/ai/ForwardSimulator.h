#pragma once
// ForwardSimulator —— 前瞻搜索：用真实撮合/战斗内核的精简版做 rollout
#include "ai/ComputeBudget.h"
#include "ai/IntentPredictor.h"
#include "core/GameState.h"
#include "util/Fixed.h"

namespace gf {

enum class AiActionKind : u8 {
    MarketBuy = 0,
    MarketSell,
    FuturesHedge,
    Hoard,
    DiploOffer,
    DiploDemand,
    DiploThreat,
    Betray,
    Embargo,
    DeclareWar,
    Colonize,
    Research,
    Build,
    Mega,
    Propaganda,
    Spy,
    PowerBalance,
    Invade,       // 出兵入侵：把舰队开向敌方星系
    Count,
};

struct AiAction {
    AiActionKind kind = AiActionKind::MarketBuy;
    u32 target = 0xFFFFFFFFu;
    u8 res = 0;
    i64 qty = 0;
    Fixed px{};
    Fixed terms = Fixed(0);
    std::string desc;
};

struct RolloutResult {
    Fixed ev = Fixed(0);
    Fixed ownUtility = Fixed(0);
    Fixed riskPenalty = Fixed(0);
    Fixed retaliation = Fixed(0);
    Fixed tradeGain = Fixed(0);
    Fixed betrayalGain = Fixed(0);
    int horizon = 0;
    bool feasible = true;
    std::string reason;
};

/// 对单个候选动作做 H 期 rollout
[[nodiscard]] RolloutResult simulateAction(const GameState& st, u32 actor, const AiAction& a,
                                           const IntentPrediction& playerIntent, int horizon,
                                           ComputeBudget& budget);

/// 生成候选动作（按静态效用取 top-K）
[[nodiscard]] std::vector<AiAction> generateCandidates(const GameState& st, u32 actor,
                                                       const IntentPrediction& playerIntent,
                                                       const ComputeBudget& budget);

/// 选中最优动作（argmax EV）
[[nodiscard]] AiAction chooseBestAction(const GameState& st, u32 actor, const IntentPrediction& playerIntent,
                                        ComputeBudget& budget, RolloutResult* outResult);

/// 人类可读的动作名
[[nodiscard]] std::string_view aiActionName(AiActionKind k);

}  // namespace gf
