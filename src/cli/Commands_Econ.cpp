#include <algorithm>
#include <string>

#include "cli/Commands.h"
#include "util/TextTable.h"
#include "core/Errors.h"
#include "core/TickPipeline.h"
#include "mkt/Arbitrage.h"
#include "mkt/BlackMarket.h"
#include "mkt/Debt.h"
#include "mkt/FX.h"
#include "mkt/Futures.h"
#include "mkt/Margin.h"
#include "mkt/MarketEngine.h"
#include "mkt/ManipulationDetect.h"
#include "mkt/OrderBook.h"
#include "mkt/Settlement.h"
#include "mkt/VolModel.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {
namespace {

int requireRes(const Args& args, std::size_t posIndex, const char* usage) {
    // 同时支持 `greyfall quote alloys` 与 `greyfall market --res alloys`
    std::string key = args.has("res") ? args.get("res") : (args.posCount() > posIndex ? args.pos(posIndex) : "");
    if (key.empty()) fail(ExitCode::BadArgs, std::string("用法：") + usage);
    int idx = commodityIndexByName(key);
    if (idx < 0) {
        fail(ExitCode::BadArgs, "未知资源【" + args.pos(posIndex) +
                                    "】。可用：energy minerals food alloys components medicines supermaterials "
                                    "exotic datacrystals influence unity luxury volatiles raregases polymers "
                                    "electronics robotics bioproducts antimatter relics contraband");
    }
    return idx;
}

int requireExch(std::string_view name) {
    int idx = exchangeIndexByName(name);
    if (idx < 0) fail(ExitCode::BadArgs, "未知交易所【" + std::string(name) + "】。可用：CX / FX / BZ");
    return idx;
}

}  // namespace

int cmdBook(CliEnv& env, const Args& args) {
    env.loadState();
    if (args.posCount() < 2) fail(ExitCode::BadArgs, "用法：greyfall book <exch> <res> [--depth 8]");
    int exch = requireExch(args.pos(0));
    int res = commodityIndexByName(args.pos(1));
    if (res < 0) fail(ExitCode::BadArgs, "未知资源【" + args.pos(1) + "】");
    i64 depth = args.getInt("depth", 8);
    const Book& b = env.st.market.exchanges[static_cast<std::size_t>(exch)].books[static_cast<std::size_t>(res)];

    out(style("═══ 订单簿 " + std::string(exchangeInfo(exch).nameZh) + " · " +
                  std::string(commodityInfo(res).nameZh) + " ═══",
              Style::Heading));
    TextTable t;
    t.header({"档", "买价", "买量", "订单数", "|", "卖价", "卖量", "订单数"},
             {Align::Right, Align::Right, Align::Right, Align::Right, Align::Center, Align::Right, Align::Right,
              Align::Right});
    std::size_t n = std::max(b.bids.size(), b.asks.size());
    if (static_cast<i64>(n) > depth) n = static_cast<std::size_t>(depth);
    for (std::size_t i = 0; i < n; ++i) {
        std::string bp = i < b.bids.size() ? fixedStrPlain(b.bids[i].px, 2) : "—";
        std::string bq = i < b.bids.size() ? groupDigits(b.bids[i].qty) : "—";
        std::string bn = i < b.bids.size() ? std::to_string(b.bids[i].nOrders) : "—";
        std::string ap = i < b.asks.size() ? fixedStrPlain(b.asks[i].px, 2) : "—";
        std::string aq = i < b.asks.size() ? groupDigits(b.asks[i].qty) : "—";
        std::string an = i < b.asks.size() ? std::to_string(b.asks[i].nOrders) : "—";
        t.row({std::to_string(i + 1), bp, bq, bn, "|", ap, aq, an});
    }
    out(t.render());
    out("");
    TextTable s;
    s.header({"项目", "值"});
    s.row({"最新成交", fixedStrPlain(b.last, 2)});
    s.row({"中间价", fixedStrPlain(b.mid, 2)});
    s.row({"价差", fixedStrPlain(b.spread * Fixed(100), 3) + "%"});
    s.row({"本 tick 成交", groupDigits(b.volume)});
    s.row({"VWAP", fixedStrPlain(b.vwap, 2)});
    s.row({"σ（GARCH）", fixedStrPlain(b.sigma, 4)});
    s.row({"V20", groupDigits(b.var20.rawValue() / FIX)});
    s.row({"累计冲击（临时/永久）", fixedStrPlain(b.impactTemp, 4) + " / " + fixedStrPlain(b.impactPerm, 4)});
    s.row({"活动订单", std::to_string(b.orders.size()) + " 笔"});
    out(s.render());
    return 0;
}

