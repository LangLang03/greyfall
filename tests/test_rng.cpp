// 随机总线：概率分布与流独立性
#include <algorithm>
#include <cmath>

#include "check.h"
#include "rng/Streams.h"

using namespace gf;

// 回归守卫（严重）：chance 曾在**混合域**比较 ——
// 写成 `Fixed::raw(r) < p.rawValue()` 时，右侧的 i64 会被隐式转换为 Fixed（乘 1000），
// 于是判定变成 `r < p.rawValue() * 1000`。由于 r ∈ [0,999]，
// **任何正概率都恒为真** —— 全游戏每一处概率门槛（AI 决策、监管命中、
// 间谍成败、债务违约、代理人资助……）全部失效。
TEST(rng, chance_respects_probability) {
    struct Case {
        int pct;
        double lo;
        double hi;
    };
    const Case cases[] = {{1, 0.002, 0.025}, {5, 0.035, 0.065}, {12, 0.10, 0.14},
                          {50, 0.47, 0.53},  {90, 0.88, 0.92}};
    for (const auto& c : cases) {
        RngBus r;
        r.seed(0xC0FFEEull);
        int hits = 0;
        const int N = 60000;
        for (int i = 0; i < N; ++i)
            if (r.chance(RngStream::Ai, Fixed::pct(c.pct))) ++hits;
        double rate = static_cast<double>(hits) / N;
        CHECK(rate >= c.lo);
        CHECK(rate <= c.hi);
    }
}

TEST(rng, chance_boundaries_are_exact) {
    RngBus r;
    r.seed(1234);
    // 0% 恒为假
    for (int i = 0; i < 1000; ++i) CHECK(!r.chance(RngStream::Ai, Fixed(0)));
    CHECK(!r.chance(RngStream::Ai, Fixed::raw(0)));
    // 100% 恒为真
    for (int i = 0; i < 1000; ++i) CHECK(r.chance(RngStream::Ai, Fixed(1)));
    CHECK(r.chance(RngStream::Ai, Fixed::pct(100)));
}

TEST(rng, chance_consumes_exactly_one_draw) {
    // 每次判定恰好消耗一次抽样（保证回放确定性）
    RngBus a;
    a.seed(999);
    RngBus b;
    b.seed(999);
    for (int i = 0; i < 500; ++i) {
        (void)a.chance(RngStream::Ai, Fixed::pct(37));
        (void)b.nextU64(RngStream::Ai);
    }
    CHECK_EQ(a.consumed(RngStream::Ai), b.consumed(RngStream::Ai));
    CHECK_EQ(a.consumed(RngStream::Ai), 500u);
}

TEST(rng, unit_and_range_are_bounded) {
    RngBus r;
    r.seed(4242);
    for (int i = 0; i < 3000; ++i) {
        Fixed u = r.unit(RngStream::World);
        CHECK(u.rawValue() >= 0);
        CHECK(u.rawValue() < FIX);
    }
    for (int i = 0; i < 3000; ++i) {
        i64 v = r.range(RngStream::World, 5, 9);
        CHECK(v >= 5);
        CHECK(v <= 9);
    }
    // 退化区间
    CHECK_EQ(r.range(RngStream::World, 7, 7), 7);
    CHECK_EQ(r.range(RngStream::World, 9, 3), 9);
}

TEST(rng, pick_is_in_bounds_and_covers) {
    RngBus r;
    r.seed(777);
    std::vector<int> hits(5, 0);
    for (int i = 0; i < 5000; ++i) {
        std::size_t idx = r.pick(RngStream::Items, 5);
        CHECK(idx < 5);
        ++hits[idx];
    }
    for (int h : hits) CHECK(h > 0);
    // 空容器
    CHECK_EQ(r.pick(RngStream::Items, 0), 0u);
}

TEST(rng, streams_are_independent_and_reproducible) {
    // 不同流的序列不应相同
    RngBus a;
    a.seed(31337);
    RngBus b;
    b.seed(31337);
    bool differs = false;
    for (int i = 0; i < 64; ++i) {
        if (a.nextU64(RngStream::Combat) != b.nextU64(RngStream::Market)) differs = true;
    }
    CHECK(differs);
    // 同种子同流必须完全一致
    RngBus c;
    c.seed(31337);
    RngBus d;
    d.seed(31337);
    for (int i = 0; i < 64; ++i) CHECK_EQ(c.nextU64(RngStream::Crisis), d.nextU64(RngStream::Crisis));
}

TEST(rng, range_covers_endpoints_without_bias) {
    RngBus r;
    r.seed(13579);
    int lo = 0, hi = 0;
    double sum = 0;
    const int N = 120000;
    for (int i = 0; i < N; ++i) {
        i64 v = r.range(RngStream::World, 1, 6);
        CHECK(v >= 1);
        CHECK(v <= 6);
        if (v == 1) ++lo;
        if (v == 6) ++hi;
        sum += static_cast<double>(v);
    }
    // 六个值应大致等概率（各约 1/6）
    CHECK(lo > N / 6 * 8 / 10);
    CHECK(lo < N / 6 * 12 / 10);
    CHECK(hi > N / 6 * 8 / 10);
    CHECK(hi < N / 6 * 12 / 10);
    // 均值应为 3.5
    CHECK(std::fabs(sum / N - 3.5) < 0.05);
}

TEST(rng, normal_has_unit_stddev) {
    // normal() 是「12 个均匀值之和减 6」，标准差应约为 1
    RngBus r;
    r.seed(24680);
    double sum = 0, sum2 = 0;
    const int N = 200000;
    for (int i = 0; i < N; ++i) {
        double v = static_cast<double>(r.normal(RngStream::Vol).rawValue()) / 1000.0;
        sum += v;
        sum2 += v * v;
    }
    double mean = sum / N;
    double var = sum2 / N - mean * mean;
    CHECK(std::fabs(mean) < 0.05);
    CHECK(std::fabs(std::sqrt(var) - 1.0) < 0.05);
}

TEST(rng, shuffle_permutes_without_losing_elements) {
    RngBus r;
    r.seed(112233);
    std::vector<int> deck = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<int> sorted = deck;
    int changed = 0;
    for (int round = 0; round < 200; ++round) {
        std::vector<int> a = deck;
        r.shuffle(RngStream::Items, a.data(), a.size());
        std::vector<int> s = a;
        std::sort(s.begin(), s.end());
        // 洗牌必须保持元素集合不变
        CHECK(s == sorted);
        if (a != deck) ++changed;
    }
    // 洗牌应当确实改变顺序（几乎每一轮）
    CHECK(changed > 190);
}

TEST(rng, distribution_is_roughly_uniform) {
    RngBus r;
    r.seed(24680);
    const int buckets = 10;
    std::vector<int> hist(buckets, 0);
    const int N = 100000;
    for (int i = 0; i < N; ++i) {
        Fixed u = r.unit(RngStream::Vol);
        int b = static_cast<int>(u.rawValue() * buckets / FIX);
        if (b < 0) b = 0;
        if (b >= buckets) b = buckets - 1;
        ++hist[static_cast<std::size_t>(b)];
    }
    int expect = N / buckets;
    for (int h : hist) {
        // 允许 ±20% 偏差
        CHECK(h > expect * 8 / 10);
        CHECK(h < expect * 12 / 10);
    }
}
