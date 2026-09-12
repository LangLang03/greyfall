// 定点数学库与输入解析的性质检验
//
// 存在的理由：本项目曾两次因「定点与原生整数混用」产生**静默**缺陷 ——
// Fixed::raw(N) 把值写小 1000 倍、chance() 被隐式提升为定标值而恒为真。
// 这类缺陷不崩溃、不报错，功能测试反而更容易通过；
// 只有**与参考值比对的性质检验**能抓住它们。
#include <cmath>

#include "check.h"
#include "util/Fixed.h"
#include "util/Str.h"

using namespace gf;

TEST(fixed, sqrt_matches_reference) {
    const double xs[] = {0.0, 0.01, 0.25, 1.0, 2.0, 4.0, 9.0, 100.0, 10000.0};
    for (double x : xs) {
        Fixed r = fxSqrt(Fixed::raw(static_cast<i64>(x * 1000)));
        double got = static_cast<double>(r.rawValue()) / 1000.0;
        CHECK(std::fabs(got - std::sqrt(x)) < 0.05);
    }
    // 负数与零
    CHECK_EQ(fxSqrt(Fixed(0)).rawValue(), 0);
    CHECK_EQ(fxSqrt(Fixed(-4)).rawValue(), 0);
    // 单调性
    Fixed prev = Fixed(0);
    for (int i = 1; i <= 200; ++i) {
        Fixed r = fxSqrt(Fixed(i));
        CHECK(r.rawValue() >= prev.rawValue());
        prev = r;
    }
}

TEST(fixed, pow_matches_reference) {
    const double bases[] = {1.0, 2.0, 0.5, 1.5};
    const int exps[] = {0, 1, 2, 3, 4, -1, -2};
    for (double b : bases) {
        for (int e : exps) {
            Fixed r = fxPow(Fixed::raw(static_cast<i64>(b * 1000)), e);
            double got = static_cast<double>(r.rawValue()) / 1000.0;
            double want = std::pow(b, e);
            CHECK(std::fabs(got - want) < 0.02);
        }
    }
    // 任何数的 0 次幂为 1
    CHECK_EQ(fxPow(Fixed(7), 0).rawValue(), Fixed(1).rawValue());
}

TEST(fixed, lerp_matches_reference) {
    CHECK_EQ(fxLerp(Fixed(0), Fixed(1), Fixed::raw(500)).rawValue(), Fixed::raw(500).rawValue());
    CHECK_EQ(fxLerp(Fixed(10), Fixed(20), Fixed::raw(250)).rawValue(), Fixed::raw(12500).rawValue());
    // 负区间插值
    CHECK_EQ(fxLerp(Fixed(-5), Fixed(5), Fixed::raw(500)).rawValue(), 0);
    // t=0/t=1 应落在端点
    CHECK_EQ(fxLerp(Fixed(3), Fixed(9), Fixed(0)).rawValue(), Fixed(3).rawValue());
    CHECK_EQ(fxLerp(Fixed(3), Fixed(9), Fixed(1)).rawValue(), Fixed(9).rawValue());
}

TEST(fixed, mul_div_roundtrip_and_signs) {
    CHECK_EQ((Fixed(3) * Fixed(4)).rawValue(), Fixed(12).rawValue());
    CHECK_EQ((Fixed(1) / Fixed(4)).rawValue(), Fixed::raw(250).rawValue());
    CHECK_EQ((Fixed(-3) * Fixed(4)).rawValue(), Fixed(-12).rawValue());
    CHECK_EQ((Fixed(1) / Fixed(-4)).rawValue(), Fixed::raw(-250).rawValue());
    CHECK_EQ((Fixed(-1) / Fixed(4)).rawValue(), Fixed::raw(-250).rawValue());
    // 小值相乘不能塌陷为 0（这是定点精度的经典陷阱）
    Fixed tiny = Fixed::raw(100) * Fixed::raw(100);   // 0.1 × 0.1
    CHECK_EQ(tiny.rawValue(), Fixed::raw(10).rawValue());
    // 除法精度
    Fixed third = Fixed(7) / Fixed(3);
    CHECK(std::fabs(static_cast<double>(third.rawValue()) / 1000.0 - 7.0 / 3.0) < 0.002);
}

