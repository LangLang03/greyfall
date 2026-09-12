// 市场：撮合单调性与无自成交 / 冲击定律 / 保证金级联收敛 / 期限结构与基差 /
//       套利偏差 / 波动率聚集 / 操纵检测 / 内幕 / 违约与托管
#include <algorithm>

#include "check.h"
#include "core/GameState.h"
#include "core/TickPipeline.h"
#include "gen/WorldGen.h"
#include "mkt/Arbitrage.h"
#include "mkt/BlackMarket.h"
#include "mkt/Debt.h"
#include "mkt/Futures.h"
#include "mkt/FX.h"
#include "mkt/Insider.h"
#include "ai/FraudDetect.h"
#include "mkt/ManipulationDetect.h"
#include "mkt/Margin.h"
#include "mkt/MarketEngine.h"
#include "mkt/Matching.h"
#include "mkt/OrderBook.h"
#include "mkt/Settlement.h"
#include "mkt/VolModel.h"

using namespace gf;

namespace {

GameState marketWorld(u64 seed = 99) {
    WorldGenOptions o;
    o.seed = seed;
    o.difficulty = 2;
    o.empireCount = 8;
    o.systemCount = 32;
    GameState st;
    generateWorld(st, o);
    return st;
}

Order mkOrder(u64 id, u32 owner, bool buy, i64 px, i64 qty) {
    Order o;
    o.id = id;
    o.seq = static_cast<u32>(id);
    o.owner = owner;
    o.buy = buy;
    o.px = Fixed::raw(px);
    o.qty = qty;
    o.shown = qty;
    return o;
}

}  // namespace

TEST(market, orderbook_levels_and_best_prices) {
    Book b;
    b.orders.push_back(mkOrder(1, 1, true, 9900, 100));
    b.orders.push_back(mkOrder(2, 1, true, 9900, 50));
    b.orders.push_back(mkOrder(3, 1, true, 9800, 200));
    b.orders.push_back(mkOrder(4, 1, false, 10100, 300));
    b.orders.push_back(mkOrder(5, 1, false, 10200, 400));
    bookRebuildLevels(b);
    CHECK_EQ(b.bids.size(), 2ull);
    CHECK_EQ(b.asks.size(), 2ull);
    CHECK_EQ(b.bids[0].px.rawValue(), 9900);
    CHECK_EQ(b.bids[0].qty, 150);          // 同价合并
    CHECK_EQ(b.bids[0].nOrders, 2u);
    CHECK_EQ(b.bids[1].px.rawValue(), 9800);
    CHECK_EQ(b.asks[0].px.rawValue(), 10100);
    CHECK_EQ(bookBestBid(b)->px.rawValue(), 9900);
    CHECK_EQ(bookBestAsk(b)->px.rawValue(), 10100);
    CHECK_EQ(bookMid(b).rawValue(), 10000);
    CHECK_EQ(bookSpread(b).rawValue(), 20);   // (10100-9900)/10000 = 2%
    CHECK_EQ(bookDepthQty(b, true, 1), 150);
    CHECK_EQ(bookDepthQty(b, true, 2), 350);
    CHECK_EQ(bookActiveQty(b, false), 700);
}

