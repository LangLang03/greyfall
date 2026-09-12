#pragma once

#include <array>
#include <string>
#include <vector>

#include "util/Fixed.h"

namespace gf {

struct GameState;

struct VictoryRules {
    int maxUnrestPct = 10;
    int minStabilityPct = 85;
    int minLegitimacyPct = 85;
    int minAverageSatisfactionPct = 75;
    int minFactionSatisfactionPct = 60;
    int reserveQuarters = 2;
    u32 requiredQuarters = 12;
};

/// 季末采样的连续治理成绩；查询和同季操作不能增加计数。
struct VictoryProgress {
    u32 consecutiveQuarters = 0;
    u64 lastEvaluatedTick = 0;
    bool achieved = false;
};

struct VictoryStatus {
    bool won = false;
    bool currentCriteriaMet = false;
    int aliveRivals = 0;
    Fixed avgUnrest = Fixed(0);
    Fixed minStability = Fixed(0);
    Fixed minLegitimacy = Fixed(0);
    Fixed avgFactionSatisfaction = Fixed(0);
    Fixed minFactionSatisfaction = Fixed(0);
    Fixed cash = Fixed(0);
    Fixed netIncome = Fixed(0);
    std::array<Fixed, 3> reserves{};
    std::array<Fixed, 3> reserveNeeds{};
    int unsettledSystems = 0;
    bool domesticPeace = false;
    u32 sustainedQuarters = 0;
    VictoryRules rules;
    std::vector<std::string> unmet;
};

[[nodiscard]] VictoryRules victoryRules(int difficulty);
[[nodiscard]] VictoryStatus checkVictory(const GameState& st);
[[nodiscard]] std::string victoryReport(const GameState& st);
/// 在当季全部结算完成、tick 增加后调用；每个 tick 最多采样一次。
void victoryPhase(GameState& st);

}  // namespace gf
