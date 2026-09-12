#include "mkt/BlackMarket.h"

#include <algorithm>

#include "mkt/OrderBook.h"

namespace gf {

void blackMarketUpdate(GameState& st) {
    MarketState& m = st.market;
    ExchangeMarket& bz = m.exchanges[kExchBZ];

    // 结构性溢价：由配给强度、封锁与关税决定（走私风险成本）
    Fixed structural = Fixed::pct(30) + m.rationing * Fixed::pct(60) + bz.embargo * Fixed::pct(40);
    for (int e = 0; e < kExchangeCount; ++e) {
        if (e == kExchBZ) continue;
        structural += m.exchanges[static_cast<std::size_t>(e)].tariff * Fixed::pct(30);
    }
    // 结构值下限必须**低于套利的盈亏平衡点**（约 3.5% = 黑市 2% 手续费
    // + 白市 0.2% + 两侧价差）。下限原为 10%，于是溢价每 tick 都从 2% 回升到
    // 有利可图的水平，套利窗口反复重开、可无限重复提取
    //（实测每轮稳定 +0.65% 权益）。黑市溢价代表风险补偿，
    // 在没有建模查没风险的前提下，它不应高于交易成本。
    m.blackMarketStructural = fxClamp(structural, Fixed::pct(2), Fixed::pct(220));

    // 当前溢价向结构值缓慢回归：这是套利窗口"重新打开"的唯一来源。
    // 回归很慢（每季 8%），因此套利一次之后窗口不会立刻恢复。
    m.blackMarketPremium =
        fxLerp(m.blackMarketPremium, m.blackMarketStructural, Fixed::pct(8));

    for (int c = 0; c < kCommodityCount; ++c) {
        Book& b = bz.books[static_cast<std::size_t>(c)];
        const CommodityInfo& ci = commodityInfo(c);
        Book& white = m.exchanges[kExchCX].books[static_cast<std::size_t>(c)];
        Fixed whiteMid = white.mid.rawValue() > 0 ? white.mid : ci.basePrice;

        // ---- 库存驱动的溢价压缩 ----
        // 黑市做市商净买入（有人往黑市抛货）时库存变多，它必须降价出货；
        // 净卖出（有人从黑市扫货）时库存变少，它抬价补货。
        MarketMakerState& mm = bz.mm[static_cast<std::size_t>(c)];
        i64 limit = mm.inventoryLimit > 0 ? mm.inventoryLimit : 1;
        // 无量纲比值：库存 / 上限
        Fixed invRatio = mm.inventory / Fixed(limit);
        invRatio = fxClamp(invRatio, Fixed(-2), Fixed(2));
        Fixed pressure = fxClamp(m.arbPressure[static_cast<std::size_t>(c)], Fixed(-1), Fixed(1));
        // 库存每偏离上限 1 倍 → 溢价压缩 8%；套利压力再额外压缩
        Fixed compress = invRatio * Fixed::pct(8) + pressure * Fixed::pct(12);
        m.blackMarketPremium = fxClamp(m.blackMarketPremium - compress, Fixed::pct(2), Fixed::pct(300));

        // 套利压力随时间衰减
        m.arbPressure[static_cast<std::size_t>(c)] = m.arbPressure[static_cast<std::size_t>(c)] * Fixed::pct(70);

        // 现价 = 白市价 × (1 + 当前溢价)；不再直接搬动簿内报价，
        // 做市商会以该锚点重新报价，价格因此是"被交易出来的"。
        Fixed target = whiteMid * (Fixed(1) + m.blackMarketPremium);
        m.blackMarketPrice[static_cast<std::size_t>(c)] = target;
        if (b.mid.rawValue() <= 0) b.mid = target;
        if (b.last.rawValue() <= 0) b.last = target;

        // 库存越限时做市商单边撤单（由 marketMakerRequote 执行），
        // 这里只标记流动性状态，供 UI 与 AI 参考。
        b.liquidityDrained = (b.bids.empty() || b.asks.empty());
    }
}

Fixed blackMarketPremiumOf(const GameState& st, u8 res) {
    Fixed white = st.market.exchanges[kExchCX].books[res].mid;
    Fixed black = st.market.blackMarketPrice[res];
    if (white.rawValue() <= 0) return Fixed(0);
    return Fixed::raw(mulDivSat(black.rawValue() - white.rawValue(), FIX, white.rawValue()));
}

bool blackMarketTradable(const GameState& st, u8 res) {
    (void)st;
    return res < kCommodityCount;
}

Fixed blackMarketPriceOf(const GameState& st, u8 res) {
    Fixed v = st.market.blackMarketPrice[res];
    if (v.rawValue() > 0) return v;
    return commodityInfo(static_cast<int>(res)).basePrice;
}

Fixed blackMarketRaidRisk(const GameState& st, u32 actor) {
    // 监管强度 + 该主体在黑市的敞口
    Fixed base = Fixed::pct(3) + Fixed::raw(5) * exchangeInfo(kExchBZ).regulator / Fixed(1000);
    Fixed exposure = Fixed(0);
    for (const auto& p : st.market.positions)
        if (p.qty != 0) exposure += Fixed::pct(1);
    if (actor < st.empires.size()) {
        base += st.empires[actor].propaganda * Fixed::pct(2);
    }
    return fxClamp(base + exposure, Fixed(0), Fixed::pct(60));
}

}  // namespace gf
