#pragma once
// 线索超图：可信度传播 / 衰减 / 矛盾裁定 / 发现
#include "clue/ClueDef.h"
#include "core/GameState.h"

namespace gf {

struct TickReport;

/// 阶段 11：可信度传播 & 衰减 & 矛盾裁定
void cluePhase(GameState& st, TickReport& rep);

/// 玩家手工连接线索
[[nodiscard]] bool clueLink(GameState& st, u16 a, u16 b, ClueEdgeKind kind, std::string* err);

/// 断开
[[nodiscard]] bool clueUnlink(GameState& st, u16 a, u16 b, std::string* err);

/// 归档
[[nodiscard]] bool clueArchive(GameState& st, u16 id, std::string* err);

/// 发现一条线索（带来源）
void clueDiscover(GameState& st, u16 id, const Provenance& prov, TickReport* rep);

/// 可信度计算
[[nodiscard]] Fixed clueCredibility(const GameState& st, u16 id);

/// 矛盾裁定：可信度高者胜
void adjudicateContradictions(GameState& st);

/// 最小充分集枚举：在合取范式上求最小充分集
struct MinimalSet {
    std::vector<u16> nodes;
    bool diverse = false;       // 来源多样性 ≥3
    bool noUnresolved = false;  // 无未裁定矛盾
    bool hasInsider = false;    // 至少 1 条来自对手内部
    bool satisfiesAll = false;
};
[[nodiscard]] std::vector<MinimalSet> minimalSatisfyingSets(const GameState& st, u16 conclusion);

/// 结论是否可解锁
[[nodiscard]] bool conclusionUnlockable(const GameState& st, u16 conclusion, std::string* why);

/// 推断报告（deduce --explain）
[[nodiscard]] std::string deduceReport(const GameState& st, u16 conclusion);

/// 提交结论（deduce --commit），错判有真代价
[[nodiscard]] bool commitConclusion(GameState& st, u16 conclusion, std::string* err);

/// 线索列表文本
[[nodiscard]] std::string clueListText(const GameState& st, bool onlyUnlinked, const std::string& tagFilter,
                                       const std::string& subjectFilter);

}  // namespace gf