TEST(fixed, saturation_does_not_wrap) {
    Fixed big = Fixed::raw(INT64_MAX);
    // 溢出必须饱和而不是回绕成负数
    Fixed sum = big + big;
    CHECK(sum.rawValue() > 0);
    Fixed neg = Fixed::raw(INT64_MIN);
    Fixed diff = neg - big;
    CHECK(diff.rawValue() < 0);
}

TEST(fixed, clamp_and_minmax) {
    CHECK_EQ(fxClamp(Fixed(5), Fixed(1), Fixed(3)).rawValue(), Fixed(3).rawValue());
    CHECK_EQ(fxClamp(Fixed(-5), Fixed(1), Fixed(3)).rawValue(), Fixed(1).rawValue());
    CHECK_EQ(fxClamp(Fixed(2), Fixed(1), Fixed(3)).rawValue(), Fixed(2).rawValue());
    CHECK_EQ(fxMin(Fixed(1), Fixed(2)).rawValue(), Fixed(1).rawValue());
    CHECK_EQ(fxMax(Fixed(1), Fixed(2)).rawValue(), Fixed(2).rawValue());
    CHECK_EQ(fxAbs(Fixed(-7)).rawValue(), Fixed(7).rawValue());
    CHECK_EQ(fxSign(Fixed(-7)).rawValue(), Fixed(-1).rawValue());
    CHECK_EQ(fxSign(Fixed(0)).rawValue(), 0);
    CHECK_EQ(fxSign(Fixed(7)).rawValue(), Fixed(1).rawValue());
}

TEST(fixed, pct_and_bp_scales) {
    // pct 是百分之一，bp 是万分之一 —— 混用会让量级差 100 倍
    CHECK_EQ(Fixed::pct(100).rawValue(), FIX);
    CHECK_EQ(Fixed::pct(1).rawValue(), 10);
    CHECK_EQ(Fixed::bp(10000).rawValue(), FIX);
    CHECK_EQ(Fixed::bp(100).rawValue(), 10);
    CHECK_EQ(Fixed::bp(1).rawValue(), 0);   // 万分之一次于定点精度
    // milli 是千分之一
    CHECK_EQ(Fixed::milli(1000).rawValue(), FIX);
    CHECK_EQ(Fixed::milli(250).rawValue(), Fixed::raw(250).rawValue());
}

TEST(fixed, muldivsat_saturates) {
    CHECK_EQ(mulDivSat(100, 3, 2), 150);
    CHECK_EQ(mulDivSat(100, 1, 3), 33);
    CHECK_EQ(mulDivSat(0, 5, 7), 0);
    CHECK_EQ(mulDivSat(-100, 3, 2), -150);
    // 除以零必须安全返回 0，而不是崩溃
    CHECK_EQ(mulDivSat(100, 5, 0), 0);
    // 极大值不溢出
    i64 big = mulDivSat(INT64_MAX / 2, 1000, 1);
    CHECK(big > 0);
}

TEST(fixed, parse_fixed_handles_formats) {
    bool ok = false;
    CHECK_EQ(parseFixed("31.4", &ok).rawValue(), Fixed::raw(31400).rawValue());
    CHECK(ok);
    CHECK_EQ(parseFixed("-0.75", &ok).rawValue(), Fixed::raw(-750).rawValue());
    CHECK(ok);
    CHECK_EQ(parseFixed("1,200", &ok).rawValue(), Fixed(1200).rawValue());
    CHECK(ok);
    CHECK_EQ(parseFixed("42", &ok).rawValue(), Fixed(42).rawValue());
    CHECK(ok);
    CHECK_EQ(parseFixed("0", &ok).rawValue(), 0);
    CHECK(ok);
    // 非法输入必须报告失败而不是静默返回 0
    (void)parseFixed("abc", &ok);
    CHECK(!ok);
    (void)parseFixed("", &ok);
    CHECK(!ok);
}

TEST(fixed, parse_int_handles_fallback) {
    CHECK_EQ(parseInt("123", -1), 123);
    CHECK_EQ(parseInt("-45", -1), -45);
    CHECK_EQ(parseInt("0", -1), 0);
    // 失败时回退到给定值（而不是 0 —— 否则无法区分「解析失败」与「真的是 0」）
    CHECK_EQ(parseInt("abc", -7), -7);
    CHECK_EQ(parseInt("", -9), -9);
}
