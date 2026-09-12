#pragma once
// 自动制衡（balance of power）与遏制联盟
#include "core/GameState.h"
#include "util/Fixed.h"

namespace gf {

struct Coalition {
    u32 leader = 0;
    std::vector<u32> members;
    u32 target = 0;
    Fixed strength = Fixed(0);
    Fixed threshold = Fixed(0);
    bool formed = false;
    std::string reason;
};

/// 若玩家 scoreIdx > 0.55 → 形成遏制联盟（军备 + 封锁 + 联邦投票串联）
[[nodiscard]] Coalition evaluateCoalition(const GameState& st, u32 target);

/// 执行联盟行动
void powerBalancingPhase(GameState& st);

/// 玩家在国力序中的分位（0..1）
[[nodiscard]] Fixed powerIndexRank(const GameState& st, u32 actor);

/// 报告文本（intel --threat 的一部分）
[[nodiscard]] std::string coalitionReport(const GameState& st, u32 target);

}  // namespace gf
