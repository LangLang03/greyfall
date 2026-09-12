#pragma once
// 国内派系双层博弈 + AI 的代理人战
#include "core/GameState.h"
#include "domain/Domestic.h"

namespace gf {

/// 阶段 8：国内派系诉求、民心、政变风险
void domesticPhase(GameState& st);

/// AI 直接向玩家的国内派系输送资金/情报（代理人战）
void factionAiPhase(GameState& st);

/// 满足某派系的诉求
[[nodiscard]] bool satisfyFaction(GameState& st, u32 empire, FactionKind kind, std::string* err);

/// 压制某派系
[[nodiscard]] bool suppressFaction(GameState& st, u32 empire, FactionKind kind, std::string* err);

/// 政变评估
[[nodiscard]] Fixed coupRiskOf(const GameState& st, u32 empire);

/// 文本报告
[[nodiscard]] std::string domesticReport(const GameState& st, u32 empire);

}  // namespace gf