int cmdQuote(CliEnv& env, const Args& args) {
    env.loadState();
    int res = requireRes(args, 0, "greyfall quote <res>");
    TextTable t;
    t.header({"交易所", "买一", "买量", "卖一", "卖量", "中间价", "价差", "最新"},
             {Align::Left, Align::Right, Align::Right, Align::Right, Align::Right, Align::Right, Align::Right,
              Align::Right});
    for (int e = 0; e < kExchangeCount; ++e) {
        const Book& b = env.st.market.exchanges[static_cast<std::size_t>(e)].books[static_cast<std::size_t>(res)];
        const PriceLevel* bid = bookBestBid(b);
        const PriceLevel* ask = bookBestAsk(b);
        t.row({std::string(exchangeInfo(e).nameZh), bid ? fixedStrPlain(bid->px, 2) : "—",
               bid ? groupDigits(bid->qty) : "—", ask ? fixedStrPlain(ask->px, 2) : "—",
               ask ? groupDigits(ask->qty) : "—", fixedStrPlain(b.mid, 2),
               fixedStrPlain(bookSpread(b) * Fixed(100), 3) + "%", fixedStrPlain(b.last, 2)});
    }
    out(t.render());
    out("综合现货指数：" + fixedStrPlain(env.st.market.spotIndex[static_cast<std::size_t>(res)], 2));
    return 0;
}

int cmdCurve(CliEnv& env, const Args& args) {
    env.loadState();
    int res = requireRes(args, 0, "greyfall curve <res>");
    out(style("═══ 期限结构 · " + std::string(commodityInfo(res).nameZh) + " ═══", Style::Heading));
    TextTable t;
    t.header({"期限", "价格", "基差", "持有成本", "便利收益", "持仓量"},
             {Align::Left, Align::Right, Align::Right, Align::Right, Align::Right, Align::Right});
    const ExchangeMarket& x = env.st.market.exchanges[kExchCX];
    for (int k = 0; k < kFuturesTerms; ++k) {
        const FuturesQuote& f = x.futures[static_cast<std::size_t>(res)][static_cast<std::size_t>(k)];
        t.row({"F" + std::to_string(k + 1) + "（" + std::to_string(k + 1) + " 季）", fixedStrPlain(f.price, 2),
               fixedStrPlain(f.basis * Fixed(100), 3) + "%", fixedStrPlain(f.carry, 3),
               fixedStrPlain(f.convenience, 3), groupDigits(f.openInterest)});
    }
    out(t.render());
    out("");
    out("结构判定：" + std::string(futuresStructureName(env.st, static_cast<u8>(res))));
    out("现货中间价（CX）：" +
        fixedStrPlain(x.books[static_cast<std::size_t>(res)].mid, 2));
    out("低库存 → 正便利收益 → backwardation；封锁预期 → contango。");
    return 0;
}

int cmdMarket(CliEnv& env, const Args& args) {
    env.loadState();
    int res = requireRes(args, 0, "greyfall market --res <res> [--depth 8]");
    i64 depth = args.getInt("depth", 8);
    Args a2 = args;
    a2.setAction("book");
    if (args.has("exch")) {
        Args sub;
        (void)sub;
    }
    out(style("═══ 盘面速览 · " + std::string(commodityInfo(res).nameZh) + " ═══", Style::Heading));

    TextTable t;
    t.header({"交易所", "买一", "卖一", "中间价", "价差", "σ", "V20"},
             {Align::Left, Align::Right, Align::Right, Align::Right, Align::Right, Align::Right, Align::Right});
    for (int e = 0; e < kExchangeCount; ++e) {
        const Book& b = env.st.market.exchanges[static_cast<std::size_t>(e)].books[static_cast<std::size_t>(res)];
        const PriceLevel* bid = bookBestBid(b);
        const PriceLevel* ask = bookBestAsk(b);
        t.row({std::string(exchangeInfo(e).idName), bid ? fixedStrPlain(bid->px, 2) : "—",
               ask ? fixedStrPlain(ask->px, 2) : "—", fixedStrPlain(b.mid, 2),
               fixedStrPlain(bookSpread(b) * Fixed(100), 3) + "%", fixedStrPlain(b.sigma, 4),
               groupDigits(b.var20.rawValue() / FIX)});
    }
    out(t.render());

    // 主所（CX）的深度
    const Book& b = env.st.market.exchanges[kExchCX].books[static_cast<std::size_t>(res)];
    out("");
    out(style("CX 档位（前 " + std::to_string(depth) + " 档）", Style::Sub));
    TextTable d;
    d.header({"档", "买价", "买量", "|", "卖价", "卖量"},
             {Align::Right, Align::Right, Align::Right, Align::Center, Align::Right, Align::Right});
    std::size_t n = std::max(b.bids.size(), b.asks.size());
    if (static_cast<i64>(n) > depth) n = static_cast<std::size_t>(depth);
    for (std::size_t i = 0; i < n; ++i) {
        d.row({std::to_string(i + 1), i < b.bids.size() ? fixedStrPlain(b.bids[i].px, 2) : "—",
               i < b.bids.size() ? groupDigits(b.bids[i].qty) : "—", "|",
               i < b.asks.size() ? fixedStrPlain(b.asks[i].px, 2) : "—",
               i < b.asks.size() ? groupDigits(b.asks[i].qty) : "—"});
    }
    out(d.render());

    // 套利与持仓
    ArbOpportunity arb = bestArbitrage(env.st, static_cast<u8>(res));
    out("");
    TextTable s;
    s.header({"项目", "值"});
    s.row({"跨所净价差", fixedStrPlain(arb.netGap * Fixed(100), 3) + "%（" +
                            std::string(exchangeInfo(arb.buyExch).idName) + " 买 → " +
                            std::string(exchangeInfo(arb.sellExch).idName) + " 卖）"});
    s.row({"期限结构", std::string(futuresStructureName(env.st, static_cast<u8>(res)))});
    s.row({"黑市溢价", fixedStrPlain(blackMarketPremiumOf(env.st, static_cast<u8>(res)) * Fixed(100), 1) + "%"});
    s.row({"我方持仓", [&] {
               for (const auto& p : env.st.market.positions)
                   if (p.res == res) return groupDigits(p.qty) + "（均价 " + fixedStrPlain(p.avgCost, 2) + "）";
               return std::string("0");
           }()});
    s.row({"保证金余量", fixedStrPlain(marginRatio(env.st), 2) + "（< 1 会触发追保与级联强平）"});
    out(s.render());
    return 0;
}

