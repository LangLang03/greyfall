#pragma once
// AI 主管线：阶段 4/5/6 的实现
#include "ai/ComputeBudget.h"
#include "ai/ForwardSimulator.h"
#include "ai/IntentPredictor.h"
#include "ai/OmniscientReader.h"
#include "core/GameState.h"

namespace gf {

struct TickReport;

/// 阶段 4：OmniscientReader 快照（玩家全字段 + rollbackCount + 各字段暴露路径）
void aiReadPlayer(GameState& st);

/// 阶段 5：ToModel 更新 + FraudDetect 异常打分
void aiUpdatePlayerModel(GameState& st);

/// 阶段 6：IntentPredictor + ForwardSimulator → 各 AI 阵营动作
void aiTakeActions(GameState& st, TickReport& rep);

/// AI 执行一个具体动作（供 ForwardSimulator 选中后落地）
void aiExecuteAction(GameState& st, u32 actor, const AiAction& a, TickReport& rep);

/// intel --threat：某 AI 对玩家的威胁摘要
[[nodiscard]] std::string threatReport(const GameState& st, u32 observer);

/// AI 建造：按当前瓶颈（产出缺口 / 研究 / 防御）选一项建筑并落地。
/// 返回是否真的建成了。
[[nodiscard]] bool aiConstructBest(struct GameState& st, u32 empire);

/// AI 建造阶段：每 tick 为「闲置资金充裕」的 AI 帝国投建一项建筑。
///
/// 建造是**持续性活动**，不该与一次性行动（外交/市场/间谍）在同一张
/// EV 表上竞争 —— 实测这样做的结果是 AI 从不建造（240 季零建筑），
/// 国库却囤到 19.6 万。
void aiConstructionPhase(struct GameState& st);

/// AI 研究阶段：按收入比例持续投入研究点（与玩家的 research 命令同换算率）
void aiResearchPhase(struct GameState& st);

/// 侦察：玩家 spy 某 AI 得到的情报
[[nodiscard]] std::string spyReport(const GameState& st, u32 target, int budget);

}  // namespace gf
