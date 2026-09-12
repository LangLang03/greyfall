#pragma once
// ComputeBudget —— AI 的计算预算与智能等级
#include "util/Fixed.h"

namespace gf {

struct ComputeBudget {
    i64 nodeBudget = 24000;     // 本季剩余节点预算
    i64 nodeSpent = 0;
    u8 foresight = 2;           // 1..4，前瞻深度（tick）
    int topK = 10;              // 候选动作剪枝
    int topM = 4;               // 对玩家的策略路径剪枝

    [[nodiscard]] i64 left() const { return nodeBudget - nodeSpent; }
    [[nodiscard]] bool canAfford(i64 cost) const { return left() >= cost; }
    bool spend(i64 cost) {
        if (!canAfford(cost)) return false;
        nodeSpent += cost;
        return true;
    }
    void reset(i64 budget, u8 fs) {
        nodeBudget = budget;
        nodeSpent = 0;
        foresight = fs;
    }
    /// 由难度推导
    static ComputeBudget forDifficulty(int difficulty) {
        ComputeBudget b;
        b.foresight = static_cast<u8>(std::min(4, 1 + difficulty / 2));
        if (b.foresight < 1) b.foresight = 1;
        b.nodeBudget = 4000 + static_cast<i64>(difficulty) * 8000;
        b.topK = 6 + difficulty;
        b.topM = 2 + difficulty / 2;
        return b;
    }
};

}  // namespace gf