int cmdPosition(CliEnv& env, const Args&) {
    env.loadState();
    const MarketState& m = env.st.market;
    out(style("═══ 持仓 ═══", Style::Heading));
    const bool hasFutures = std::any_of(m.futuresPositions.begin(), m.futuresPositions.end(),
        [](const FuturesPosition& p) { return p.owner == kPlayerId && p.qty > 0; });
    if (m.positions.empty() && !hasFutures) {
        out("（没有持仓）");
    }
    if (!m.positions.empty()) {
        TextTable t;
        t.header({"资源", "净头寸", "均价", "现价", "未实现", "已实现"},
                 {Align::Left, Align::Right, Align::Right, Align::Right, Align::Right, Align::Right});
        for (const auto& p : m.positions) {
            if (p.qty == 0) continue;
            Fixed spot = m.exchanges[kExchCX].books[static_cast<std::size_t>(p.res)].mid;
            Fixed unreal = Fixed::raw(mulDivSat((spot - p.avgCost).rawValue(), p.qty, 1));
            t.row({std::string(commodityName(p.res)), groupDigits(p.qty), fixedStrPlain(p.avgCost, 2),
                   fixedStrPlain(spot, 2), fixedStrSigned(unreal, 0), fixedStrSigned(p.realized, 0)});
        }
        out(t.render());
    }
    if (hasFutures) {
        out("");
        out(style("期货持仓", Style::Sub));
        TextTable t;
        t.header({"资源", "期限", "方向", "数量", "开仓价", "现价", "杠杆", "保证金"},
                 {Align::Left, Align::Left, Align::Left, Align::Right, Align::Right, Align::Right, Align::Right,
                  Align::Right});
        for (const auto& p : m.futuresPositions) {
            if (p.owner != kPlayerId || p.qty == 0) continue;
            const FuturesQuote& q =
                m.exchanges[kExchCX].futures[static_cast<std::size_t>(p.res)][static_cast<std::size_t>(p.term)];
            t.row({std::string(commodityName(p.res)), "F" + std::to_string(p.term + 1), p.isShort ? "空" : "多",
                   groupDigits(p.qty), fixedStrPlain(p.entry, 2), fixedStrPlain(q.price, 2),
                   std::to_string(p.leverage), fixedStr(p.margin, 0)});
        }
        out(t.render());
    }
    out("");
    TextTable s;
    s.header({"账户", "值"});
    s.row({"现金", fixedStr(m.margin.cash, 1)});
    s.row({"权益", fixedStr(m.margin.equity, 1)});
    s.row({"初始保证金率", fixedStrPlain(m.margin.initMargin, 3)});
    s.row({"维持保证金率", fixedStrPlain(m.margin.maintMargin, 3)});
    s.row({"保证金余量", fixedStrPlain(marginRatio(env.st), 2)});
    s.row({"累计强平次数", std::to_string(m.margin.forcedLiquidations)});
    s.row({"上次级联深度", std::to_string(m.margin.cascadeDepth)});
    out(s.render());
    return 0;
}

int cmdPnl(CliEnv& env, const Args& args) {
    env.loadState();
    const MarketState& m = env.st.market;
    Fixed realized = Fixed(0);
    Fixed unreal = Fixed(0);
    for (const auto& p : m.positions) {
        realized += p.realized;
        Fixed spot = m.exchanges[kExchCX].books[static_cast<std::size_t>(p.res)].mid;
        unreal += Fixed::raw(mulDivSat((spot - p.avgCost).rawValue(), p.qty, 1));
    }
    TextTable t;
    t.header({"项目", "值"});
    t.row({"已实现盈亏", fixedStrSigned(realized, 1)});
    t.row({"未实现盈亏", fixedStrSigned(unreal, 1)});
    t.row({"期货保证金占用", fixedStr([&] {
               Fixed v = Fixed(0);
               for (const auto& p : m.futuresPositions) if (p.owner == kPlayerId) v += p.margin;
               return v;
           }(), 1)});
    t.row({"现金", fixedStr(m.margin.cash, 1)});
    t.row({"权益", fixedStr(m.margin.equity, 1)});
    t.row({"国债", fixedStr(totalDebt(env.st, kPlayerId), 1)});
    out(t.render());

    i64 n = args.getInt("ticks", 20);
    if (env.st.history.size() > 1) {
        out("");
        out(style("权益/国力曲线（最近 " + std::to_string(n) + " 季）", Style::Sub));
        std::size_t start = env.st.history.size() > static_cast<std::size_t>(n)
                                ? env.st.history.size() - static_cast<std::size_t>(n)
                                : 0;
        for (std::size_t i = start; i < env.st.history.size(); ++i) {
            const auto& h = env.st.history[i];
            Fixed sc = h.score[kPlayerId];
            out("  t=" + padLeft(std::to_string(h.tick), 4) + "  国力 " + padLeft(fixedStr(sc, 2), 10) +
                "  国库 " + padLeft(fixedStr(h.treasury[kPlayerId], 0), 12) + "  " + bar(sc / Fixed(20), 20));
        }
    }
    return 0;
}

