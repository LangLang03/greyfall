#include <cstdio>

#include "rng/Streams.h"

#include "util/Bits.h"
#include "util/Str.h"

namespace gf {
namespace {
constexpr std::string_view kNames[kRngStreamCount] = {
    "world", "empire", "market", "vol", "ai", "diplo", "items", "clue", "plot", "combat", "crisis", "epoch",
};
}  // namespace

std::string_view rngStreamName(RngStream s) noexcept {
    std::size_t i = static_cast<std::size_t>(s);
    if (i >= kRngStreamCount) return "?";
    return kNames[i];
}

RngStream rngStreamFromName(std::string_view name) noexcept {
    for (std::size_t i = 0; i < kRngStreamCount; ++i)
        if (kNames[i] == name) return static_cast<RngStream>(i);
    return RngStream::Count;
}

void RngBus::seed(u64 rootSeed, u64 tick) {
    for (std::size_t i = 0; i < kRngStreamCount; ++i) {
        // 每条流由 rootSeed 与流序号经 SplitMix 派生 ⇒ 流之间统计独立
        SplitMix64 sm(rootSeed ^ (0xA5A5A5A5A5A5A5A5ull * (i + 1)) ^ (tick * 0x9E3779B97F4A7C15ull));
        state_[i] = sm.nextU64();
        consumed_[i] = 0;
    }
}

void RngBus::reseedFrom(const RngBus& other) {
    state_ = other.state_;
    consumed_ = other.consumed_;
}

u64 RngBus::nextU64(RngStream s) {
    std::size_t i = static_cast<std::size_t>(s);
    if (i >= kRngStreamCount) i = 0;
    SplitMix64 sm(state_[i]);
    u64 v = sm.nextU64();
    state_[i] = sm.s;
    ++consumed_[i];
    return v;
}

i64 RngBus::range(RngStream s, i64 lo, i64 hi) {
    if (hi <= lo) return lo;
    u64 span = static_cast<u64>(hi - lo) + 1ull;
    return lo + static_cast<i64>(nextU64(s) % span);
}

Fixed RngBus::unit(RngStream s) {
    // 取高 20 位并缩放到 [0, FIX)：raw = (v20 * FIX) >> 20。
    // 早期直接 `Fixed::raw(nextU64 >> 44)` —— 把 20 位整数当成 raw 值，
    // 于是返回值落在 [0, 1048.6] 而非 [0, 1)，**99.9% 的调用 ≥ 1.0**，
    // 所有以 unit() 为基础的判定与定价全部失真。
    u64 v = nextU64(s) >> 44;   // [0, 2^20)
    return Fixed::raw(static_cast<i64>((v * static_cast<u64>(FIX)) >> 20));
}

Fixed RngBus::normal(RngStream s) {
    i64 acc = 0;
    for (int i = 0; i < 12; ++i) acc += static_cast<i64>(nextU64(s) % 1001);
    return Fixed::raw(acc - 6000);
}

bool RngBus::chance(RngStream s, Fixed p) {
    if (p.rawValue() >= FIX) return true;
    if (p.rawValue() <= 0) return false;
    // 必须在**原生整数**域比较：写成 `Fixed::raw(r) < p.rawValue()` 时，
    // 右侧的 i64 会被隐式转换成 Fixed（乘 1000），于是比较变成
    // `r < p.rawValue() * 1000` —— 由于 r ∈ [0,999]，任何正概率都恒为真。
    // 这会让全游戏的每一处概率判定失效（AI 决策、监管命中、间谍成败……）。
    i64 r = static_cast<i64>(nextU64(s) % 1000);
    return r < p.rawValue();
}

std::size_t RngBus::pick(RngStream s, std::size_t count) {
    if (count == 0) return 0;
    return static_cast<std::size_t>(nextU64(s) % count);
}

u64 RngBus::fingerprint() const {
    u64 h = 1469598103934665603ull;
    for (std::size_t i = 0; i < kRngStreamCount; ++i) {
        h ^= state_[i];
        h *= 1099511628211ull;
        h ^= consumed_[i];
        h *= 1099511628211ull;
    }
    return h;
}

}  // namespace gf
