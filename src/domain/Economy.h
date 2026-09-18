#pragma once
// 经济结算：收入、维护费、产出、GDP 与国力评分、研究推进。
//
// 这个模块此前住在 `plot/BeatResolver.cpp` 里 —— 那是分层违规：
// `domain` 层的经济规则被放在剧情层，导致
//   ① 经济逻辑与事件文本/幕次推进混在一个 653 行的 God 文件里；
//   ② 依赖方向倒置（读经济规则要先拉进整个剧情模块）；
//   ③ 任何剧情改动都可能意外影响经济。
// 搬迁到 `domain/` 后，`plot/` 只负责剧情，经济只依赖 `domain` 与 `mkt`。

namespace gf {

struct GameState;

/// 阶段 13：收入 / 维护 / 折旧 / 产出 / GDP 与国力评分。
/// 在 tick 末尾、其余所有阶段之后执行（收入到账后才能被下一季的开支使用）。
void economyPhase(GameState& st);

/// 研究推进：立项 + 逐季投入资金（受最短工期与季度上限约束）。
/// 由 `economyPhase` 在其末尾调用，顺序不可调换 ——
/// 研究预算取自本季结算后的国库，提前调用会让 AI 永远分不到预算。
void researchTick(GameState& st);

}  // namespace gf
