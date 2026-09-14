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

/// 由难度推导的「侵略性」系数（定点，1.0 = 基准）。
///
/// 为什么需要它：难度此前只影响 AI 的**计算质量**（前瞻深度、节点预算），
/// 完全不改变它的**行为倾向**。结果是难度 1 与难度 5 的 AI 一样激进 ——
/// 实测难度 3 下玩家（3 星系 / 2 舰队 / 12 万 cr）在 40 季内被夺走全部领土，
/// 没有任何新手缓冲期。设计文档明确要求「强度参数化（--difficulty 决定
/// foresight/compute）」并给出「AI 透视导致必输体验」的风险对策，
/// 这条曲线就是那个对策。
///
/// 曲线：难度 1 → 0.45（明显克制），3 → 0.90（默认），5 → 1.25（毫不留情）。
[[nodiscard]] inline Fixed aiAggression(int difficulty) {
    const int d = difficulty < 1 ? 1 : (difficulty > 5 ? 5 : difficulty);
    // 用整数万分比表示，避免浮点
    static constexpr i64 kNum[5] = {45, 70, 90, 108, 125};
    return Fixed::pct(kNum[d - 1]);
}

/// 由难度推导的「对玩家的宣战意愿」系数。比 aggression 更陡，
/// 因为宣战是玩家体验里最直接的压力来源。
[[nodiscard]] inline Fixed aiWarWillingness(int difficulty) {
    const int d = difficulty < 1 ? 1 : (difficulty > 5 ? 5 : difficulty);
    static constexpr i64 kNum[5] = {30, 60, 100, 135, 170};
    return Fixed::pct(kNum[d - 1]);
}

}  // namespace gf