TEST(market, matching_price_time_priority_no_self_trade) {
    Book b;
    b.orders.push_back(mkOrder(1, 1, false, 10000, 100));
    b.orders.push_back(mkOrder(2, 2, false, 10000, 100));   // 同价更晚 ⇒ 时间优先
    b.orders.push_back(mkOrder(3, 1, false, 10100, 100));
    bookRebuildLevels(b);

    MkOrder in;
    in.id = 100;
    in.owner = 7;
    in.buy = true;
    in.px = Fixed::raw(10050);
    in.qty = 150;
    MkResult r = matchOrder(b, in, Fixed(0));
    CHECK_EQ(r.filled, 150);
    CHECK_EQ(r.consumed.size(), 2ull);          // 吃掉档 1 全部 + 档 2 的 50
    CHECK_EQ(r.consumed[0].passiveId, 1ull);    // 价格-时间优先：seq 小的先成交
    CHECK_EQ(r.consumed[1].passiveId, 2ull);
    CHECK_EQ(r.avgPx.rawValue(), 10000);
    CHECK_EQ(r.remaining, 0);

    // 无自成交：同一 owner 的对手单必须被跳过
    Book b2;
    b2.orders.push_back(mkOrder(10, 5, false, 10000, 500));
    bookRebuildLevels(b2);
    MkOrder self;
    self.id = 200;
    self.owner = 5;
    self.buy = true;
    self.px = Fixed::raw(10000);
    self.qty = 100;
    MkResult r2 = matchOrder(b2, self, Fixed(0));
    CHECK_EQ(r2.filled, 0);
    CHECK_EQ(r2.consumed.size(), 0ull);
}

TEST(market, matching_limit_price_respected) {
    Book b;
    b.orders.push_back(mkOrder(1, 1, false, 10500, 100));
    b.orders.push_back(mkOrder(2, 1, false, 10000, 100));
    bookRebuildLevels(b);
    MkOrder in;
    in.id = 300;
    in.owner = 9;
    in.buy = true;
    in.px = Fixed::raw(10100);   // 只能吃到 10000 那一档
    in.qty = 200;
    MkResult r = matchOrder(b, in, Fixed(0));
    CHECK_EQ(r.filled, 100);
    CHECK_EQ(r.remaining, 100);
    CHECK_EQ(r.avgPx.rawValue(), 10000);
}

TEST(market, impact_law_monotone_in_quantity) {
    Fixed sigma = Fixed::pct(3);
    Fixed v20 = Fixed(1000);
    Fixed i1 = impactTemporary(sigma, 100, v20);
    Fixed i2 = impactTemporary(sigma, 400, v20);
    Fixed i3 = impactTemporary(sigma, 1600, v20);
    CHECK(i1.rawValue() > 0);
    CHECK(i2.rawValue() > i1.rawValue());
    CHECK(i3.rawValue() > i2.rawValue());
    // 平方根定律：Q 变为 4 倍 ⇒ 冲击约变为 2 倍
    CHECK(i2.rawValue() >= i1.rawValue() * 17 / 10);
    CHECK(i2.rawValue() <= i1.rawValue() * 23 / 10);
    CHECK(i3.rawValue() >= i2.rawValue() * 17 / 10);
    // 零量无冲击；永久冲击是临时的 κ 倍
    CHECK_EQ(impactTemporary(sigma, 0, v20).rawValue(), 0);
    CHECK_EQ(impactPermanent(i2).rawValue(), i2.rawValue() * 350 / 1000);
    // 半衰回归
    CHECK_EQ(impactDecay(i2).rawValue(), i2.rawValue() / 2);
}

TEST(market, volatility_clustering) {
    VolState v;
    v.lastSigma = Fixed::pct(2);
    Fixed low = volSigma(v);
    // 一次大冲击之后波动率必须上升
    volInjectJump(v, Fixed::pct(10));
    Fixed high = volSigma(v);
    CHECK(high.rawValue() > low.rawValue());
    // 大收益之后的 GARCH 更新也会抬高 σ
    VolState v2;
    v2.lastSigma = Fixed::pct(2);
    volUpdate(v2, Fixed::pct(15));
    CHECK(v2.lastSigma.rawValue() > Fixed::pct(2).rawValue());
    // 无事件时跳跃偏差衰减
    VolState v3 = v;
    for (int i = 0; i < 10; ++i) volUpdate(v3, Fixed(0));
    CHECK(v3.jumpBias.rawValue() < v.jumpBias.rawValue());
}

