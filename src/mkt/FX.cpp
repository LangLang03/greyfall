#include "mkt/FX.h"

#include <algorithm>

#include "mkt/Arbitrage.h"

namespace gf {

void fxUpdate(GameState& st) {
    MarketState& m = st.market;
    // 结算币价的驱动：交易所活跃度（成交额）与封锁程度
    for (int e = 0; e < kExchangeCount; ++e) {
        ExchangeMarket& x = m.exchanges[static_cast<std::size_t>(e)];
        Fixed activity = x.volumeTick;
        Fixed base = e == kExchCX ? Fixed(1) : (e == kExchFX ? Fixed::raw(940) : Fixed::raw(1350));
        // 活跃度越高，币价越坚挺
        Fixed drift = Fixed::raw(mulDivSat(activity.rawValue(), 3, 1000000));
        drift = fxClamp(drift, Fixed::bp(-500), Fixed::bp(500));
        Fixed target = base * (Fixed(1) + drift / Fixed(1000));
        target = target * (Fixed(1) - x.embargo * Fixed::pct(10));
        m.fx[static_cast<std::size_t>(e)] = fxLerp(m.fx[static_cast<std::size_t>(e)], target, Fixed::pct(25));
        if (m.fx[static_cast<std::size_t>(e)].rawValue() <= 0) m.fx[static_cast<std::size_t>(e)] = base;
    }
    // 配给强度：战争与封锁越久越高
    Fixed warPressure = Fixed(0);
    for (const auto& r : st.relations)
        if (r.atWar) warPressure += Fixed::pct(5);
    warPressure = fxClamp(warPressure, Fixed(0), Fixed::pct(80));
    m.rationing = fxLerp(m.rationing, warPressure, Fixed::pct(20));
}

Fixed fxRate(const GameState& st, int fromExch, int toExch) {
    if (fromExch < 0 || fromExch >= kExchangeCount || toExch < 0 || toExch >= kExchangeCount) return Fixed(1);
    Fixed f = st.market.fx[static_cast<std::size_t>(fromExch)];
    Fixed t = st.market.fx[static_cast<std::size_t>(toExch)];
    if (f.rawValue() <= 0) return Fixed(1);
    return Fixed::raw(mulDivSat(t.rawValue(), FIX, f.rawValue()));
}

Fixed rationingDampen(const GameState& st) {
    return Fixed(1) - st.market.rationing / Fixed(2);
}

}  // namespace gf
