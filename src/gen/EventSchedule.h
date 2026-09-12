#pragma once
// 事件调度器与异常点分布
#include "core/GameState.h"

namespace gf {

/// 生成危机时间表：按权重与 tick 排布未来事件
void scheduleEvents(GameState& st);
/// 把异常点分布到星系（部分星系有异常）
void generateAnomalies(GameState& st);
/// 初始化线索超图（玩家已知的起点线索 + 边）
void initClueGraph(GameState& st);
/// 抽取下一个事件（返回事件 id；-1 表示本 tick 无事件）
[[nodiscard]] int pickEvent(GameState& st, EventPhase phase);

}  // namespace gf
