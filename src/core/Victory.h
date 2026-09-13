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
    /// 连续破产季数（失败判定用；季末结算后累加，恢复则归零）
    u32 consecutiveBankrupt = 0;
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

/// 失败判定：玩家是否已无可挽回。
///
/// 旧实现完全没有失败态 —— `st.ended` 只有「征服胜利」与「剧情结局」两个
/// 赋值点，失去全部领土后游戏会**无声地继续运行**（实测玩家 0 星系 / 0 行星，
/// 却仍显示"存活"、仍持有舰队、仍能 advance，永远等不到任何结局）。
/// 这里给出明确的负局条件。
enum class DefeatKind : u8 {
    None = 0,
    /// 失去全部星系与行星（被征服）
    Conquered,
    /// 连续多季国库破产且无恢复可能
    Bankrupt,
};

struct DefeatStatus {
    DefeatKind kind = DefeatKind::None;
    bool defeated = false;
    std::string reason;
};

/// 纯查询：不修改状态。`victoryPhase` 用它决定是否结束游戏。
[[nodiscard]] DefeatStatus checkDefeat(const GameState& st);

/// 失败结局的文本（用于 advance 输出与 status）
[[nodiscard]] std::string defeatText(const GameState& st);

}  // namespace gf
