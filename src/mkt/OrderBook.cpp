#include "domain/Sector.h"
#include "mkt/Matching.h"
#include "mkt/OrderBook.h"

#include <algorithm>

namespace gf {
void bookRebuildLevels(Book& b) {
    b.bids.clear();
    b.asks.clear();
    // orders 以 (px, seq) 稳定序维护 ⇒ 直接顺序扫描即可得到价格-时间优先的档位
    std::stable_sort(b.orders.begin(), b.orders.end(), [](const Order& x, const Order& y) {
        if (x.buy != y.buy) return x.buy;              // 买单在前
        if (x.px.rawValue() != y.px.rawValue()) {
            return x.buy ? x.px.rawValue() > y.px.rawValue() : x.px.rawValue() < y.px.rawValue();
        }
        return x.seq < y.seq;
    });
    for (const Order& o : b.orders) {
        i64 remaining = o.qty - o.filled;
        if (remaining <= 0) continue;
        auto& side = o.buy ? b.bids : b.asks;
        if (!side.empty() && side.back().px.rawValue() == o.px.rawValue()) {
            side.back().qty += remaining;
            side.back().nOrders += 1;
        } else {
            if (side.size() >= static_cast<std::size_t>(kBookDepthMax)) continue;
            side.push_back(PriceLevel{o.px, remaining, 1});
        }
    }
}

const PriceLevel* bookBestBid(const Book& b) { return b.bids.empty() ? nullptr : &b.bids.front(); }
const PriceLevel* bookBestAsk(const Book& b) { return b.asks.empty() ? nullptr : &b.asks.front(); }

Fixed bookMid(const Book& b) {
    const PriceLevel* bid = bookBestBid(b);
    const PriceLevel* ask = bookBestAsk(b);
    if (bid && ask) return Fixed::raw((bid->px.rawValue() + ask->px.rawValue()) / 2);
    if (bid) return bid->px;
    if (ask) return ask->px;
    return b.last;
}

Fixed bookSpread(const Book& b) {
    const PriceLevel* bid = bookBestBid(b);
    const PriceLevel* ask = bookBestAsk(b);
    if (!bid || !ask) return Fixed(0);
    Fixed mid = bookMid(b);
    if (mid.rawValue() <= 0) return Fixed(0);
    return Fixed::raw(mulDivSat(ask->px.rawValue() - bid->px.rawValue(), FIX, mid.rawValue()));
}

void bookInsert(Book& b, const Order& o) {
    b.orders.push_back(o);
    bookRebuildLevels(b);
    b.mid = bookMid(b);
    b.spread = bookSpread(b);
}

bool bookRemove(Book& b, u64 orderId) {
    auto it = std::find_if(b.orders.begin(), b.orders.end(), [orderId](const Order& o) { return o.id == orderId; });
    if (it == b.orders.end()) return false;
    b.orders.erase(it);
    bookRebuildLevels(b);
    b.mid = bookMid(b);
    b.spread = bookSpread(b);
    return true;
}

Order* bookFind(Book& b, u64 orderId) {
    for (auto& o : b.orders)
        if (o.id == orderId) return &o;
    return nullptr;
}

i64 bookDepthQty(const Book& b, bool buy, int levels) {
    const auto& side = buy ? b.bids : b.asks;
    i64 total = 0;
    int n = 0;
    for (const auto& lv : side) {
        if (n >= levels) break;
        total += lv.qty;
        ++n;
    }
    return total;
}

Fixed bookDepthNotional(const Book& b, bool buy, int levels) {
    const auto& side = buy ? b.bids : b.asks;
    i64 notional = 0;
    int n = 0;
    for (const auto& lv : side) {
        if (n >= levels) break;
        notional += mulDivSat(lv.px.rawValue(), lv.qty, 1);
        ++n;
    }
    return Fixed::raw(notional);
}

Fixed bookEstimatePrice(const Book& b, bool buy, i64 qty, i64* fillable) {
    const auto& side = buy ? b.asks : b.bids;
    i64 want = qty;
    i64 got = 0;
    i64 notional = 0;
    for (const auto& lv : side) {
        if (want <= 0) break;
        i64 take = lv.qty < want ? lv.qty : want;
        notional += mulDivSat(lv.px.rawValue(), take, 1);
        got += take;
        want -= take;
    }
    if (fillable) *fillable = got;
    if (got <= 0) {
        const PriceLevel* best = buy ? bookBestAsk(b) : bookBestBid(b);
        return best ? best->px : b.last;
    }
    return Fixed::raw(notional / got);
}

i64 bookResolveCrossed(Book& b, int maxRounds,
    const std::function<void(const Order&, const Order&, Fixed, i64)>& settle) {
    i64 trades = 0;
    for (int round = 0; round < maxRounds; ++round) {
        Order* bestBid = nullptr;
        Order* bestAsk = nullptr;
        for (auto& o : b.orders) {
            if (o.qty - o.filled <= 0) continue;
            if (o.buy) {
                if (bestBid == nullptr || o.px.rawValue() > bestBid->px.rawValue() ||
                    (o.px.rawValue() == bestBid->px.rawValue() && o.seq < bestBid->seq))
                    bestBid = &o;
            } else {
                if (bestAsk == nullptr || o.px.rawValue() < bestAsk->px.rawValue() ||
                    (o.px.rawValue() == bestAsk->px.rawValue() && o.seq < bestAsk->seq))
                    bestAsk = &o;
            }
        }
        if (bestBid == nullptr || bestAsk == nullptr) break;
        if (bestBid->px.rawValue() < bestAsk->px.rawValue()) break;   // 未交叉，正常

        // 自成交防护：同主体（且不是做市商）的交叉直接撤销较晚挂出的一笔
        if (bestBid->owner != kNoEmpire && bestBid->owner == bestAsk->owner) {
            Order* later = bestBid->seq > bestAsk->seq ? bestBid : bestAsk;
            later->filled = later->qty;
            continue;
        }

        i64 bidLeft = bestBid->qty - bestBid->filled;
        i64 askLeft = bestAsk->qty - bestAsk->filled;
        i64 qty = bidLeft < askLeft ? bidLeft : askLeft;
        if (qty <= 0) break;
        // 成交价取先挂出的一方（被动方）
        Fixed px = bestAsk->seq < bestBid->seq ? bestAsk->px : bestBid->px;
        if (settle) settle(*bestBid, *bestAsk, px, qty);
        bestBid->filled += qty;
        bestAsk->filled += qty;
        bookRecordTrade(b, px, qty);
        b.netFlow += qty;   // 交叉清算视为均衡成交，净流中性
        b.netFlow -= qty;
        ++trades;
    }
    {
        b.orders.erase(std::remove_if(b.orders.begin(), b.orders.end(),
                                      [](const Order& o) { return o.filled >= o.qty; }),
                       b.orders.end());
        bookRebuildLevels(b);
        b.mid = bookMid(b);
        b.spread = bookSpread(b);
    }
    return trades;
}

i64 bookExpireDayOrders(Book& b, u64 tick) {
    i64 removed = 0;
    auto it = std::remove_if(b.orders.begin(), b.orders.end(), [tick, &removed](const Order& o) {
        if (o.tif == Tif::Day && o.placedTick < tick) {
            ++removed;
            return true;
        }
        return false;
    });
    if (removed > 0) {
        b.orders.erase(it, b.orders.end());
        bookRebuildLevels(b);
        b.mid = bookMid(b);
        b.spread = bookSpread(b);
    }
    return removed;
}

i64 bookActiveQty(const Book& b, bool buy) {
    i64 total = 0;
    for (const Order& o : b.orders) {
        if (o.buy != buy) continue;
        i64 r = o.qty - o.filled;
        if (r > 0) total += r;
    }
    return total;
}

void bookClear(Book& b) {
    b.orders.clear();
    b.bids.clear();
    b.asks.clear();
}

}  // namespace gf
