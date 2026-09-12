#pragma once
// 灰域纪元 —— 全局整数别名与定点数（FIX=1000，int64，全程无浮点）
//
// 设计约束（见方案 §1）：
//   * 所有数值一律 int64 定点，FIX = 1000 ⇒ 三位小数精度；
//   * 乘除走 __int128 中间量，并在越界时饱和，绝不 UB；
//   * 不提供 float/double 构造，避免任何隐式浮点进入数值路径。
#include <cstdint>
#include <string>
#include <string_view>

namespace gf {

using i64 = std::int64_t;
using u64 = std::uint64_t;
using i32 = std::int32_t;
using u32 = std::uint32_t;
using i16 = std::int16_t;
using u16 = std::uint16_t;
using i8 = std::int8_t;
using u8 = std::uint8_t;

/// 定点标度：1.000 == 1000
inline constexpr i64 FIX = 1000;

/// 定点数可表示的最大/最小值（以"1.0"为单位）
inline constexpr i64 FIX_MAX_WHOLE = INT64_MAX / FIX;

/// 饱和的 (a*b)/d，中间量使用 128 位。
[[nodiscard]] constexpr i64 mulDivSat(i64 a, i64 b, i64 d) noexcept {
    if (d == 0) return 0;
    __int128 r = static_cast<__int128>(a) * static_cast<__int128>(b);
    r /= static_cast<__int128>(d);
    if (r > static_cast<__int128>(INT64_MAX)) return INT64_MAX;
    if (r < static_cast<__int128>(INT64_MIN)) return INT64_MIN;
    return static_cast<i64>(r);
}

/// 饱和加法
[[nodiscard]] constexpr i64 addSat(i64 a, i64 b) noexcept {
    __int128 r = static_cast<__int128>(a) + static_cast<__int128>(b);
    if (r > static_cast<__int128>(INT64_MAX)) return INT64_MAX;
    if (r < static_cast<__int128>(INT64_MIN)) return INT64_MIN;
    return static_cast<i64>(r);
}

/// 饱和乘法
[[nodiscard]] constexpr i64 mulSat(i64 a, i64 b) noexcept {
    __int128 r = static_cast<__int128>(a) * static_cast<__int128>(b);
    if (r > static_cast<__int128>(INT64_MAX)) return INT64_MAX;
    if (r < static_cast<__int128>(INT64_MIN)) return INT64_MIN;
    return static_cast<i64>(r);
}

/// 整数平方根（向下取整），Newton 迭代，纯整数。
[[nodiscard]] constexpr i64 isqrt(i64 x) noexcept {
    if (x <= 0) return 0;
    u64 r = static_cast<u64>(x);
    u64 g = 1;
    while (g < r) g <<= 1;
    g >>= 1;
    while (true) {
        u64 ng = (g + r / g) >> 1;
        if (ng >= g) break;
        g = ng;
    }
    return static_cast<i64>(g);
}

/// 定点标量。整型可隐式构造（内部自动左移 FIX），浮点构造被删除。
struct Fixed {
    i64 v{};  // 原始定点值（1.0 == 1000）

    constexpr Fixed() noexcept = default;
    constexpr Fixed(int x) noexcept : v(static_cast<i64>(x) * FIX) {}
    constexpr Fixed(long x) noexcept : v(static_cast<i64>(x) * FIX) {}
    constexpr Fixed(long long x) noexcept : v(static_cast<i64>(x) * FIX) {}
    constexpr Fixed(unsigned x) noexcept : v(static_cast<i64>(x) * FIX) {}
    Fixed(double) = delete;
    Fixed(float) = delete;

    /// 由原始定点整数构造（1.0 必须写成 raw(FIX)）
    [[nodiscard]] static constexpr Fixed raw(i64 r) noexcept {
        Fixed f;
        f.v = r;
        return f;
    }
    /// 由千分之一为单位构造：m=31'400 ⇒ 31.400
    [[nodiscard]] static constexpr Fixed milli(i64 m) noexcept { return raw(m); }
    /// 由万分比构造：bp=250 ⇒ 0.025
    [[nodiscard]] static constexpr Fixed bp(i64 basisPoints) noexcept {
        return raw(mulDivSat(basisPoints, FIX, 10000));
    }
    /// 由百分数构造：pct=37 ⇒ 0.37
    [[nodiscard]] static constexpr Fixed pct(i64 percent) noexcept {
        return raw(mulDivSat(percent, FIX, 100));
    }
    /// x·num/den（保留定点精度，用于 bp 以下的细分比例，如 0.06% = 6/10000）
    [[nodiscard]] static constexpr Fixed ratio(Fixed x, i64 num, i64 den) noexcept {
        return raw(mulDivSat(x.v, num, den));
    }