TEST(market, futures_basis_and_structure) {
    GameState st = marketWorld();
    futuresUpdateAll(st);
    const ExchangeMarket& x = st.market.exchanges[kExchCX];
    const auto& row = x.futures[static_cast<std::size_t>(Commodity::Alloys)];
    for (int t = 0; t < kFuturesTerms; ++t) CHECK(row[static_cast<std::size_t>(t)].price.rawValue() > 0);
    // 期限越远，持有成本越高 ⇒ contango 时价格递增
    CHECK(row[3].carry.rawValue() > row[0].carry.rawValue());
    std::string_view structure = futuresStructureName(st, static_cast<u8>(Commodity::Alloys));
    CHECK(!structure.empty());
    // 长期现货升水 ⇒ 便利收益非负
    CHECK(row[0].convenience.rawValue() >= 0);
}

TEST(market, margin_call_cascade_converges) {
    GameState st = marketWorld(7);
    MarketState& m = st.market;
    // 制造一个高杠杆的巨额头寸，并让价格逆向跳变
    FuturesPosition p;
    p.res = static_cast<u8>(Commodity::Alloys);
    p.term = 0;
    p.qty = 500000;
    p.entry = m.exchanges[kExchCX].books[static_cast<std::size_t>(Commodity::Alloys)].mid;
    p.margin = Fixed(1000);
    p.leverage = 10;
    m.futuresPositions.push_back(p);
    m.margin.cash = Fixed(1000);
    marketApplyShock(st, static_cast<u8>(Commodity::Alloys), Fixed::pct(-30), "测试：级联压力");
    TickReport rep;
    int depth = marginCascade(st, rep);
    CHECK(depth >= 0);
    CHECK(depth <= 8);   // 必须收敛，不能无限循环
    CHECK_EQ(m.margin.cascadeDepth, depth);
}

TEST(market, arbitrage_net_of_costs) {
    GameState st = marketWorld(11);
    // 人为在 FX 上把卖价抬高，制造一个明确的套利机会
    Book& fx = st.market.exchanges[kExchFX].books[static_cast<std::size_t>(Commodity::Alloys)];
    for (auto& o : fx.orders) {
        if (!o.buy) o.px = Fixed::raw(o.px.rawValue() + 8000);   // 抬价 8
    }
    bookRebuildLevels(fx);
    ArbOpportunity a = bestArbitrage(st, static_cast<u8>(Commodity::Alloys));
    CHECK(a.grossGap.rawValue() > 0);
    // 净价差必须小于等于毛价差（要扣手续费/运费/关税/封锁）
    CHECK(a.netGap.rawValue() <= a.grossGap.rawValue());
    CHECK(a.buyExch != a.sellExch);
}

TEST(market, fx_and_blackmarket_react_to_rationing) {
    GameState st = marketWorld(13);
    // 结构性溢价必须立即响应配给强度
    Fixed structBefore = st.market.blackMarketStructural;
    st.market.rationing = Fixed::pct(80);
    blackMarketUpdate(st);
    CHECK(st.market.blackMarketStructural.rawValue() > structBefore.rawValue());
    // 当前溢价是状态量，会向结构值收敛（需要若干季），而不是瞬间跳变
    Fixed premBefore = st.market.blackMarketPremium;
    for (int i = 0; i < 40; ++i) blackMarketUpdate(st);
    CHECK(st.market.blackMarketPremium.rawValue() > premBefore.rawValue());
    CHECK(st.market.blackMarketPremium.rawValue() <= st.market.blackMarketStructural.rawValue() + 1);
    fxUpdate(st);
    for (int e = 0; e < kExchangeCount; ++e) CHECK(st.market.fx[static_cast<std::size_t>(e)].rawValue() > 0);
    CHECK(fxRate(st, kExchCX, kExchFX).rawValue() > 0);
}

