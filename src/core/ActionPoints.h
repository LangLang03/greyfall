#pragma once
// 行动点（AP）
#include <string_view>

#include "util/Fixed.h"

namespace gf {

struct ActionPoints {
    int base = 4;
    int bonus = 0;      // 基建/政体加成
    int spent = 0;
    int deferred = 0;   // defer 消耗

    [[nodiscard]] int max() const { return base + bonus; }
    [[nodiscard]] int left() const {
        int v = max() - spent;
        return v < 0 ? 0 : v;
    }
    void reset() { spent = 0; }
    /// 尝试花费；不足返回 false
    [[nodiscard]] bool trySpend(int n) {
        if (left() < n) return false;
        spent += n;
        return true;
    }
    void refund(int n) {
        spent -= n;
        if (spent < 0) spent = 0;
    }
};

/// AP 花费常量
namespace apcost {
inline constexpr int kOrder = 0;        // 下单不耗 AP（市场是自由的）
inline constexpr int kFutures = 0;
inline constexpr int kEnvoy = 1;
inline constexpr int kSpy = 2;
inline constexpr int kColony = 2;
inline constexpr int kShipBuild = 1;
inline constexpr int kFleetOrder = 1;
inline constexpr int kEdict = 1;
inline constexpr int kResearch = 1;
inline constexpr int kBuild = 1;
inline constexpr int kMega = 2;
inline constexpr int kAscend = 3;
inline constexpr int kRecruit = 1;
inline constexpr int kCombine = 0;
inline constexpr int kUseItem = 0;
inline constexpr int kLink = 0;
inline constexpr int kDeduce = 1;
inline constexpr int kDeduceCommit = 2;
inline constexpr int kForgeProve = 1;
inline constexpr int kPropaganda = 1;
}  // namespace apcost

}  // namespace gf
