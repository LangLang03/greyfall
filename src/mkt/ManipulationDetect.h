#pragma once
// 操纵检测：wash / spoof / corner / pump&dump / front-running / insider
#include "core/GameState.h"
#include "mkt/MarketState.h"

namespace gf {

struct ManipFinding {
    u32 actor = 0;
    ManipKind kind = ManipKind::WashTrading;
    Fixed score = Fixed(0);
    std::string detail;
};

/// 登记一次下单 / 撤单 / 成交（供序列表征使用）
void manipRecordPlace(GameState& st, u32 actor, bool buy, i64 qty);
void manipRecordCancel(GameState& st, u32 actor, i64 qty);
void manipRecordFill(GameState& st, u32 actor, bool buy, i64 qty);

/// 全部检测器（每 tick 调用）
[[nodiscard]] std::vector<ManipFinding> manipulationDetect(const GameState& st);

/// 监管裁决：概率 p = base + 0.05·violence，含罚没与信用崩塌
void manipulationEnforce(GameState& st, const std::vector<ManipFinding>& findings);

/// 某主体当前被查概率
[[nodiscard]] Fixed regulatorHitProbability(const GameState& st, u32 actor);

/// 更新主体统计
[[nodiscard]] ActorMarketStats& actorStatsOf(MarketState& m, u32 actor);

}  // namespace gf