int cmdVol(CliEnv& env, const Args& args) {
    env.loadState();
    int res = requireRes(args, 0, "greyfall vol <res>");
    const VolState& v = env.st.market.vol[static_cast<std::size_t>(res)];
    const CommodityInfo& ci = commodityInfo(res);
    TextTable t;
    t.header({"项目", "值"});
    t.row({"资源", std::string(ci.nameZh)});
    t.row({"基础波动率", fixedStrPlain(ci.volatility, 4)});
    t.row({"当前 σ", fixedStrPlain(volSigma(v), 4)});
    t.row({"方差 σ²", fixedStrPlain(volVariance(v), 6)});
    t.row({"ω / α / β", fixedStrPlain(v.omega, 6) + " / " + fixedStrPlain(v.alpha, 3) + " / " +
                            fixedStrPlain(v.beta, 3)});
    t.row({"上期收益", fixedStrSigned(v.lastReturn * Fixed(100), 3) + "%"});
    t.row({"跳跃偏差", fixedStrPlain(v.jumpBias, 6)});
    out(t.render());
    out("");
    out("GARCH(1,1)：σ²_t = ω + α·r²_{t-1} + β·σ²_{t-1}（α+β = 0.94）—— 波动率聚集，大波动后跟大波动。");
    out("最近冲击：");
    int n = 0;
    for (auto it = env.st.market.shocks.rbegin(); it != env.st.market.shocks.rend() && n < 5; ++it, ++n) {
        if (it->res != res) continue;
        out("  t=" + std::to_string(it->tick) + "  " + it->cause + "  Δ=" +
            fixedStrSigned(it->magnitude * Fixed(100), 2) + "%  σ " + fixedStrPlain(it->sigmaBefore, 4) + " → " +
            fixedStrPlain(it->sigmaAfter, 4));
    }
    return 0;
}

int cmdArb(CliEnv& env, const Args& args) {
    env.loadState();
    bool net = args.has("net");
    auto list = arbitrageList(env.st, 12);
    if (list.empty()) {
        out("（没有可用的跨所套利机会：价差已被运费、手续费、关税与封锁抹平）");
        return 0;
    }
    TextTable t;
    t.header({"资源", "买入所", "卖出所", "毛价差", "净价差", "容量"},
             {Align::Left, Align::Left, Align::Left, Align::Right, Align::Right, Align::Right});
    for (const auto& o : list) {
        t.row({std::string(commodityName(o.res)), std::string(exchangeInfo(o.buyExch).idName),
               std::string(exchangeInfo(o.sellExch).idName),
               fixedStrPlain((net ? o.netGap : o.grossGap) * Fixed(100), 3) + "%",
               fixedStrPlain(o.netGap * Fixed(100), 3) + "%", groupDigits(o.capacity)});
    }
    out(t.render());
    out("");
    out("净价差已扣除：双边手续费 + 跨所运费 + 关税 + 封锁惩罚。边疆自由市场(FX)运费最高。");
    return 0;
}

int cmdShock(CliEnv& env, const Args& args) {
    env.loadState();
    i64 last = args.getInt("last", 10);
    const auto& s = env.st.market.shocks;
    if (s.empty()) {
        out("（还没有市场冲击记录）");
        return 0;
    }
    TextTable t;
    t.header({"tick", "资源", "幅度", "σ 前", "σ 后", "原因"},
             {Align::Right, Align::Left, Align::Right, Align::Right, Align::Right, Align::Left});
    std::size_t start = s.size() > static_cast<std::size_t>(last) ? s.size() - static_cast<std::size_t>(last) : 0;
    for (std::size_t i = start; i < s.size(); ++i) {
        t.row({std::to_string(s[i].tick), std::string(commodityName(s[i].res)),
               fixedStrSigned(s[i].magnitude * Fixed(100), 2) + "%", fixedStrPlain(s[i].sigmaBefore, 4),
               fixedStrPlain(s[i].sigmaAfter, 4), s[i].cause});
    }
    out(t.render());
    return 0;
}