    [[nodiscard]] constexpr i64 rawValue() const noexcept { return v; }
    [[nodiscard]] constexpr i64 whole() const noexcept { return v / FIX; }
    [[nodiscard]] constexpr i64 round() const noexcept {
        return v >= 0 ? (v + FIX / 2) / FIX : -((-v + FIX / 2) / FIX);
    }
    [[nodiscard]] constexpr bool isZero() const noexcept { return v == 0; }
    [[nodiscard]] constexpr bool isNeg() const noexcept { return v < 0; }
    [[nodiscard]] constexpr bool isPos() const noexcept { return v > 0; }

    friend constexpr auto operator<=>(Fixed, Fixed) noexcept = default;

    constexpr Fixed& operator+=(Fixed o) noexcept {
        v = addSat(v, o.v);
        return *this;
    }
    constexpr Fixed& operator-=(Fixed o) noexcept {
        v = addSat(v, -o.v);
        return *this;
    }
    constexpr Fixed& operator*=(Fixed o) noexcept {
        v = mulDivSat(v, o.v, FIX);
        return *this;
    }
    constexpr Fixed& operator/=(Fixed o) noexcept {
        v = mulDivSat(v, FIX, o.v);
        return *this;
    }
};

[[nodiscard]] constexpr Fixed operator+(Fixed a, Fixed b) noexcept {
    return Fixed::raw(addSat(a.v, b.v));
}
[[nodiscard]] constexpr Fixed operator-(Fixed a, Fixed b) noexcept {
    return Fixed::raw(addSat(a.v, -b.v));
}
[[nodiscard]] constexpr Fixed operator-(Fixed a) noexcept { return Fixed::raw(-a.v); }
[[nodiscard]] constexpr Fixed operator*(Fixed a, Fixed b) noexcept {
    return Fixed::raw(mulDivSat(a.v, b.v, FIX));
}
[[nodiscard]] constexpr Fixed operator/(Fixed a, Fixed b) noexcept {
    return Fixed::raw(mulDivSat(a.v, FIX, b.v));
}

[[nodiscard]] constexpr Fixed fxAbs(Fixed a) noexcept { return Fixed::raw(a.v < 0 ? -a.v : a.v); }
[[nodiscard]] constexpr Fixed fxMin(Fixed a, Fixed b) noexcept { return a.v < b.v ? a : b; }
[[nodiscard]] constexpr Fixed fxMax(Fixed a, Fixed b) noexcept { return a.v > b.v ? a : b; }
[[nodiscard]] constexpr Fixed fxClamp(Fixed x, Fixed lo, Fixed hi) noexcept {
    return x.v < lo.v ? lo : (x.v > hi.v ? hi : x);
}
[[nodiscard]] constexpr Fixed fxSign(Fixed a) noexcept {
    return a.v > 0 ? Fixed(1) : (a.v < 0 ? Fixed(-1) : Fixed(0));
}
/// 定点平方根：sqrt(a)，纯整数实现
[[nodiscard]] constexpr Fixed fxSqrt(Fixed a) noexcept {
    if (a.v <= 0) return Fixed(0);
    __int128 t = static_cast<__int128>(a.v) * FIX;
    if (t > static_cast<__int128>(INT64_MAX)) t = static_cast<__int128>(INT64_MAX);
    return Fixed::raw(isqrt(static_cast<i64>(t)));
}
/// 定点幂（整数指数，快速幂，负指数取倒数）
[[nodiscard]] constexpr Fixed fxPow(Fixed base, i64 exp) noexcept {
    bool neg = exp < 0;
    if (neg) exp = -exp;
    Fixed r(1), b = base;
    while (exp > 0) {
        if (exp & 1) r = r * b;
        b = b * b;
        exp >>= 1;
    }
    return neg ? Fixed(1) / r : r;
}

/// 线性插值：a + (b-a)*t，t ∈ [0,1]
[[nodiscard]] constexpr Fixed fxLerp(Fixed a, Fixed b, Fixed t) noexcept {
    return a + (b - a) * t;
}

/// 从十进制字符串解析定点（"31.4" / "-0.75" / "1,200"），失败返回 ok=false
[[nodiscard]] Fixed parseFixed(std::string_view s, bool* ok = nullptr) noexcept;
/// 解析整数，失败返回 fallback
[[nodiscard]] i64 parseInt(std::string_view s, i64 fallback = 0) noexcept;

/// 千分位分组整数串
[[nodiscard]] std::string groupDigits(i64 x);
/// 定点格式化，dec 位小数（0..3），带千分位
[[nodiscard]] std::string fixedStr(Fixed x, int dec = 2);
/// 定点格式化但不分组（紧凑显示，如价格）
[[nodiscard]] std::string fixedStrPlain(Fixed x, int dec = 2);
/// 带符号前缀：+1.25 / -1.25
[[nodiscard]] std::string fixedStrSigned(Fixed x, int dec = 2);

}  // namespace gf
