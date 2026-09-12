#pragma once
// 战斗结算（HOI4 风格）：组织度驱动的多 tick 战斗
//
// 与旧版的关键差别：
//   * 战斗是**持续状态**，不是一次性掷骰。每 tick 双方结算伤害、损耗组织度，
//     某一方组织度归零才撤退/溃败。
//   * **战斗宽度**限制同时展开的战力，堆叠兵力会溢出（无法全部参战）。
//   * **指挥官**提供攻/防/后勤/宽度修正，并随战斗累积经验。
//   * **老练度**由舰队累积经验决定，长期存活的舰队越打越强。
//   * **地形**（星云/小行星/巨构/要塞）给防守方加成。
#include "core/GameState.h"
#include "domain/Commander.h"

namespace gf {

struct TickReport;

struct BattleOutcome {
    u32 attacker = 0;
    u32 defender = 0;
    u32 system = 0;
    Fixed attackerLoss = Fixed(0);
    Fixed defenderLoss = Fixed(0);
    bool attackerWon = false;
    Fixed warScoreDelta = Fixed(0);
    std::string narrative;
};

/// 舰队战力（含士气/补给/老练度/指挥官，不含组织度）
[[nodiscard]] Fixed fleetPower(const GameState& st, u32 fleetId);
/// 舰队的组织度上限
[[nodiscard]] Fixed fleetMaxOrg(const GameState& st, const Fleet& f);
/// 星系防御值
[[nodiscard]] Fixed systemDefense(const GameState& st, u32 system);
/// 双方胜率（0..1），用于 AI 的战争 EV
[[nodiscard]] Fixed combatOdds(const GameState& st, u32 attacker, u32 defender);
/// 地形名与防守加成
[[nodiscard]] std::string terrainNameOf(const GameState& st, u32 system);
[[nodiscard]] Fixed terrainDefenseBonus(const GameState& st, u32 system);

/// 开启一场战斗（若该星系已有战斗则把舰队并入）
[[nodiscard]] u32 beginBattle(GameState& st, u32 system, u32 attacker, u32 defender);
/// 推进所有进行中的战斗一 tick；返回本 tick 结束的战斗数
int battlePhase(GameState& st, TickReport& rep);
/// 纯计算的胜率（供 AI 前瞻用，不改变状态）
[[nodiscard]] Fixed simulateBattleOdds(const GameState& st, u32 system, u32 attacker, u32 defender);

/// 阶段 9：舰队移动、战斗推进、战线结算
void combatPhase(GameState& st, TickReport& rep);
void fleetMovementPhase(GameState& st);

/// 战斗报告文本
[[nodiscard]] std::string battleReportText(const Battle& b, const GameState& st);
/// 当前所有战斗的摘要（battles 命令）
[[nodiscard]] std::string battlesText(const GameState& st, u32 filterOwner);

/// 指挥官管理
[[nodiscard]] u32 recruitCommander(GameState& st, u32 empire, std::string name, CommanderTrait trait);
[[nodiscard]] bool assignCommander(GameState& st, u32 commanderId, u32 fleetId, std::string* err);
[[nodiscard]] std::string commandersText(const GameState& st, u32 empire);

/// 组织度与补给恢复（每 tick）
void fleetRecoveryPhase(GameState& st);

}  // namespace gf
