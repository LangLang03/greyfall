#include "gen/WorldGen.h"

#include <algorithm>
#include <string>

#include "domain/Empire.h"
#include "gen/EmpireGen.h"
#include "gen/EventSchedule.h"
#include "gen/ModifierGen.h"
#include "gen/NameGen.h"
#include "gen/StarMapGen.h"
#include "items/ItemDef.h"
#include "mkt/OrderBook.h"
#include "rng/Streams.h"

namespace gf {
namespace {

Fixed exchangePriceFactor(int exch) {
    switch (exch) {
        case kExchCX: return Fixed(1);
        case kExchFX: return Fixed::raw(1070);   // 边疆溢价 7%
        default: return Fixed::raw(1450);        // 黑市溢价 45%
    }
}

void marketInit(GameState& st) {
    MarketState& m = st.market;
    const bool scarceAlloys = hasModifier(st.modifierBits, kModScarceAlloys);
    const bool boom = hasModifier(st.modifierBits, kModBoomCycle);

    for (int e = 0; e < kExchangeCount; ++e) {
        ExchangeMarket& x = m.exchanges[static_cast<std::size_t>(e)];
        x.kind = static_cast<u8>(e);
        x.embargo = Fixed(0);
        x.tariff = Fixed(0);
        x.clearingReserve = Fixed(2400000);
        for (int c = 0; c < kCommodityCount; ++c) {
            const CommodityInfo& ci = commodityInfo(c);
            Book& b = x.books[static_cast<std::size_t>(c)];
            bookClear(b);
            if (!ci.tradable) {
                b.last = ci.basePrice;
                b.mid = ci.basePrice;
                continue;
            }
            Fixed mid = ci.basePrice * exchangePriceFactor(e);
            if (scarceAlloys && c == static_cast<int>(Commodity::Alloys)) mid = mid * Fixed::raw(1350);
            if (boom) mid = mid * Fixed::raw(1080);
            b.last = mid;
            b.mid = mid;
            b.open = mid;
            b.high = mid;
            b.low = mid;
            b.sigma = ci.volatility;
            b.var20 = Fixed(ci.typicalVolume / 20);
            b.vwap = mid;

            Fixed step = Fixed::ratio(mid, 40, 10000);        // 0.40% 档位间隔
            Fixed halfSpread = Fixed::ratio(mid, 6, 10000);   // 0.06% 半价差
            // 低价标的必须保留至少 1 个最小刻度，否则买卖价会重合
            if (step.rawValue() < 2) step = Fixed::raw(2);
            if (halfSpread.rawValue() < 2) halfSpread = Fixed::raw(2);
            const i64 baseQty = ci.typicalVolume / 30 + 10;
            const int levels = 6;
            for (int i = 0; i < levels; ++i) {
                i64 q = baseQty * (1 + i);
                for (int side = 0; side < 2; ++side) {
                    Order o;
                    o.id = m.nextOrderId++;
                    o.seq = static_cast<u32>(m.nextSeq++);
                    o.owner = kNoEmpire;                     // 做市商
                    o.controller = 0xFFFF0000u + static_cast<u32>(e);
                    o.px = side == 0 ? (mid - halfSpread - step * Fixed(i)) : (mid + halfSpread + step * Fixed(i));
                    o.qty = q;
                    o.shown = q;
                    o.buy = (side == 0);
                    o.kind = OrderKind::Limit;
                    o.tif = Tif::Gtc;
                    o.exch = static_cast<u8>(e);
                    o.res = static_cast<u8>(c);
                    o.placedTick = 0;
                    o.synthetic = true;
                    b.orders.push_back(o);
                }
            }
            bookRebuildLevels(b);
            b.mid = bookMid(b);
            b.spread = bookSpread(b);

            // 期货期限结构：contango（carry > convenience），低库存转 backwardation
            for (int t = 0; t < kFuturesTerms; ++t) {
                FuturesQuote& f = x.futures[static_cast<std::size_t>(c)][static_cast<std::size_t>(t)];
                Fixed carry = Fixed::raw(6) * Fixed(t + 1);
                Fixed convenience = Fixed::raw(2) * Fixed(t + 1);
                f.carry = carry;
                f.convenience = convenience;
                f.price = mid * (Fixed(1) + carry - convenience);
                f.basis = mid.rawValue() > 0 ? Fixed::raw(mulDivSat(f.price.rawValue() - mid.rawValue(), FIX, mid.rawValue()))
                                             : Fixed(0);
                f.openInterest = 0;
            }

            MarketMakerState& mm = x.mm[static_cast<std::size_t>(c)];
            mm.inventory = Fixed(0);
            mm.gamma = Fixed::raw(120);
            mm.baseSpread = halfSpread;
            mm.halted = false;
            mm.quotesPlaced = 0;
            mm.absorbed = 0;
            // 库存上限以该标的典型成交量的 10% 为标度
            mm.inventoryLimit = std::max<i64>(200, ci.typicalVolume / 10);
        }
    }

    for (int c = 0; c < kCommodityCount; ++c) {
        const CommodityInfo& ci = commodityInfo(c);
        VolState& v = m.vol[static_cast<std::size_t>(c)];
        v.omega = Fixed::raw(std::max<i64>(1, ci.volatility.rawValue() / 60));
        v.alpha = Fixed::raw(80);
        v.beta = Fixed::raw(860);
        v.lastSigma = ci.volatility;
        v.lastReturn = Fixed(0);
        v.jumpBias = Fixed(0);
        m.spotIndex[static_cast<std::size_t>(c)] = m.exchanges[kExchCX].books[static_cast<std::size_t>(c)].mid;
        m.arbGap[static_cast<std::size_t>(c)] = Fixed(0);
        m.blackMarketPrice[static_cast<std::size_t>(c)] =
            m.exchanges[kExchBZ].books[static_cast<std::size_t>(c)].mid;
        (void)ci;
    }
    m.fx = {Fixed(1), Fixed::raw(940), Fixed::raw(1350)};
    m.blackMarketPremium = Fixed::pct(35);
    m.rationing = Fixed(0);
    m.margin.cash = st.empires.empty() ? Fixed(120000) : st.empires[0].treasury;
    m.margin.initMargin = Fixed::pct(20);
    m.margin.maintMargin = Fixed::pct(10);
    m.margin.equity = m.margin.cash;
    m.nextOrderId = std::max<u64>(m.nextOrderId, 1);
}

}  // namespace

void initMarket(GameState& st) { marketInit(st); }

void generateWorld(GameState& st, const WorldGenOptions& opts) {
    st = GameState{};
    st.schemaVersion = static_cast<u32>(kSchemaVersion);
    st.seed = opts.seed;
    st.tick = 0;
    st.difficulty = std::clamp(opts.difficulty, 1, 5);
    st.epochIndex = opts.epochIndex;
    st.legacyMask = opts.legacyMask;
    st.endless = opts.endless;
    st.act = 1;
    st.aiForesight = std::clamp(1 + st.difficulty / 2, 1, 4);
    st.aiNodeBudget = 4000 + static_cast<i64>(st.difficulty) * 6000;
    st.rng.seed(opts.seed);
    // 链头非零 = 链存在；只有真正缺失时才会被标记为焚毁
    st.chronicleHead = opts.seed ^ 0xC4A0C1E1ull;
    st.initRelations();

    NameGen names(opts.seed ^ 0xEE0C4ull);
    st.epochName = opts.epochName.empty() ? names.epoch() : opts.epochName;

    std::string modName;
    st.modifierBits = rollModifiers(opts.seed, st.difficulty, modName);
    st.modifierName = modName;
    if (hasModifier(st.modifierBits, kModPanopticonBoost)) {
        st.aiForesight = std::min(4, st.aiForesight + 1);
    }
    if (hasModifier(st.modifierBits, kModSilentArchive)) {
        // 静默档案：初始存档链被视为"曾经焚毁过"的敏感状态
        st.logEvent(LogPhase::Chronicle, kLogRollback, "词缀【静默档案】：AI 对存档行为的容忍度减半");
    }

    StarMapOptions sm;
    sm.systemCount = std::clamp(opts.systemCount, 24, 256);
    sm.sectorCount = std::clamp(opts.empireCount / 2, 4, 10);
    sm.minPlanets = 1;
    sm.maxPlanets = 4;
    generateStarMap(st, sm);

    EmpireGenOptions eg;
    eg.count = opts.empireCount;
    eg.difficulty = st.difficulty;
    eg.seed = opts.seed;
    generateEmpires(st, eg);

    generateAnomalies(st);
    // 太空生物：可猎杀的资源点（海星可加工成罐头）
    scatterFauna(st, st.rng, sm.systemCount);
    scheduleEvents(st);
    initClueGraph(st);

    // 关系矩阵：观感由伦理/政体相似度与距离决定
    const bool trustDeficit = hasModifier(st.modifierBits, kModTrustDeficit);
    for (std::size_t a = 0; a < st.empires.size(); ++a) {
        for (std::size_t b = 0; b < st.empires.size(); ++b) {
            if (a == b) continue;
            Relation& r = st.relation(static_cast<u32>(a), static_cast<u32>(b));
            const Empire& ea = st.empires[a];
            const Empire& eb = st.empires[b];
            int sharedEthics = 0;
            for (u8 x : ea.ethics)
                for (u8 y : eb.ethics)
                    if (x == y) ++sharedEthics;
            Fixed base = Fixed::raw(static_cast<i64>(sharedEthics) * 120);
            base += empireModifier(ea, ModKind::DiploWeight);
            if (trustDeficit) base -= Fixed::pct(20);
            r.opinion = fxClamp(base + Fixed::raw(static_cast<i64>(st.rng.range(RngStream::Diplo, -150, 150))),
                                Fixed(-1), Fixed(1));
            r.trust = Fixed::raw(400 + static_cast<i64>(st.rng.range(RngStream::Diplo, 0, 300)));
            r.fear = Fixed(0);
            r.debt = Fixed(0);
            r.border = 0;
        }
    }

    // 联邦：若干 AI 组成一个初始联邦，玩家不在其中
    if (st.empires.size() > 4) {
        Federation fed;
        fed.id = 0;
        fed.name = names.federation();
        fed.founder = 1;
        int members = std::min<int>(4, static_cast<int>(st.empires.size()) - 2);
        for (int i = 0; i < members; ++i) {
            u32 id = static_cast<u32>(1 + i);
            fed.members.push_back(id);
            st.empires[id].federation = 0;
        }
        fed.cohesion = Fixed::pct(60);
        fed.treasury = Fixed(50000);
        st.federations.push_back(std::move(fed));
    } else {
        st.empires[0].federation = 0xFFFFFFFFu;
    }
    for (auto& e : st.empires)
        if (e.federation == 0 && std::find(st.federations[0].members.begin(), st.federations[0].members.end(), e.id) ==
                                     st.federations[0].members.end())
            e.federation = 0xFFFFFFFFu;

    marketInit(st);

    // 玩家初始道具：3 件起点筹码
    st.inventory.items.clear();
    for (int i = 0; i < 3; ++i) {
        ItemInstance it;
        it.def = static_cast<u16>(st.rng.pick(RngStream::Items, kItemCount));
        it.count = 1;
        it.prov.channel = ProvChannel::Gift;
        it.prov.credibility = Fixed::pct(65);
        it.prov.source = kNoEmpire;
        it.prov.tick = 0;
        it.acquiredTick = 0;
        st.inventory.items.push_back(std::move(it));
    }

    // 初始持仓与保证金
    st.market.margin.cash = st.empires[0].treasury;
    st.market.margin.equity = st.market.margin.cash;

    // 玩家的舰队设计起点
    if (st.empires[0].designs.empty()) {
        FleetDesign d;
        d.id = 0;
        d.name = "隼级";
        d.hull = HullClass::Frigate;
        d.firepower = hullInfo(HullClass::Frigate).baseFirepower;
        d.defense = hullInfo(HullClass::Frigate).baseDefense;
        d.speed = hullInfo(HullClass::Frigate).baseSpeed;
        d.custom = true;
        st.empires[0].designs.push_back(std::move(d));
    }

    st.logEvent(LogPhase::Setup, kLogNewGame,
                "新纪元【" + st.epochName + "】开启：帝国 " + std::to_string(st.empires.size()) + "，星系 " +
                    std::to_string(st.map.systems.size()) + "，词缀 " + st.modifierName);
    refreshEmpireBonuses(st);
    st.pushHistory();
}

}  // namespace gf