// 回归守卫：无限套利。此前黑市溢价被硬锚定在白市价上、做市商库存从不更新、
// 且下单没有购买力校验，导致「CX 买 → BZ 卖」可以无限刷钱。
TEST(market, arbitrage_is_self_limiting) {
    GameState st = marketWorld(777);
    const int c = static_cast<int>(Commodity::Volatiles);
    Fixed startCash = st.market.margin.cash;
    Fixed peak = startCash;
    int profitableRounds = 0;
    std::vector<Fixed> cashAfter;

    // 只衡量**同一 tick 内**的套利盈亏：不能跨 tick 比较现金总额，
    // 否则会把正常的税收收入算成套利收益（实测不交易也会每 tick +940）。
    std::vector<Fixed> arbProfit;
    for (int round = 0; round < 12; ++round) {
        Fixed before = st.market.margin.cash;
        // 用一半现金在 CX 市价买入
        Fixed mid = st.market.exchanges[kExchCX].books[static_cast<std::size_t>(c)].mid;
        if (mid.rawValue() <= 0) break;
        // before.rawValue() 是定点原始值；除以价格原始值即得到单位数量
        // （不要再乘 1000 —— 那会放大三个数量级，被购买力校验直接拒绝）
        i64 qty = before.rawValue() / 2 / mid.rawValue();
        if (qty < 100) break;
        OrderRequest buy;
        buy.owner = kPlayerId;
        buy.res = static_cast<u8>(c);
        buy.exch = kExchCX;
        buy.buy = true;
        buy.qty = qty;
        buy.kind = OrderKind::Market;
        OrderAck a = marketSubmitOrder(st, buy);
        if (a.filled <= 0) {
            // 买入失败（例如购买力不足）是合法结果，但不应长期无法交易
            advanceOneTick(st);
            continue;
        }
        OrderRequest sell;
        sell.owner = kPlayerId;
        sell.res = static_cast<u8>(c);
        sell.exch = kExchBZ;
        sell.buy = false;
        sell.qty = a.filled;
        sell.kind = OrderKind::Market;
        (void)marketSubmitOrder(st, sell);
        Fixed after = st.market.margin.cash;
        arbProfit.push_back(after - before);
        if (after.rawValue() > before.rawValue()) ++profitableRounds;
        if (after.rawValue() > peak.rawValue()) peak = after;
        cashAfter.push_back(after);
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);
    }

    // 1) 套利不可能无限盈利：获利轮次必须有限
    CHECK(profitableRounds <= 4);
    CHECK(arbProfit.size() >= 4);
    // 2) 正确匹配最高买价后，残余优价挂单仍可能带来少量利润；
    //    窗口必须收敛，最后三次交易不能继续稳定抽取收益。
    if (arbProfit.size() >= 3)
        for (std::size_t i = arbProfit.size() - 3; i < arbProfit.size(); ++i)
            CHECK(arbProfit[i].rawValue() <= 0);
    // 3) 首轮收益应当有限（不能把本金翻几倍）
    CHECK(arbProfit[0].rawValue() < startCash.rawValue());
    // 4) 套利总收益必须有界。
    //    注意不能用「最终现金 ≤ 峰值」来衡量 —— 帝国税收每 tick 都在进账
    //    （实测不交易也 +940/tick），现金总额必然单调上升。
    Fixed totalArb = Fixed(0);
    for (const auto& p : arbProfit) totalArb += p;
    CHECK(totalArb.rawValue() < startCash.rawValue() / 2);
    // 3) 全程市场必须保持双边可交易，不能被打成一潭死水
    for (int e = 0; e < kExchangeCount; ++e) {
        const Book& b = st.market.exchanges[static_cast<std::size_t>(e)].books[static_cast<std::size_t>(c)];
        CHECK(!b.bids.empty());
        CHECK(!b.asks.empty());
    }
    // 4) 账本不允许交叉（买一 ≥ 卖一）
    for (int e = 0; e < kExchangeCount; ++e) {
        const Book& b = st.market.exchanges[static_cast<std::size_t>(e)].books[static_cast<std::size_t>(c)];
        if (!b.bids.empty() && !b.asks.empty())
            CHECK(b.bids.front().px.rawValue() < b.asks.front().px.rawValue());
    }
}

