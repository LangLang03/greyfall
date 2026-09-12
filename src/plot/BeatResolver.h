#pragma once
// 剧情幕推进、事件调度落地、结局评估、经济结算
#include "core/GameState.h"
#include "plot/Skeleton.h"

namespace gf {

struct TickReport;

/// 阶段 10：事件调度器（异常/危机/剧情幕）
void eventsPhase(GameState& st, TickReport& rep);

/// 阶段 12：结论解锁 → 幕次推进 → 结局评估
void plotPhase(GameState& st);

/// 阶段 13：收入/维护/折旧/种族张力
void economyPhase(GameState& st);

/// 自动结算一个抉择（headless / bots 用）
void resolveChoiceAuto(GameState& st, int optionIndex);

/// 玩家结算抉择
[[nodiscard]] bool resolveChoice(GameState& st, std::size_t index, int optionIndex, std::string* err);

/// 延后抉择
[[nodiscard]] bool deferChoice(GameState& st, std::size_t index, int ticks, std::string* err);

/// 结局评估
[[nodiscard]] int evaluateEnding(const GameState& st);

/// epoch --report 文本
[[nodiscard]] std::string epochReport(const GameState& st);

/// 事件文本渲染
[[nodiscard]] std::string pendingText(const GameState& st, const PendingChoice& c);

}  // namespace gf
