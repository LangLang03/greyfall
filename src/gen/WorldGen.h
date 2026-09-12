#pragma once
// 世界生成入口
#include <string>

#include "core/GameState.h"
#include "util/Fixed.h"

namespace gf {

struct WorldGenOptions {
    u64 seed = 0;
    int difficulty = 2;
    int empireCount = 12;
    int systemCount = 64;
    bool endless = false;
    u64 legacyMask = 0;
    u32 epochIndex = 0;
    std::string epochName;
    /// 纪元继承：上一纪元的帝国名/负债/仇敌（可空）
    std::string legacyNote;
};

/// 生成一个完整可玩的世界（星图 + 行星 + 帝国 + 交易所 + 期货曲线 + 危机表 + 剧情骨架 + 初始持仓）
void generateWorld(GameState& st, const WorldGenOptions& opts);

/// 只重建市场（新纪元和测试用）
void initMarket(GameState& st);

}  // namespace gf
