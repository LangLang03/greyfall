#include "mkt/Futures.h"

#include <algorithm>

#include "core/TickPipeline.h"
#include "mkt/Margin.h"
#include "mkt/OrderBook.h"

namespace gf {

Fixed futuresFairPrice(Fixed spot, Fixed carry, Fixed convenience) {
    return spot * (Fixed(1) + (carry - convenience) / Fixed(1));
}

Fixed convenienceYield(const GameState& st, u8 res) {
    // 全市场流通库存 vs 典型量：库存低 → 便利收益高
    i64 totalStock = 0;
    for (const auto& e : st.empires) totalStock += e.stock[res].rawValue() / FIX;
    const CommodityInfo& ci = commodityInfo(static_cast<int>(res));
    i64 typical = ci.typicalVolume / 4 + 1;
    if (totalStock >= typical * 2) return Fixed(0);
    Fixed scarcity = Fixed(1) - Fixed::raw(mulDivSat(totalStock, FIX, typical * 2));
    if (scarcity.rawValue() < 0) scarcity = Fixed(0);
    return Fixed::raw(10) * scarcity;   // 最高 10‰ 的便利收益
}

void futuresUpdateCurve(GameState& st, int exch) {
    ExchangeMarket& x = st.market.exchanges[static_cast<std::size_t>(exch)];
    for (int c = 0; c < kCommodityCount; ++c) {
        const CommodityInfo& ci = commodityInfo(c);
        if (!ci.tradable) continue;
        Fixed spot = x.books[static_cast<std::size_t>(c)].mid;
        if (spot.rawValue() <= 0) continue;
        Fixed conv = convenienceYield(st, static_cast<u8>(c));
        for (int t = 0; t < kFuturesTerms; ++t) {
            FuturesQuote& f = x.futures[static_cast<std::size_t>(c)][static_cast<std::size_t>(t)];
            // 持有成本：仓储 + 保险 + 资金成本，随期限线性增长
            Fixed carry = ci.storage * Fixed(t + 1) + Fixed::raw(4) * Fixed(t + 1);
            if (x.tariff.rawValue() > 0) carry += x.tariff;
            f.carry = carry;
            f.convenience = conv;
            // carry 与 conv 本身就是**分数**（0.010 = 1%），不能再除以 1000。
            // 早期多除了 1000，使持有成本对价格毫无影响 ——
            // 结果是 F1~F4 恒等于现货价（基差 0），期限结构形同虚设，
            // 跨期套利与展期策略也就无从谈起。
            Fixed fair = futuresFairPrice(spot, carry, conv);
            // 平滑：向理论价靠拢 40%，避免曲线抖动
            f.price = f.price.rawValue() > 0 ? fxLerp(f.price, fair, Fixed::pct(40)) : fair;
            f.basis = Fixed::raw(mulDivSat(f.price.rawValue() - spot.rawValue(), FIX, spot.rawValue()));
        }
    }
}

void futuresUpdateAll(GameState& st) {
    for (int e = 0; e < kExchangeCount; ++e) futuresUpdateCurve(st, e);
}

const char* futuresStructureName(const GameState& st, u8 res) {
    const ExchangeMarket& x = st.market.exchanges[kExchCX];
    Fixed near = x.futures[res][0].price;
    Fixed far = x.futures[res][kFuturesTerms - 1].price;
    if (near.rawValue() <= 0 || far.rawValue() <= 0) return "无报价";
    i64 diff = far.rawValue() - near.rawValue();
    i64 threshold = near.rawValue() / 100;   // 1%
    if (diff > threshold) return "contango（期货升水）";
    if (diff < -threshold) return "backwardation（期货贴水）";
    return "flat（平坦）";
}

void futuresSettleExpiry(GameState& st, TickReport& rep) {
    MarketState& m = st.market;
    for (auto& p : m.futuresPositions) {
        if (p.qty == 0) continue;
        FuturesQuote& q = m.exchanges[kExchCX].futures[static_cast<std::size_t>(p.res)]
                                                   [static_cast<std::size_t>(p.term)];
        Fixed spot = m.exchanges[kExchCX].books[static_cast<std::size_t>(p.res)].mid;
        const u64 expiry = p.expiryTick > 0 ? p.expiryTick : (p.openedTick / 4 + p.term + 1) * 4 - 1;
        if (st.tick >= expiry) {
            Fixed diff = spot - p.entry;
            Fixed pnl = Fixed::raw(mulDivSat(diff.rawValue(), p.qty, 1));
            if (p.isShort) pnl = Fixed::raw(-pnl.rawValue());
            Empire* owner = st.empire(p.owner);
            if (owner != nullptr) {
                Fixed& cash = p.owner == kPlayerId ? m.margin.cash : owner->treasury;
                cash += pnl + p.margin;
                owner->treasury = cash;
            }
            q.openInterest -= p.qty;
            st.logEvent(LogPhase::Market, "market.futures.settle",
                        "合约交割：资源 " + std::string(commodityName(p.res)) + " ×" + std::to_string(p.qty) +
                            "，盈亏 " + fixedStrSigned(pnl, 0),
                        p.owner, pnl);
            rep.notional += Fixed::raw(mulDivSat(spot.rawValue(), p.qty, 1));
            p.qty = 0;
            p.margin = Fixed(0);
        }
    }
    m.futuresPositions.erase(
        std::remove_if(m.futuresPositions.begin(), m.futuresPositions.end(),
                       [](const FuturesPosition& p) { return p.qty == 0; }),
        m.futuresPositions.end());
    futuresUpdateAll(st);
}

bool futuresOpen(GameState& st, u8 res, u8 term, i64 qty, bool isShort, int leverage, std::string* err) {
    return futuresOpen(st, kPlayerId, res, term, qty, isShort, leverage, err);
}

bool futuresOpen(GameState& st, u32 owner, u8 res, u8 term, i64 qty, bool isShort, int leverage,
                 std::string* err) {
    if (res >= kCommodityCount) {
        if (err) *err = "非法资源";
        return false;
    }
    if (term >= kFuturesTerms) {
        if (err) *err = "非法期限（F1..F4）";
        return false;
    }
    if (qty <= 0) {
        if (err) *err = "数量必须为正";
        return false;
    }
    if (leverage < 1 || leverage > 20) {
        if (err) *err = "--lev 必须在 1..20";
        return false;
    }
    MarketState& m = st.market;
    FuturesQuote& q = m.exchanges[kExchCX].futures[res][term];
    Fixed notional = Fixed::raw(mulDivSat(q.price.rawValue(), qty, 1));
    const CommodityInfo& ci = commodityInfo(static_cast<int>(res));
    Fixed need = marginRequired(notional, ci.volatility, leverage, term + 1);
    const bool isPlayer = (owner == kPlayerId);
    Empire* ow = st.empire(owner);
    if (ow == nullptr || !ow->alive || !ci.tradable || q.price.rawValue() <= 0) {
        if (err) *err = "非法主体、资源或期货报价";
        return false;
    }
    Fixed avail = isPlayer ? m.margin.cash : (ow != nullptr ? ow->treasury : Fixed(0));
    if (avail.rawValue() < need.rawValue()) {
        if (err) *err = "保证金不足：需要 " + fixedStr(need, 0) + "，可用 " + fixedStr(avail, 0);
        return false;
    }
    if (isPlayer) {
        m.margin.cash -= need;
        ow->treasury = m.margin.cash;
    } else if (ow != nullptr) {
        ow->treasury -= need;
    }
    // 合并同向持仓
    const u64 expiryTick = (st.tick / 4 + term + 1) * 4 - 1;
    for (auto& p : m.futuresPositions) {
        if (p.owner == owner && p.expiryTick == expiryTick && p.res == res && p.term == term && p.isShort == isShort && p.leverage == leverage) {
            Fixed oldNotional = Fixed::raw(mulDivSat(p.entry.rawValue(), p.qty, 1));
            Fixed newAvg = Fixed::raw(mulDivSat(oldNotional.rawValue() + notional.rawValue(), 1, p.qty + qty));
            p.entry = newAvg;
            p.qty += qty;
            p.margin += need;
            q.openInterest += qty;
            return true;
        }
    }
    FuturesPosition p;
    p.owner = owner;
    p.expiryTick = expiryTick;
    p.res = res;
    p.term = term;
    p.qty = qty;
    p.entry = q.price;
    p.margin = need;
    p.leverage = leverage;
    p.isShort = isShort;
    p.openedTick = st.tick;
    m.futuresPositions.push_back(p);
    q.openInterest += qty;
    st.logEvent(LogPhase::Market, "market.futures.open",
                std::string(isShort ? "卖出" : "买入") + " " + std::string(commodityName(res)) + " F" +
                    std::to_string(term + 1) + " ×" + std::to_string(qty) + " @ " + fixedStrPlain(q.price, 2) +
                    "（杠杆 " + std::to_string(leverage) + "，保证金 " + fixedStr(need, 0) + "）",
                owner, notional);
    return true;
}

bool futuresClose(GameState& st, u8 res, u8 term, i64 qty, std::string* err) {
    if (res >= kCommodityCount || term >= kFuturesTerms || qty <= 0) {
        if (err) *err = "资源、期限或平仓数量无效";
        return false;
    }
    MarketState& m = st.market;
    bool closed = false;
    for (auto& p : m.futuresPositions) {
        if (p.owner != kPlayerId || p.res != res || p.term != term || p.qty == 0) continue;
        i64 take = std::min(qty, p.qty);
        FuturesQuote& q = m.exchanges[kExchCX].futures[res][term];
        Fixed diff = q.price - p.entry;
        Fixed pnl = Fixed::raw(mulDivSat(diff.rawValue(), take, 1));
        if (p.isShort) pnl = Fixed::raw(-pnl.rawValue());
        Fixed released = Fixed::raw(mulDivSat(p.margin.rawValue(), take, p.qty));
        m.margin.cash += pnl + released;
        st.empires[kPlayerId].treasury = m.margin.cash;
        p.qty -= take;
        p.margin -= released;
        q.openInterest -= take;
        qty -= take;
        closed = true;
        st.logEvent(LogPhase::Market, "market.futures.close",
                    "平仓 " + std::string(commodityName(res)) + " F" + std::to_string(term + 1) + " ×" +
                        std::to_string(take) + "，盈亏 " + fixedStrSigned(pnl, 0),
                    kPlayerId, pnl);
        if (qty == 0) break;
    }
    if (closed) {
        m.futuresPositions.erase(std::remove_if(m.futuresPositions.begin(), m.futuresPositions.end(),
            [](const FuturesPosition& p) { return p.qty == 0; }), m.futuresPositions.end());
        return true;
    }
    if (err) *err = "没有可平的对应持仓";
    return false;
}

}  // namespace gf
