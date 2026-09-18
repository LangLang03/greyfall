#pragma once
// 事件系统：危机时间表、常规事件抽取、事件效果结算、抉择文本渲染。
//
// 这一步拆分的动机：`plot/BeatResolver.cpp` 原本是 653 行的 God 文件，
// 混合了事件效果、抉择结算、幕次推进、经济结算、研究推进、结局评估与
// epoch 报告共 7 种职责。经济结算已迁到 `domain/Economy`（见该文件注释），
// 这里再把**事件系统**独立出来 —— 它与剧情幕次推进是两条正交的链路：
//   · 事件系统：TickReport + 帝国/危机状态 → 触发事件、施加效果、产生抉择
//   · 剧情系统：线索图 + 结论 → 推进幕次、评估结局
// 二者唯一的交点是 `PendingChoice`（事件产生抉择，剧情消费抉择）。
// 继续混在一个文件里，任何剧情改动都要重新理解事件调度，反之亦然。

#include <string>

namespace gf {

struct GameState;
struct TickReport;
struct PendingChoice;

/// 阶段 10：事件调度器（危机时间表 / 异常 / 常规抽事件）。
void eventsPhase(GameState& st, TickReport& rep);

/// 抉择的文本渲染（玩家可见的事件描述 + 选项列表）。
[[nodiscard]] std::string pendingText(const GameState& st, const PendingChoice& c);

/// 自动结算一个抉择（headless / bots / 自动化试玩用）。
/// `optionIndex >= 0` 强制指定选项；传 -1 表示**理性择优**
/// （优先不消耗国库的选项 —— 事件文本普遍把"代价：国库 -"放在第 0 项，
/// 无脑选 0 会让基准测试持续替玩家做最贵的选择）。
void resolveChoiceAuto(GameState& st, int optionIndex);

/// 玩家结算抉择
[[nodiscard]] bool resolveChoice(GameState& st, std::size_t index, int optionIndex, std::string* err);

/// 延后抉择
[[nodiscard]] bool deferChoice(GameState& st, std::size_t index, int ticks, std::string* err);

}  // namespace gf