int cmdCredit(CliEnv& env, const Args&) {
    env.loadState();
    const GameState& st = env.st;
    TextTable t;
    t.header({"主体", "信用评级", "借贷利差", "债务", "违约概率"},
             {Align::Left, Align::Right, Align::Right, Align::Right, Align::Right});
    for (const auto& e : st.empires) {
        if (!e.alive) continue;
        t.row({e.name + (e.isPlayer ? " ★" : ""), fixedStrPlain(e.creditRating, 2),
               fixedStrPlain(borrowingSpread(st, e.id) * Fixed(100), 2) + "%",
               fixedStr(totalDebt(st, e.id), 0), fixedStrPlain(defaultProbability(st, e.id) * Fixed(100), 1) + "%"});
    }
    out(t.render());

    if (!st.market.debts.empty()) {
        out("");
        out(style("债务记录", Style::Sub));
        TextTable d;
        d.header({"#", "借款人", "本金", "利率", "发行 tick", "期限", "状态"});
        for (const auto& x : st.market.debts) {
            d.row({std::to_string(x.id), std::to_string(x.borrower), fixedStr(x.principal, 0),
                   fixedStrPlain(x.rate * Fixed(100), 2) + "%", std::to_string(x.issuedTick),
                   std::to_string(x.termTicks), x.defaulted ? "违约" : "正常"});
        }
        out(d.render());
    }
    if (!st.market.escrows.empty()) {
        out("");
        out(style("托管 / 信用证", Style::Sub));
        TextTable e2;
        e2.header({"#", "付款方", "收款方", "金额", "期限", "状态"});
        for (const auto& x : st.market.escrows) {
            e2.row({std::to_string(x.id), std::to_string(x.payer), std::to_string(x.payee), fixedStr(x.amount, 0),
                    std::to_string(x.termTicks), x.released ? "已释放" : (x.breached ? "违约" : "存续")});
        }
        out(e2.render());
    }
    out("");
    out("信誉即抵押品：托管/信用证降低违约概率，但占用资金成本。信用评级影响借贷利差与信用额度。");
    return 0;
}

int cmdFx(CliEnv& env, const Args&) {
    env.loadState();
    const MarketState& m = env.st.market;
    TextTable t;
    t.header({"交易所", "结算币价（对 credits）", "费率", "运费", "监管", "封锁", "关税"},
             {Align::Left, Align::Right, Align::Right, Align::Right, Align::Right, Align::Right, Align::Right});
    for (int e = 0; e < kExchangeCount; ++e) {
        const ExchangeInfo& ei = exchangeInfo(e);
        const ExchangeMarket& x = m.exchanges[static_cast<std::size_t>(e)];
        t.row({std::string(ei.nameZh), fixedStrPlain(m.fx[static_cast<std::size_t>(e)], 3),
               fixedStrPlain(ei.fee * Fixed(100), 2) + "%", fixedStrPlain(ei.transitCost * Fixed(100), 2) + "%",
               fixedStrPlain(ei.regulator, 2), fixedStrPlain(x.embargo, 2),
               fixedStrPlain(x.tariff * Fixed(100), 2) + "%"});
    }
    out(t.render());
    out("");
    out("战时配给强度：" + fixedStrPlain(m.rationing, 2) + "（抑制需求 " +
        fixedStrPlain((Fixed(1) - rationingDampen(env.st)) * Fixed(100), 1) + "%）");
    out("直接汇率示例：1 CX 币 = " + fixedStrPlain(fxRate(env.st, kExchCX, kExchFX), 4) + " FX 币");
    return 0;
}

int cmdBlackmarket(CliEnv& env, const Args&) {
    env.loadState();
    const MarketState& m = env.st.market;
    out(style("═══ 黑市 ═══", Style::Heading));
    TextTable t;
    t.header({"项目", "值"});
    t.row({"平均溢价", fixedStrPlain(m.blackMarketPremium * Fixed(100), 1) + "%"});
    t.row({"配给强度", fixedStrPlain(m.rationing, 2)});
    t.row({"查没风险", fixedStrPlain(blackMarketRaidRisk(env.st, kPlayerId) * Fixed(100), 1) + "%"});
    out(t.render());
    out("");
    TextTable p;
    p.header({"资源", "白市(CX)", "黑市(BZ)", "溢价", "违禁"},
             {Align::Left, Align::Right, Align::Right, Align::Right, Align::Center});
    for (int c = 0; c < kCommodityCount; ++c) {
        if (std::string(commodityInfo(c).idName) != "contraband" && c % 3 != 0) continue;
        Fixed white = m.exchanges[kExchCX].books[static_cast<std::size_t>(c)].mid;
        Fixed black = blackMarketPriceOf(env.st, static_cast<u8>(c));
        bool banned = !commodityInfo(c).tradable;
        p.row({std::string(commodityName(c)), fixedStrPlain(white, 2), fixedStrPlain(black, 2),
               fixedStrPlain(blackMarketPremiumOf(env.st, static_cast<u8>(c)) * Fixed(100), 1) + "%",
               banned ? "是" : "-"});
    }
    out(p.render());
    return 0;
}

