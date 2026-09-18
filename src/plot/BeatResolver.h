#pragma once
// 剧情幕推进、抉择结算、结局评估。
// 事件系统已拆到 plot/EventSystem.h，经济结算在 domain/Economy.h。
// 经济结算已迁至 domain/Economy.h（此前是本模块的分层违规）。
#include "core/GameState.h"
#include "plot/Skeleton.h"

namespace gf {

struct TickReport;

/// 阶段 12：结论解锁 → 幕次推进 → 结局评估
void plotPhase(GameState& st);

/// 结局评估
[[nodiscard]] int evaluateEnding(const GameState& st);

/// epoch --report 文本
[[nodiscard]] std::string epochReport(const GameState& st);

}  // namespace gf