// 购买力与持仓约束：没有钱不能买，没有货不能卖
TEST(market, buying_power_and_stock_are_enforced) {
    GameState st = marketWorld(31);
    st.market.margin.cash = Fixed(1000);   // 只有 1000 cr
    OrderRequest buy;
    buy.owner = kPlayerId;
    buy.res = static_cast<u8>(Commodity::Alloys);
    buy.exch = kExchCX;
    buy.buy = true;
    buy.qty = 100000;                        // 远超购买力
    buy.kind = OrderKind::Market;
    OrderAck a = marketSubmitOrder(st, buy);
    CHECK(!a.accepted);
    CHECK(a.filled == 0);
    CHECK(a.reason.find("购买力不足") != std::string::npos);

    // 卖出超过持仓
    OrderRequest sell;
    sell.owner = kPlayerId;
    sell.res = static_cast<u8>(Commodity::Relics);
    sell.exch = kExchCX;
    sell.buy = false;
    sell.qty = 999999;
    sell.kind = OrderKind::Limit;
    sell.px = st.market.exchanges[kExchCX].books[static_cast<std::size_t>(Commodity::Relics)].mid;
    OrderAck b = marketSubmitOrder(st, sell);
    CHECK(!b.accepted);
    CHECK(b.reason.find("持仓不足") != std::string::npos);
}

TEST(market, manipulation_detection_responds_to_wash_trading) {
    GameState st = marketWorld(17);
    // 一个主体双向大量挂单并撤单 ⇒ wash + spoof 特征
    for (int i = 0; i < 12; ++i) {
        manipRecordPlace(st, 3, true, 1000);
        manipRecordPlace(st, 3, false, 1000);
    }
    for (int i = 0; i < 40; ++i) manipRecordCancel(st, 3, 500);
    manipRecordFill(st, 3, true, 10);
    std::vector<ManipFinding> findings = manipulationDetect(st);
    CHECK(!findings.empty());
    bool wash = false, spoof = false;
    for (const auto& f : findings) {
        if (f.actor != 3) continue;
        if (f.kind == ManipKind::WashTrading) wash = true;
        if (f.kind == ManipKind::Spoofing) spoof = true;
    }
    CHECK(wash || spoof);
    Fixed overall = Fixed(0);
    for (const auto& f : findings) overall += f.score;
    CHECK(overall.rawValue() > 0);
    CHECK(overall.rawValue() > 0);
    // 监管裁决必须留下记录
    manipulationEnforce(st, findings);
    CHECK(!st.market.manipulations.empty());
}

TEST(market, insider_signals_emitted_and_consumed) {
    GameState st = marketWorld(19);
    for (int i = 0; i < 12; ++i) {
        insiderEmitSignals(st);
        insiderConsume(st);
    }
    CHECK(st.market.insiderSignals.size() <= 256);
}

TEST(market, debt_credit_and_escrow) {
    GameState st = marketWorld(23);
    st.empires[kPlayerId].treasury = Fixed(200000);
    st.market.margin.cash = Fixed(200000);
    std::string err;
    Fixed before = st.market.margin.cash;
    CHECK(borrowCredits(st, kPlayerId, Fixed(50000), 8, &err));
    CHECK_EQ(st.market.margin.cash.rawValue(), before.rawValue() + Fixed(50000).rawValue());
    CHECK(totalDebt(st, kPlayerId).rawValue() > 0);
    // 超额借款必须被信用额度拒绝
    CHECK(!borrowCredits(st, kPlayerId, Fixed(100000000), 8, &err));
    CHECK(!err.empty());
    // 还款
    CHECK(repayCredits(st, kPlayerId, Fixed(20000), &err));
    CHECK(totalDebt(st, kPlayerId).rawValue() < Fixed(50000).rawValue());
    // 托管：占用资金，释放后回到收款方
    Fixed payeeBefore = st.empires[1].treasury;
    CHECK(escrowOpen(st, kPlayerId, 1, Fixed(10000), 0, 0, 4, &err));
    CHECK(!st.market.escrows.empty());
    u32 id = st.market.escrows.front().id;
    CHECK(escrowRelease(st, id, true, &err));
    CHECK_EQ(st.empires[1].treasury.rawValue(), payeeBefore.rawValue() + Fixed(10000).rawValue());
    creditUpdate(st);
    for (const auto& e : st.empires) CHECK(e.creditRating.rawValue() >= 0);
}

