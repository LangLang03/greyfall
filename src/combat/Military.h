#pragma once
// 军备与造舰：军力不是开局写死的数字，而是需要**时间积累**的产出
//
// 设计动机：
//   早期版本里 `empire.military` 由 EmpireGen 一次性写成 400~900，
//   舰队 strength 也在开局定死（80~260）且永不增长。
//   结果是「谁开局强谁赢」—— 最高难度下玩家开局不久就能零伤亡打穿全图。
//   这里引入军备积累：
//     * 军力目标由经济体量、行星开发度、船坞建筑共同决定
//     * 实际军力以**有限速率**逼近目标（造舰需要时间，被打残也要时间恢复）
//     * 难度为 AI 提供实质加成（而非仅仅「更聪明」）
#include "util/Fixed.h"

namespace gf {

struct GameState;

/// 某帝国的军力目标（含难度加成）
[[nodiscard]] Fixed militaryTargetOf(const GameState& st, u32 empire);

/// 每 tick 推进军备：军力向目标靠拢，并按军力重新分配舰队强度
void militaryPhase(GameState& st);

/// 难度对 AI 的军备加成倍率（玩家为 1.0）
[[nodiscard]] Fixed difficultyMilitaryBonus(const GameState& st, u32 empire);

/// 文本：当前军备状态
[[nodiscard]] std::string militaryReport(const GameState& st, u32 empire);

}  // namespace gf
