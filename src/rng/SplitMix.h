#pragma once
// SplitMix64 —— 最小可复现伪随机核心（无浮点、跨平台字节一致）
#include <cstdint>

#include "util/Bits.h"
#include "util/Fixed.h"

namespace gf {

struct SplitMix64 {
    u64 s = 0;

    explicit constexpr SplitMix64(u64 seed = 0) noexcept : s(seed) {}

    [[nodiscard]] constexpr u64 nextU64() noexcept {
        s = s + 0x9E3779B97F4A7C15ull;
        u64 z = s;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    [[nodiscard]] constexpr u32 nextU32() noexcept { return static_cast<u32>(nextU64() >> 32); }

    /// [0, bound) 无偏拒绝采样
    [[nodiscard]] constexpr u32 nextBelow(u32 bound) noexcept {
        if (bound == 0) return 0;
        u32 limit = 0xFFFFFFFFu - (0xFFFFFFFFu % bound);
        while (true) {
            u32 r = nextU32();
            if (r < limit) return r % bound;
        }
    }

    /// [lo, hi] 闭区间
    [[nodiscard]] constexpr i64 range(i64 lo, i64 hi) noexcept {
        if (hi <= lo) return lo;
        u64 span = static_cast<u64>(hi - lo) + 1ull;
        return lo + static_cast<i64>(nextU64() % span);
    }

    /// [0,1) 定点
    [[nodiscard]] constexpr Fixed unit() noexcept {
        return Fixed::raw(static_cast<i64>(nextU64() >> 44));  // 20 bit ⇒ 0..1048575 (0..1048.575)
    }

    /// 近似正态（12 均匀和减 6），返回定点，标准差 ≈ 1
    [[nodiscard]] Fixed normal() noexcept {
        i64 acc = 0;
        for (int i = 0; i < 12; ++i) acc += static_cast<i64>(nextU64() % 1001);
        return Fixed::raw(acc - 6000);
    }

    [[nodiscard]] bool chance(Fixed p) noexcept {
        i64 r = static_cast<i64>(nextU64() % 1000);
        return r < p.rawValue();
    }
};

}  // namespace gf