// ---------------------------------------------------------------------------
// 交易动作
// ---------------------------------------------------------------------------
int cmdOrder(CliEnv& env, const Args& args) {
    env.loadState();
    if (args.posCount() < 3) {
        fail(ExitCode::BadArgs,
             "用法：greyfall order buy|sell <res> <qty> [@ <price>] [--exch X --tif day|gtc] "
             "[--kind limit|market|iceberg] [--shown N]");
    }
    std::string side = toLower(args.pos(0));
    if (side != "buy" && side != "sell") fail(ExitCode::BadArgs, "方向必须是 buy 或 sell");
    int res = commodityIndexByName(args.pos(1));
    if (res < 0) fail(ExitCode::BadArgs, "未知资源【" + args.pos(1) + "】");
    i64 qty = parseInt(args.pos(2), -1);
    if (qty <= 0) fail(ExitCode::BadArgs, "数量必须为正整数");

    OrderRequest req;
    req.owner = kPlayerId;
    req.res = static_cast<u8>(res);
    req.exch = static_cast<u8>(args.has("exch") ? requireExch(args.get("exch")) : kExchCX);
    req.buy = (side == "buy");
    req.qty = qty;

    std::string tif = toLower(args.get("tif", "gtc"));
    req.tif = (tif == "day") ? Tif::Day : Tif::Gtc;

    // 注意：先记下「是否显式指定了类型」，真正的判定放到价格解析之后。
    // 未指定类型且未给价格时应当按**市价**成交 ——
    // 早期一律默认限价单，导致 `order buy 合金 100` 这种最自然的写法直接报错。
    const bool kindGiven = args.has("kind");
    std::string kind = toLower(args.get("kind", "limit"));
    if (kind == "market") req.kind = OrderKind::Market;
    else if (kind == "iceberg") req.kind = OrderKind::Iceberg;
    else req.kind = OrderKind::Limit;
    req.shown = args.getInt("shown", 0);

    // 价格：支持 `@ 31.4` 与 `@31.4` 两种写法
    bool havePrice = false;
    for (std::size_t i = 3; i < args.posCount(); ++i) {
        const std::string& tok = args.pos(i);
        if (tok.empty() || tok[0] != '@') continue;
        std::string num = tok.substr(1);
        if (num.empty()) {
            if (i + 1 >= args.posCount()) fail(ExitCode::BadArgs, "@ 后必须给出价格");
            num = args.pos(i + 1);
        }
        bool ok = false;
        req.px = parseFixed(num, &ok);
        if (!ok) fail(ExitCode::BadArgs, "无法解析价格 " + num);
        havePrice = true;
        break;
    }
    if (!havePrice && args.posCount() > 3 && args.pos(3) != "@") {
        bool ok = false;
        Fixed p = parseFixed(args.pos(3), &ok);
        if (ok) {
            req.px = p;
            havePrice = true;
        }
    }
    // 未显式指定类型、且调用者没给价格 ⇒ 视为市价单（符合直觉）
    if (!kindGiven && !havePrice) req.kind = OrderKind::Market;
    if (req.kind == OrderKind::Market) {
        if (!havePrice) {
            const Book& b = env.st.market.exchanges[req.exch].books[static_cast<std::size_t>(res)];
            req.px = req.buy ? (b.mid * Fixed::raw(1300)) : (b.mid * Fixed::raw(700));
        }
    } else if (!havePrice) {
        fail(ExitCode::BadArgs,
             "限价单必须给出价格。写法：greyfall order buy <商品> <数量> @ <价格>；"
             "若要按市价立即成交，可省略价格或加 --kind market");
    }

    OrderAck ack = marketSubmitOrder(env.st, req);
    if (!ack.accepted && ack.filled == 0) {
        env.st.logEvent(LogPhase::Market, kLogOrderRejected, "订单被拒：" + ack.reason, kPlayerId);
        env.commit("order-rejected");
        out(style("订单未成交或未入簿：" + ack.reason, Style::Bad));
        return static_cast<int>(ExitCode::NoFill);
    }

    // 记录为计划动作（AI 会读到 —— 这是"透视"的直接体现）
    PlannedAction pa;
    pa.kind = PlannedKind::Order;
    pa.command = "order " + side + " " + std::string(commodityInfo(res).idName) + " " + std::to_string(qty);
    pa.res = static_cast<u8>(res);
    pa.qty = req.buy ? qty : -qty;
    pa.px = req.px;
    pa.exch = req.exch;
    pa.queuedTick = env.st.tick;
    env.st.pendingActions.push_back(pa);

    TextTable t;
    t.header({"项目", "值"});
    t.row({"订单号", ack.orderId == 0 ? "（已全部成交）" : std::to_string(ack.orderId)});
    t.row({"方向", req.buy ? "买入" : "卖出"});
    t.row({"资源 / 数量", std::string(commodityInfo(res).nameZh) + " ×" + groupDigits(qty)});
    t.row({"限价", fixedStrPlain(req.px, 2)});
    t.row({"交易所", std::string(exchangeInfo(req.exch).idName)});
    t.row({"有效期", std::string(tifName(req.tif))});
    t.row({"成交", groupDigits(ack.filled) + " @ " + (ack.filled ? fixedStrPlain(ack.avgPx, 2) : "—")});
    t.row({"残单", groupDigits(ack.remaining)});
    t.row({"手续费", fixedStr(ack.fee, 2)});
    t.row({"冲击", fixedStrPlain(ack.impact * Fixed(100), 3) + "%"});
    out(t.render());
    out("");
    out(style("注意：你的订单队列对泛视网络完全可见 —— AI 可以抢在你之前吃掉关键档位。", Style::Warn));
    out("  反制：拆单（多次小额）、冰量单（--kind iceberg --shown N）、--tif day、或先 decoy 误导。");

    env.commit("order");
    return 0;
}

