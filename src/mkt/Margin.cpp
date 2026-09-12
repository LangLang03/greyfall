#include "mkt/Margin.h"

#include <algorithm>

#include "core/TickPipeline.h"
#include "mkt/Matching.h"
#include "mkt/MarketEngine.h"
#include "mkt/OrderBook.h"

namespace gf {

Fixed marginRequired(Fixed notional, Fixed sigma, int leverage, int termTicks) {
    // m0 = 5%, m1 = 0.15：require = notional·(m0 + m1·σ·√T·lev)
    Fixed m0 = Fixed::pct(5);
    Fixed sigmaTerm = sigma * fxSqrt(Fixed(termTicks)) * Fixed(leverage);
    Fixed m1 = Fixed::bp(1500);
    Fixed ratio = m0 + m1 * sigmaTerm;
    if (ratio.rawValue() < m0.rawValue()) ratio = m0;
    if (ratio.rawValue() > FIX) ratio = Fixed(1);
    return notional * ratio;
}

namespace {

Fixed futuresPnl(const MarketState& m, const FuturesPosition& p) {
    const FuturesQuote& q =
        m.exchanges[kExchCX].futures[static_cast<std::size_t>(p.res)][static_cast<std::size_t>(p.term)];
    Fixed diff = q.price - p.entry;
    Fixed pnl = Fixed::raw(mulDivSat(diff.rawValue(), p.qty, 1));
    return p.isShort ? Fixed::raw(-pnl.rawValue()) : pnl;
}

}  // namespace

bool marginMarkToMarket(GameState& st) {
    MarketState& m = st.market;
    Fixed equity = m.margin.cash;
    Fixed blocked = Fixed(0);
    for (const auto& p : m.futuresPositions) {
        if (p.owner != kPlayerId || p.qty == 0) continue;
        blocked += p.margin;
        equity += p.margin + futuresPnl(m, p);
    }
    for (const auto& p : m.positions) {
        if (p.qty == 0) continue;
        Fixed spot = m.exchanges[kExchCX].books[static_cast<std::size_t>(p.res)].mid;
        const Empire* player = st.empire(kPlayerId);
        i64 held = player == nullptr ? 0 : std::max<i64>(0, std::min(p.qty, player->stock[p.res].rawValue() / FIX));
        // 买入已从现金扣除本金；权益应加回仍持有现货的全部市值。
        equity += spot * Fixed(held);
    }
    m.margin.equity = equity;
    return equity.rawValue() < blocked.rawValue();
}

int marginCascade(GameState& st, TickReport& rep) {
    MarketState& m = st.market;
    int depth = 0;
    const int kMaxDepth = 8;
    m.margin.cascadeDepth = 0;

    while (depth < kMaxDepth) {
        bool breached = marginMarkToMarket(st);
        if (!breached) break;
        // 追保 → 强制平仓：以市价吃穿簿深度
        ++depth;
        m.margin.callActive = true;
        m.margin.cascadeDepth = depth;
        i64 liquidatedQty = 0;
        const auto positions = m.positions;
        for (const auto& p : positions) {
            const Empire* player = st.empire(kPlayerId);
            if (p.qty <= 0 || player == nullptr) continue;
            OrderRequest order;
            order.owner = kPlayerId;
            order.res = p.res;
            order.exch = kExchCX;
            order.buy = false;
            order.qty = std::min(p.qty / 2 + 1, player->stock[p.res].rawValue() / FIX);
            order.kind = OrderKind::Market;
            if (order.qty <= 0) continue;
            OrderAck ack = marketSubmitOrder(st, order);
            if (ack.filled > 0) {
                liquidatedQty += ack.filled;
                ++m.margin.forcedLiquidations;
                const Book& b = m.exchanges[kExchCX].books[p.res];
                m.shocks.push_back(ShockRecord{st.tick, p.res, ack.impact, "强制平仓级联", b.sigma, b.sigma});
            }
        }
        // 期货强平
        for (auto& fp : m.futuresPositions) {
            if (fp.owner != kPlayerId || fp.qty == 0) continue;
            FuturesQuote& q = m.exchanges[kExchCX].futures[static_cast<std::size_t>(fp.res)]
                                                       [static_cast<std::size_t>(fp.term)];
            Fixed loss = futuresPnl(m, fp);
            if (loss.rawValue() >= -fp.margin.rawValue() / 2) continue;  // 未触及强平线
            m.margin.cash += fp.margin + loss;
            st.empires[kPlayerId].treasury = m.margin.cash;
            q.openInterest -= fp.qty;
            ++m.margin.forcedLiquidations;
            liquidatedQty += fp.qty;
            fp.qty = 0;
            fp.margin = Fixed(0);
        }
        if (liquidatedQty == 0) break;   // 无可平仓位 ⇒ 无法继续收敛
    }
    if (depth > 1) {
        st.logEvent(LogPhase::Market, kLogCascade,
                    "CAUTION: cascade depth=" + std::to_string(depth) + "，强制平仓 " +
                        std::to_string(m.margin.forcedLiquidations) + " 次",
                    kPlayerId, Fixed(depth));
    }
    if (depth > 0) {
        st.logEvent(LogPhase::Market, kLogMarginCall,
                    "追保触发：权益 " + fixedStr(m.margin.equity, 0) + "，级联深度 " + std::to_string(depth),
                    kPlayerId);
        rep.notional = rep.notional;  // 级联不改变成交量统计口径
    }
    (void)marginMarkToMarket(st);
    m.margin.callActive = false;
    return depth;
}

Fixed marginRatio(const GameState& st) {
    const MarketState& m = st.market;
    Fixed blocked = Fixed(0);
    for (const auto& p : m.futuresPositions) if (p.owner == kPlayerId) blocked += p.margin;
    for (const auto& p : m.positions) {
        Fixed spot = m.exchanges[kExchCX].books[static_cast<std::size_t>(p.res)].mid;
        blocked += Fixed::raw(mulDivSat(spot.rawValue(), std::abs(p.qty), 1)) * Fixed::pct(20);
    }
    if (blocked.rawValue() <= 0) return Fixed(9);
    return Fixed::raw(mulDivSat(m.margin.equity.rawValue(), FIX, blocked.rawValue()));
}

}  // namespace gf
