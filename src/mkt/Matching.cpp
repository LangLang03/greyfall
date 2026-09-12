#include "mkt/Matching.h"

#include <algorithm>

#include "domain/Sector.h"
#include "mkt/OrderBook.h"

namespace gf {
namespace {

constexpr i64 kImpactY = 700;    // Y = 0.7
constexpr i64 kImpactKappa = 350;  // κ = 0.35

void sortSides(std::vector<Order>& orders) {
    std::stable_sort(orders.begin(), orders.end(), [](const Order& x, const Order& y) {
        if (x.buy != y.buy) return x.buy;
        if (x.px.rawValue() != y.px.rawValue())
            return x.buy ? x.px.rawValue() > y.px.rawValue() : x.px.rawValue() < y.px.rawValue();
        return x.seq < y.seq;
    });
}

}  // namespace

Fixed impactTemporary(Fixed sigma, i64 qty, Fixed v20, Fixed liquidityScale) {
    if (qty <= 0) return Fixed(0);
    // Q / V20 必须在定点域里算：Fixed(qty) / v20（否则会丢掉三个数量级）
    Fixed v = v20.rawValue() > 0 ? v20 : Fixed(1);
    Fixed ratio = Fixed(qty) / v;
    Fixed root = fxSqrt(ratio);
    Fixed impact = Fixed::raw(kImpactY) * sigma * root;
    return impact * liquidityScale;
}

Fixed impactPermanent(Fixed tempImpact) { return tempImpact * Fixed::raw(kImpactKappa); }

Fixed impactDecay(Fixed temp) { return temp / Fixed(2); }

void bookRecordTrade(Book& b, Fixed px, i64 qty) {
    b.last = px;
    if (qty <= 0) return;
    if (b.volume <= 0) {
        b.vwap = px;
    } else {
        i64 totalNotional = mulDivSat(b.vwap.rawValue(), b.volume, 1);
        totalNotional += mulDivSat(px.rawValue(), qty, 1);
        b.vwap = Fixed::raw(totalNotional / (b.volume + qty));
    }
    if (b.volume == 0) {
        b.open = px;
        b.high = px;
        b.low = px;
    }
    b.volume += qty;
    if (px.rawValue() > b.high.rawValue() || b.high.rawValue() == 0) b.high = px;
    if (px.rawValue() < b.low.rawValue() || b.low.rawValue() == 0) b.low = px;
}

MkResult matchOrder(Book& b, const MkOrder& in, Fixed impactReserve) {
    MkResult res;
    res.remaining = in.qty;
    if (in.qty <= 0) return res;
    // 价格-时间优先：先稳定排序活动订单
    sortSides(b.orders);

    std::vector<std::size_t> touched;
    i64 want = in.qty;
    i64 notional = 0;
    for (std::size_t i = 0; i < b.orders.size() && want > 0; ++i) {
        Order& o = b.orders[i];
        if (o.buy == in.buy) continue;            // 只吃对手方
        if (o.owner == in.owner && in.owner != kNoEmpire) continue;  // 无自成交
        i64 avail = o.qty - o.filled;
        if (avail <= 0) continue;
        if (in.kind != OrderKind::Market) {
            bool crosses = in.buy ? (o.px.rawValue() <= in.px.rawValue()) : (o.px.rawValue() >= in.px.rawValue());
            if (!crosses) break;  // 已排序 ⇒ 后续都不越价
        }
        i64 take = std::min(avail, want);
        // 冰量单简化为隐藏展示量，成交时允许立即补出剩余量。
        o.filled += take;
        notional += mulDivSat(o.px.rawValue(), take, 1);
        want -= take;
        MkFill f;
        f.passiveId = o.id;
        f.passiveOwner = o.owner;
        f.passiveController = o.controller;
        f.qty = take;
        f.px = o.px;
        res.consumed.push_back(f);
        touched.push_back(i);
        bookRecordTrade(b, o.px, take);
        // 主动成交净流：驱动跨所价差收敛与黑市溢价压缩
        b.netFlow += in.buy ? take : -take;
    }
    res.filled = in.qty - want;
    res.remaining = want;
    res.avgPx = res.filled > 0 ? Fixed::raw(notional / res.filled) : Fixed(0);
    res.levelsConsumed = static_cast<i64>(res.consumed.size());

    // 冲击：以实际成交量计算
    if (res.filled > 0) {
        Fixed temp = impactTemporary(b.sigma, res.filled, b.var20, Fixed(1));
        res.impactTemp = temp;
        res.impactPerm = impactPermanent(temp);
        b.impactTemp += temp;
        b.impactPerm += res.impactPerm;
        (void)impactReserve;
    }

    // 清理完全成交的订单
    b.orders.erase(std::remove_if(b.orders.begin(), b.orders.end(),
                                  [](const Order& o) { return o.filled >= o.qty; }),
                   b.orders.end());
    bookRebuildLevels(b);
    b.mid = bookMid(b);
    b.spread = bookSpread(b);
    return res;
}

}  // namespace gf