int cmdCancel(CliEnv& env, const Args& args) {
    env.loadState();
    if (args.posCount() < 1) fail(ExitCode::BadArgs, "用法：greyfall cancel <oid>");
    u64 oid = static_cast<u64>(parseInt(args.pos(0), 0));
    bool found = false;
    i64 remaining = 0;
    for (int e = 0; e < kExchangeCount && !found; ++e) {
        for (int c = 0; c < kCommodityCount && !found; ++c) {
            remaining = marketCancelOrder(env.st, static_cast<u8>(e), static_cast<u8>(c), oid, &found);
        }
    }
    if (!found) fail(ExitCode::IllegalAction, "找不到订单 #" + std::to_string(oid));
    env.commit("cancel");
    out("已撤销订单 #" + std::to_string(oid) + "（释放 " + groupDigits(remaining) + " 单位）");
    return 0;
}

int cmdModify(CliEnv& env, const Args& args) {
    env.loadState();
    if (args.posCount() < 1) fail(ExitCode::BadArgs, "用法：greyfall modify <oid> [@ <price>] [<qty>]");
    u64 oid = static_cast<u64>(parseInt(args.pos(0), 0));
    // 找到订单
    u8 exch = 0, res = 0;
    Order snapshot;
    bool found = false;
    for (int e = 0; e < kExchangeCount && !found; ++e) {
        for (int c = 0; c < kCommodityCount && !found; ++c) {
            Book& b = env.st.market.exchanges[static_cast<std::size_t>(e)].books[static_cast<std::size_t>(c)];
            Order* o = bookFind(b, oid);
            if (o == nullptr) continue;
            snapshot = *o;
            exch = static_cast<u8>(e);
            res = static_cast<u8>(c);
            found = true;
        }
    }
    if (!found) fail(ExitCode::IllegalAction, "找不到订单 #" + std::to_string(oid));
    bool dummy = false;
    (void)marketCancelOrder(env.st, exch, res, oid, &dummy);

    OrderRequest req;
    req.owner = kPlayerId;
    req.res = res;
    req.exch = exch;
    req.buy = snapshot.buy;
    req.qty = snapshot.qty - snapshot.filled;
    req.px = snapshot.px;
    req.tif = snapshot.tif;
    req.kind = snapshot.kind;
    for (std::size_t i = 1; i < args.posCount(); ++i) {
        const std::string& tok = args.pos(i);
        if (!tok.empty() && tok[0] == '@') {
            std::string num = tok.substr(1);
            if (num.empty() && i + 1 < args.posCount()) {
                num = args.pos(i + 1);
                ++i;
            }
            bool ok = false;
            Fixed p = parseFixed(num, &ok);
            if (ok) req.px = p;
        } else {
            i64 q = parseInt(tok, -1);
            if (q > 0) req.qty = q;
        }
    }
    if (args.has("price")) req.px = args.getFixed("price", req.px);
    OrderAck ack = marketSubmitOrder(env.st, req);
    env.commit("modify");
    out("已改单 #" + std::to_string(oid) + " → 新单 " +
        (ack.orderId ? ("#" + std::to_string(ack.orderId)) : std::string("（已成交）")) + "  成交 " +
        groupDigits(ack.filled) + " 残 " + groupDigits(ack.remaining));
    out(style("改单会失去原有的时间优先权（重新排队）。", Style::Dim));
    return 0;
}

int cmdFutures(CliEnv& env, const Args& args) {
    env.loadState();
    if (args.posCount() < 4) {
        fail(ExitCode::BadArgs, "用法：greyfall futures buy|sell <res> <F1..F4> <qty> [--lev n]");
    }
    std::string side = toLower(args.pos(0));
    if (side != "buy" && side != "sell") fail(ExitCode::BadArgs, "方向必须是 buy 或 sell");
    int res = commodityIndexByName(args.pos(1));
    if (res < 0) fail(ExitCode::BadArgs, "未知资源【" + args.pos(1) + "】");
    std::string exp = toUpper(args.pos(2));
    if (exp.size() != 2 || exp[0] != 'F' || exp[1] < '1' || exp[1] > '4') {
        fail(ExitCode::BadArgs, "期限必须是 F1..F4");
    }
    u8 term = static_cast<u8>(exp[1] - '1');
    i64 qty = parseInt(args.pos(3), -1);
    if (qty <= 0) fail(ExitCode::BadArgs, "数量必须为正整数");
    int lev = static_cast<int>(args.getInt("lev", 1));
    bool isShort = (side == "sell");

    std::string err;
    if (!futuresOpen(env.st, static_cast<u8>(res), term, qty, isShort, lev, &err)) {
        fail(ExitCode::IllegalAction, err);
    }
    env.commit("futures");
    const FuturesQuote& q =
        env.st.market.exchanges[kExchCX].futures[static_cast<std::size_t>(res)][static_cast<std::size_t>(term)];
    out("已" + std::string(isShort ? "卖出" : "买入") + " " + std::string(commodityInfo(res).nameZh) + " F" +
        std::to_string(term + 1) + " ×" + groupDigits(qty) + " @ " + fixedStrPlain(q.price, 2) + "（杠杆 " +
        std::to_string(lev) + "）");
    out("保证金占用后可用现金：" + fixedStr(env.st.market.margin.cash, 1) + "，利率参考：" +
        fixedStrPlain(borrowingSpread(env.st, kPlayerId) * Fixed(100), 2) + "%");
    return 0;
}