TEST(market, ai_flow_never_trades_as_player) {
    GameState st = marketWorld(29);
    for (int i = 0; i < 6; ++i) advanceOneTick(st);
    // AI 的聚合订单流不能以玩家名义挂单（否则等于替玩家建仓）
    std::size_t playerOwned = 0;
    for (const auto& x : st.market.exchanges)
        for (const auto& b : x.books)
            for (const auto& o : b.orders)
                if (o.owner == kPlayerId) ++playerOwned;
    CHECK_EQ(playerOwned, 0ull);
    // 玩家持仓必须为空（没有显式下单）
    for (const auto& p : st.market.positions) CHECK_EQ(p.qty, 0);
}

// 回归守卫：持有成本曾多除以 1000（carry 本身已是分数），
// 使 F1~F4 恒等于现货价、基差恒为 0 —— 期限结构形同虚设，
// 跨期套利与展期策略无从谈起。
TEST(market, futures_term_structure_reflects_carry) {
    GameState st = marketWorld(9901);
    for (int i = 0; i < 10; ++i) {
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);
    }
    futuresUpdateAll(st);
    const ExchangeMarket& x = st.market.exchanges[kExchCX];
    int c = static_cast<int>(Commodity::Alloys);
    Fixed spot = x.books[static_cast<std::size_t>(c)].mid;
    CHECK(spot.rawValue() > 0);

    // 近月与远月必须不同：持有成本确实计入了价格
    Fixed f1 = x.futures[static_cast<std::size_t>(c)][0].price;
    Fixed f4 = x.futures[static_cast<std::size_t>(c)][3].price;
    CHECK(f1.rawValue() > 0);
    CHECK(f4.rawValue() > 0);
    CHECK(f1.rawValue() != f4.rawValue());

    // 正常情况下 carry > convenience ⇒ contango：远月更贵
    Fixed carry1 = x.futures[static_cast<std::size_t>(c)][0].carry;
    Fixed conv1 = x.futures[static_cast<std::size_t>(c)][0].convenience;
    if (carry1.rawValue() > conv1.rawValue()) {
        CHECK(f4.rawValue() > f1.rawValue());
        CHECK(f1.rawValue() > spot.rawValue());
    }
    // 基差必须随期限单调（线性持有成本的直接结果）
    Fixed b1 = x.futures[static_cast<std::size_t>(c)][0].basis;
    Fixed b4 = x.futures[static_cast<std::size_t>(c)][3].basis;
    CHECK(b4.rawValue() > b1.rawValue());
}

TEST(market, convenience_yield_rises_when_scarce) {
    // 库存抽干 ⇒ 便利收益上升（这是期限结构可能转向 backwardation 的机制）
    GameState st = marketWorld(9902);
    for (int i = 0; i < 8; ++i) {
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);
    }
    int c = static_cast<int>(Commodity::Alloys);
    Fixed before = convenienceYield(st, static_cast<u8>(c));
    // 抽干全世界该商品库存
    for (auto& e : st.empires) e.stock[static_cast<std::size_t>(c)] = Fixed(0);
    Fixed after = convenienceYield(st, static_cast<u8>(c));
    CHECK(after.rawValue() >= before.rawValue());
    CHECK(after.rawValue() > 0);
    // 便利收益必须有上限（避免曲线被推到负数）
    CHECK(after.rawValue() <= Fixed::pct(2).rawValue());
}

TEST(market, book_pruning_bounds_memory) {
    GameState st = marketWorld(31);
    for (int i = 0; i < 12; ++i) advanceOneTick(st);
    std::size_t maxOrders = 0;
    for (const auto& x : st.market.exchanges)
        for (const auto& b : x.books) maxOrders = std::max(maxOrders, b.orders.size());
    CHECK(maxOrders <= 256);
}
