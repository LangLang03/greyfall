#pragma once
// 每子系统独立随机流 + 消费计数（消费计数入档 ⇒ 重放可精确复现）
#include <array>
#include <cstddef>
#include <string_view>

#include "rng/SplitMix.h"

namespace gf {

enum class RngStream : u8 {
    World = 0,   // 星图/星系/行星
    Empire = 1,  // 阵营生成与内政
    Market = 2,  // 订单流与撮合
    Vol = 3,     // 波动率/跳跃
    Ai = 4,      // AI 决策采样
    Diplo = 5,   // 外交与信誉噪声
    Items = 6,   // 配方成败
    Clue = 7,    // 线索传播与伪造
    Plot = 8,    // 剧情幕与事件调度
    Combat = 9,  // 战斗结算
    Crisis = 10, // 危机/异常
    Epoch = 11,  // 纪元生成
    Count = 12,
};

inline constexpr std::size_t kRngStreamCount = static_cast<std::size_t>(RngStream::Count);

[[nodiscard]] std::string_view rngStreamName(RngStream s) noexcept;
/// 由名字解析流（用于 selftest 与调试），未知返回 Count
[[nodiscard]] RngStream rngStreamFromName(std::string_view name) noexcept;

/// 全局随机总线：每条流有独立状态与消费计数
class RngBus {
public:
    RngBus() = default;

    void seed(u64 rootSeed, u64 tick = 0);
    void reseedFrom(const RngBus& other);

    [[nodiscard]] u64 nextU64(RngStream s);
    [[nodiscard]] u32 nextU32(RngStream s) { return static_cast<u32>(nextU64(s) >> 32); }
    [[nodiscard]] i64 range(RngStream s, i64 lo, i64 hi);
    [[nodiscard]] Fixed unit(RngStream s);
    [[nodiscard]] Fixed normal(RngStream s);
    [[nodiscard]] bool chance(RngStream s, Fixed p);
    /// 从容器中均匀取一个下标；空容器返回 0
    [[nodiscard]] std::size_t pick(RngStream s, std::size_t count);
    /// Fisher-Yates 洗牌
    template <typename T>
    void shuffle(RngStream s, T* data, std::size_t n) {
        for (std::size_t i = n; i > 1; --i) {
            std::size_t j = pick(s, i);
            T tmp = data[i - 1];
            data[i - 1] = data[j];
            data[j] = tmp;
        }
    }

    [[nodiscard]] u64 consumed(RngStream s) const { return consumed_[static_cast<std::size_t>(s)]; }
    void setConsumed(RngStream s, u64 v) { consumed_[static_cast<std::size_t>(s)] = v; }
    [[nodiscard]] const std::array<u64, kRngStreamCount>& states() const { return state_; }
    void setStates(const std::array<u64, kRngStreamCount>& st) { state_ = st; }
    [[nodiscard]] const std::array<u64, kRngStreamCount>& counters() const { return consumed_; }
    void setCounters(const std::array<u64, kRngStreamCount>& c) { consumed_ = c; }

    /// 全流派生指纹（入 chronicle）
    [[nodiscard]] u64 fingerprint() const;

private:
    std::array<u64, kRngStreamCount> state_{};
    std::array<u64, kRngStreamCount> consumed_{};
};

}  // namespace gf