int cmdSettle(CliEnv& env, const Args&) {
    env.loadState();
    TickReport rep;
    futuresSettleExpiry(env.st, rep);
    settlementPhase(env.st, rep);
    env.commit("settle");
    out("交割与债务结算完成。名义额 " + fixedStr(rep.notional, 0) + "，事件 " + std::to_string(rep.eventsFired));
    return 0;
}

int cmdBorrow(CliEnv& env, const Args& args) {
    env.loadState();
    if (args.posCount() < 1) fail(ExitCode::BadArgs, "用法：greyfall borrow <amt> [--term N]");
    bool ok = false;
    Fixed amt = parseFixed(args.pos(0), &ok);
    if (!ok || amt.rawValue() <= 0) fail(ExitCode::BadArgs, "借款金额无效");
    int term = static_cast<int>(args.getInt("term", 8));
    std::string err;
    if (!borrowCredits(env.st, kPlayerId, amt, term, &err)) fail(ExitCode::IllegalAction, err);
    env.commit("borrow");
    out("已发行债券 " + fixedStr(amt, 1) + " cr，利差 " +
        fixedStrPlain(borrowingSpread(env.st, kPlayerId) * Fixed(100), 2) + "%，期限 " + std::to_string(term) +
        " 季");
    out("总债务：" + fixedStr(totalDebt(env.st, kPlayerId), 1) + "，信用评级：" +
        fixedStrPlain(env.player().creditRating, 2));
    return 0;
}

int cmdRepay(CliEnv& env, const Args& args) {
    env.loadState();
    Fixed amt = args.has("amount") ? args.getFixed("amount", Fixed(0)) : Fixed(0);
    if (amt.rawValue() <= 0) amt = totalDebt(env.st, kPlayerId);
    if (amt.rawValue() <= 0) {
        out("（没有需要偿还的债务）");
        return 0;
    }
    std::string err;
    if (!repayCredits(env.st, kPlayerId, amt, &err)) fail(ExitCode::IllegalAction, err);
    env.commit("repay");
    out("已偿还 " + fixedStr(amt, 1) + " cr。剩余债务 " + fixedStr(totalDebt(env.st, kPlayerId), 1));
    return 0;
}

int cmdEscrow(CliEnv& env, const Args& args) {
    env.loadState();
    if (args.posCount() < 3) {
        fail(ExitCode::BadArgs, "用法：greyfall escrow open|release <emp|id> <amt> [--term N]");
    }
    std::string op = toLower(args.pos(0));
    std::string err;
    if (op == "open") {
        i64 target = parseInt(args.pos(1), -1);
        if (target < 0 || target >= static_cast<i64>(env.st.empires.size())) {
            fail(ExitCode::BadArgs, "帝国编号越界");
        }
        bool ok = false;
        Fixed amt = parseFixed(args.pos(2), &ok);
        if (!ok || amt.rawValue() <= 0) fail(ExitCode::BadArgs, "金额无效");
        int term = static_cast<int>(args.getInt("term", 4));
        if (!escrowOpen(env.st, kPlayerId, static_cast<u32>(target), amt, 0, 0, term, &err)) {
            fail(ExitCode::IllegalAction, err);
        }
        env.commit("escrow");
        out("已开立托管：向 " + env.st.empires[static_cast<std::size_t>(target)].name + " 支付 " +
            fixedStr(amt, 1) + " cr（期限 " + std::to_string(term) + " 季）");
    } else if (op == "release") {
        u32 id = static_cast<u32>(parseInt(args.pos(1), 0));
        if (!escrowRelease(env.st, id, true, &err)) fail(ExitCode::IllegalAction, err);
        env.commit("escrow");
        out("已释放托管 #" + std::to_string(id));
    } else {
        fail(ExitCode::BadArgs, "操作必须是 open 或 release");
    }
    return 0;
}

int cmdInsure(CliEnv& env, const Args& args) {
    env.loadState();
    int res = requireRes(args, 0, "greyfall insure <res> [--cover N]");
    i64 cover = args.getInt("cover", 1000);
    if (cover <= 0) fail(ExitCode::BadArgs, "--cover 必须为正");
    std::string err;
    if (!insurePosition(env.st, static_cast<u8>(res), cover, &err)) fail(ExitCode::IllegalAction, err);
    env.commit("insure");
    out("已为 " + std::string(commodityInfo(res).nameZh) + " 投保 " + groupDigits(cover) + " 单位。");
    return 0;
}

}  // namespace gf
