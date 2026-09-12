#include "mkt/Arbitrage.h"

#include <algorithm>

#include "domain/ModifierUtil.h"
#include "mkt/Matching.h"
#include "mkt/OrderBook.h"

namespace gf {

ArbOpportunity bestArbitrage(const GameState& st, u8 res) {
    ArbOpportunity best;
    best.res = res;
    Fixed bestNet = Fixed(0);
    const bool brokenChain = hasModifier(st.modifierBits, kModBrokenChain);
    for (int a = 0; a < kExchangeCount; ++a) {
        for (int b = 0; b < kExchangeCount; ++b) {
            if (a == b) continue;
            const Book& ba = st.market.exchanges[static_cast<std::size_t>(a)].books[res];
            const Book& bb = st.market.exchanges[static_cast<std::size_t>(b)].books[res];
            const PriceLevel* askA = bookBestAsk(ba);
            const PriceLevel* bidB = bookBestBid(bb);
            if (askA == nullptr || bidB == nullptr) continue;
            if (askA->qty <= 0 || bidB->qty <= 0) continue;
            i64 notionalRef = askA->px.rawValue();
            if (notionalRef <= 0) continue;
            Fixed gross = Fixed::raw(mulDivSat(bidB->px.rawValue() - askA->px.rawValue(), FIX, notionalRef));
            if (gross.rawValue() <= 0) continue;
            const ExchangeInfo& ia = exchangeInfo(a);
            const ExchangeInfo& ib = exchangeInfo(b);
            Fixed cost = ia.fee + ib.fee;
            if (a != b) {
                Fixed transit = ia.transitCost;
                if (brokenChain) transit = transit / Fixed(2);
                cost += transit;
            }
            const ExchangeMarket& xb = st.market.exchanges[static_cast<std::size_t>(b)];
            cost += xb.tariff;
            cost += xb.embargo * Fixed::pct(50);
            Fixed net = gross - cost;
            if (net.rawValue() > bestNet.rawValue()) {
                bestNet = net;
                best.buyExch = static_cast<u8>(a);
                best.sellExch = static_cast<u8>(b);
                best.grossGap = gross;
                best.netGap = net;
                best.capacity = std::min(askA->qty, bidB->qty);
            }
        }
    }
    if (bestNet.rawValue() <= 0) best.netGap = bestNet;
    return best;
}

std::vector<ArbOpportunity> arbitrageList(const GameState& st, int limit) {
    std::vector<ArbOpportunity> out;
    for (int c = 0; c < kCommodityCount; ++c) {
        if (!commodityInfo(c).tradable) continue;
        ArbOpportunity o = bestArbitrage(st, static_cast<u8>(c));
        if (o.netGap.rawValue() <= 0 && o.grossGap.rawValue() <= 0) continue;
        out.push_back(o);
    }
    std::sort(out.begin(), out.end(), [](const ArbOpportunity& a, const ArbOpportunity& b) {
        return a.netGap.rawValue() > b.netGap.rawValue();
    });
    if (limit > 0 && static_cast<int>(out.size()) > limit) out.resize(static_cast<std::size_t>(limit));
    return out;
}

void arbitrageUpdate(GameState& st) {
    for (int c = 0; c < kCommodityCount; ++c) {
        if (!commodityInfo(c).tradable) continue;
        ArbOpportunity o = bestArbitrage(st, static_cast<u8>(c));
        st.market.arbGap[static_cast<std::size_t>(c)] = o.netGap;
        if (o.netGap.rawValue() <= 0 || o.capacity <= 0) continue;
        // 套利者吃价：向收敛方向推动两侧报价
        Book& ba = st.market.exchanges[o.buyExch].books[static_cast<std::size_t>(c)];
        Book& bb = st.market.exchanges[o.sellExch].books[static_cast<std::size_t>(c)];
        // 套利者也要受规模约束：单次最多吃掉该标的典型季成交量的 3%，
        // 否则会机械性地每季吃穿做市商报价，把市场打成一潭死水。
        const CommodityInfo& ci = commodityInfo(c);
        i64 cap = std::max<i64>(1, ci.typicalVolume / 30);
        i64 push = std::min<i64>(o.capacity, cap);
        push = std::min<i64>(push, std::max<i64>(1, o.capacity / 3));
        MkOrder buyOrder;
        buyOrder.id = st.market.nextOrderId++;
        buyOrder.owner = kNoEmpire;
        buyOrder.buy = true;
        buyOrder.qty = push;
        buyOrder.kind = OrderKind::Market;
        MkResult rb = matchOrder(ba, buyOrder, Fixed(0));
        MkOrder sellOrder;
        sellOrder.id = st.market.nextOrderId++;
        sellOrder.owner = kNoEmpire;
        sellOrder.buy = false;
        sellOrder.qty = rb.filled > 0 ? rb.filled : push;
        sellOrder.kind = OrderKind::Market;
        MkResult rs = matchOrder(bb, sellOrder, Fixed(0));
        if (rb.filled > 0 && rs.filled > 0) {
            st.market.tickFills += 1;
        }
    }
}

}  // namespace gf
