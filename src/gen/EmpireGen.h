#pragma once
// 帝国生成：种族/伦理/公民/政体采样 + 首都分配 + 初始经济
#include "core/GameState.h"

namespace gf {

struct EmpireGenOptions {
    int count = 12;
    int difficulty = 2;
    u64 seed = 0;
};

void generateEmpires(GameState& st, const EmpireGenOptions& opts);
/// 领袖人格 / 目标权重 / 初始心智
void initMind(Empire& e, int difficulty, RngBus& rng);
/// 由修正表汇总某帝国的修正值（0..1 为常用量纲）
Fixed empireModifier(const Empire& e, ModKind kind);

}  // namespace gf
