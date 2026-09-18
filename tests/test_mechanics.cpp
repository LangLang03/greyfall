#include <fstream>
#include "domain/Economy.h"
#include <iterator>

#include "check.h"
#include "ai/AiCore.h"
#include "ai/FactionAI.h"
#include "ai/BetrayalCalculus.h"
#include "ai/ToModel.h"
#include "combat/Resolver.h"
#include "clue/ClueGraph.h"
#include "core/TickPipeline.h"
#include "domain/Construction.h"
#include "domain/Government.h"
#include "domain/Personnel.h"
#include "domain/Trade.h"
#include "gen/WorldGen.h"
#include "mkt/Futures.h"
#include "mkt/Margin.h"
#include "mkt/MarketEngine.h"
#include "mkt/Matching.h"
#include "mkt/OrderBook.h"
#include "mkt/Settlement.h"
#include "mkt/VolModel.h"
#include "plot/EventSystem.h"
#include "plot/BeatResolver.h"
#include "save/Migration.h"
#include "save/Serde.h"

using namespace gf;

namespace {
GameState mechanicsWorld() {
    GameState st;
    WorldGenOptions options;
    options.seed = 12345; options.empireCount = 8; options.systemCount = 48;
    generateWorld(st, options);
    return st;
}

GameState mechanicsTradeWorld(Fixed cash) {
    GameState st;
    st.initRelations(); st.empires.resize(2); st.planets.resize(2); st.map.systems.resize(2);
    for (u32 i = 0; i < 2; ++i) {
        auto& e = st.empires[i]; e.id = i; e.isPlayer = i == 0; e.systems = {i}; e.capital = i;
        auto& sys = st.map.systems[i]; sys.id = i; sys.owner = i; sys.planets = {i}; sys.links = {1-i};
        auto& p = st.planets[i]; p.id = i; p.owner = i; p.colonized = true; p.stability = Fixed(1);
        p.development = Fixed(0);
    }
    constexpr auto c = static_cast<u8>(Commodity::Minerals);
    st.planets[0].yield[c] = Fixed(100); st.empires[0].stock[c] = Fixed(1000);
    st.empires[1].demand[c] = Fixed(100); st.empires[1].treasury = cash;
    for (auto& ex : st.market.exchanges) ex.books[c].mid = Fixed(10);
    TradeRoute route; route.exporter = 0; route.importer = 1; route.commodity = c;
    route.tariff = Fixed(0); route.path = {0,1}; st.market.trade.routes.push_back(route);
    return st;
}

Order mechanicsOrder(u64 id, u32 owner, bool buy, int px, i64 qty) {
    Order o; o.id = id; o.seq = static_cast<u32>(id); o.owner = owner; o.buy = buy;
    o.px = Fixed(px); o.qty = qty; o.shown = qty; o.synthetic = false; return o;
}

Book& mechanicsEmptyBook(GameState& st, u8 res) {
    Book& b = st.market.exchanges[kExchCX].books[res];
    b = Book{}; b.mid = Fixed(100); b.last = Fixed(100); return b;
}
}

TEST(mechanics, sells_match_highest_bid_and_respect_iceberg_limit) {
    Book b;
    b.orders = {mechanicsOrder(1, 1, true, 90, 3), mechanicsOrder(3, 2, true, 110, 1),
                mechanicsOrder(2, 1, true, 110, 1), mechanicsOrder(4, 2, true, 100, 3)};
    MkOrder sell; sell.owner = 0; sell.buy = false; sell.qty = 5;
    sell.px = Fixed(105); sell.kind = OrderKind::Iceberg;
    auto r = matchOrder(b, sell, Fixed(0));
    CHECK_EQ(r.filled, 2); CHECK_EQ(r.avgPx, Fixed(110)); CHECK_EQ(r.remaining, 3);
    CHECK_EQ(r.consumed.size(), 2u);
    if (r.consumed.size() == 2) {
        CHECK_EQ(r.consumed[0].passiveId, 2u); CHECK_EQ(r.consumed[1].passiveId, 3u);
    }
}

TEST(mechanics, trade_ohlc_and_volume_keep_all_fills) {
    Book b; bookRecordTrade(b, Fixed(100), 2); bookRecordTrade(b, Fixed(120), 1);
    bookRecordTrade(b, Fixed(80), 1);
    CHECK_EQ(b.open, Fixed(100)); CHECK_EQ(b.high, Fixed(120)); CHECK_EQ(b.low, Fixed(80));
    CHECK_EQ(b.volume, 4); CHECK(fxAbs(b.vwap - Fixed(100)).rawValue() <= 1); CHECK_EQ(b.last, Fixed(80));
}

TEST(mechanics, passive_seller_and_active_buyer_conserve_goods_and_cash) {
    auto st = mechanicsWorld(); constexpr auto c = static_cast<u8>(Commodity::Alloys);
    auto& b = mechanicsEmptyBook(st, c); b.orders.push_back(mechanicsOrder(999, 1, false, 100, 3));
    bookRebuildLevels(b);
    auto cash0 = st.market.margin.cash; auto cash1 = st.empires[1].treasury;
    auto goods0 = st.player().stock[c]; auto goods1 = st.empires[1].stock[c];
    OrderRequest req; req.owner = 0; req.res = c; req.exch = kExchCX; req.buy = true;
    req.qty = 3; req.kind = OrderKind::Market;
    auto ack = marketSubmitOrder(st, req);
    CHECK_EQ(ack.filled, 3);
    auto fee = marketFee(kExchCX, Fixed(300));
    CHECK_EQ(st.player().stock[c], goods0 + Fixed(3));
    CHECK_EQ(st.empires[1].stock[c], goods1 - Fixed(3));
    CHECK_EQ(st.market.margin.cash, cash0 - Fixed(300) - fee);
    CHECK_EQ(st.empires[1].treasury, cash1 + Fixed(300) - fee);
    CHECK_EQ(st.player().treasury, st.market.margin.cash);
}

TEST(mechanics, resting_bids_share_current_budget_and_settle_passive_player) {
    auto st = mechanicsWorld(); constexpr auto c = static_cast<u8>(Commodity::Alloys);
    auto& b = mechanicsEmptyBook(st, c);
    b.orders = {mechanicsOrder(999, 0, true, 100, 10), mechanicsOrder(1000, 0, true, 100, 10)};
    bookRebuildLevels(b);
    st.market.margin.cash = Fixed(250); st.player().treasury = Fixed(250);
    const auto stock = st.player().stock[c];
    OrderRequest req; req.owner = 1; req.res = c; req.exch = kExchCX; req.buy = false;
    req.qty = 20; req.kind = OrderKind::Market;
    const auto ack = marketSubmitOrder(st, req);
    CHECK_EQ(ack.filled, 2); CHECK_EQ(st.player().stock[c], stock + Fixed(2));
    CHECK(st.market.margin.cash.rawValue() >= 0);
    CHECK_EQ(st.market.margin.cash, Fixed(50) - marketFee(kExchCX, Fixed(200)));
}

TEST(mechanics, resting_asks_cannot_sell_missing_stock) {
    auto st = mechanicsWorld(); constexpr auto c = static_cast<u8>(Commodity::Alloys);
    auto& b = mechanicsEmptyBook(st, c);
    b.orders = {mechanicsOrder(999, 1, false, 100, 10), mechanicsOrder(1000, 1, false, 100, 10)};
    bookRebuildLevels(b); st.empires[1].stock[c] = Fixed(3);
    OrderRequest req; req.owner = 0; req.res = c; req.buy = true; req.qty = 20; req.kind = OrderKind::Market;
    CHECK_EQ(marketSubmitOrder(st, req).filled, 3); CHECK_EQ(st.empires[1].stock[c], Fixed(0));
}

TEST(mechanics, trade_unfunded_and_partial_delivery_match_payments) {
    constexpr auto c = static_cast<u8>(Commodity::Minerals);
    for (Fixed cash : {Fixed(0), Fixed(125), Fixed(1000)}) {
        auto st = mechanicsTradeWorld(cash); auto totalCash = st.empires[0].treasury + cash;
        tradePhase(st); const auto& route = st.market.trade.routes.front();
        const auto delivered = fxMin(Fixed(50), cash / Fixed(10));
        CHECK_EQ(route.volume, delivered); CHECK_EQ(st.empires[1].stock[c], delivered);
        CHECK_EQ(st.empires[0].stock[c], Fixed(1000) - delivered);
        CHECK_EQ(st.empires[1].treasury, cash - delivered * Fixed(10));
        CHECK_EQ(st.empires[0].treasury + st.empires[1].treasury, totalCash);
    }
}

TEST(mechanics, trade_recomputes_removed_paths) {
    auto st = mechanicsTradeWorld(Fixed(1000));
    st.map.systems[0].links.clear(); st.map.systems[1].links.clear();
    tradePhase(st);
    CHECK(!st.market.trade.routes[0].active); CHECK_EQ(st.market.trade.routes[0].volume, Fixed(0));
    CHECK_EQ(st.empires[1].stock[static_cast<u8>(Commodity::Minerals)], Fixed(0));
}

TEST(mechanics, building_charges_catalog_resources_and_exact_eta) {
    auto st = mechanicsWorld(); auto pid = st.map.systems[st.player().capital].planets.front();
    auto& p = st.planets[pid]; p.buildings.clear(); p.buildQueue.clear();
    st.player().treasury = Fixed(1000000); st.player().stock.fill(Fixed(10000));
    const auto before = st.player().stock;
    CHECK(enqueueBuilding(st, 0, pid, 0, nullptr));
    for (int c = 0; c < kCommodityCount; ++c)
        CHECK_EQ(before[c] - st.player().stock[c], Fixed(buildingInfo(0).cost[c]));
    CHECK(!p.buildQueue.empty()); if (p.buildQueue.empty()) return;
    const auto eta = p.buildQueue[0].totalTicks;
    for (u32 t = 1; t < eta; ++t) { constructionPhase(st); CHECK(p.buildings.empty()); }
    constructionPhase(st); CHECK_EQ(p.buildings.size(), 1u); CHECK(p.buildQueue.empty());
}

TEST(mechanics, building_rejection_is_atomic) {
    auto st = mechanicsWorld(); auto pid = st.map.systems[st.player().capital].planets.front();
    st.planets[pid].buildings.clear(); st.player().treasury = Fixed(1000000);
    st.player().stock[static_cast<u8>(Commodity::Minerals)] = Fixed(1);
    const auto before = serializeState(st);
    CHECK(!enqueueBuilding(st, 0, pid, 0, nullptr)); CHECK_EQ(serializeState(st), before);
}

TEST(mechanics, ai_builds_enter_same_queue) {
    auto st = mechanicsWorld(); auto& e = st.empires[1];
    e.treasury = Fixed(10000000); e.lastIncome = Fixed(100000); e.stock.fill(Fixed(100000));
    for (int i = 0; i < kTechCount; ++i) if (!techCompleted(e.tech, i)) e.tech.completed.push_back(static_cast<u8>(i));
    std::size_t built = 0, queued = 0;
    for (const auto& p : st.planets) if (p.owner == 1) { built += p.buildings.size(); queued += p.buildQueue.size(); }
    CHECK(aiConstructBest(st, 1));
    std::size_t newBuilt = 0, newQueued = 0;
    for (const auto& p : st.planets) if (p.owner == 1) { newBuilt += p.buildings.size(); newQueued += p.buildQueue.size(); }
    CHECK_EQ(newBuilt, built); CHECK_EQ(newQueued, queued + 1);
}

TEST(mechanics, bonuses_do_not_compound_and_survive_reload) {
    auto st = mechanicsWorld(); for (auto& p : st.planets) p.buildings.clear();
    auto& planet = st.planets[st.map.systems[st.player().capital].planets.front()];
    for (int b = 0; b < kBuildingCount; ++b) {
        const auto effect = buildingInfo(b).effect;
        if (effect == BuildingEffect::Trading || effect == BuildingEffect::Storage ||
            effect == BuildingEffect::ClueDiscovery || effect == BuildingEffect::ProdResearch) planet.buildings.push_back(b);
    }
    refreshEmpireBonuses(st);
    const auto trade = st.player().tradeBonus, storage = st.player().storageBonus, clue = st.player().clueBonus;
    const auto rate = st.player().tech.rate, bonus = st.player().tech.rateBonus;
    economyPhase(st); economyPhase(st);
    CHECK_EQ(st.player().tradeBonus, trade); CHECK_EQ(st.player().storageBonus, storage);
    CHECK_EQ(st.player().clueBonus, clue); CHECK_EQ(st.player().tech.rate, rate); CHECK_EQ(st.player().tech.rateBonus, bonus);
    GameState loaded; CHECK(tryDeserializeState(serializeState(st), loaded));
    CHECK_EQ(loaded.player().tradeBonus, trade); CHECK_EQ(loaded.player().storageBonus, storage);
    CHECK_EQ(loaded.player().clueBonus, clue); CHECK_EQ(loaded.player().tech.rateBonus, bonus);
    runHeadlessTicks(st, 2); runHeadlessTicks(loaded, 2);
    CHECK_EQ(serializeState(st), serializeState(loaded));
    planet.buildings.clear(); refreshEmpireBonuses(st);
    CHECK_EQ(st.player().tradeBonus, Fixed(0)); CHECK_EQ(st.player().storageBonus, Fixed(0));
}

TEST(mechanics, faction_choices_charge_and_apply_correct_effects) {
    auto st = mechanicsWorld(); st.pending.items.clear();
    auto& f = st.player().domestic.factions.front(); f.satisfaction = Fixed::pct(20);
    f.demandPressure = Fixed(2); f.lastDemandTick = 0; st.tick = 10;
    for (auto& other : st.player().domestic.factions) if (&other != &f) other.demandPressure = Fixed(0);
    domesticPhase(st);
    auto it = std::find_if(st.pending.items.begin(), st.pending.items.end(), [&](const PendingChoice& c) {
        return c.kind == ChoiceKind::Faction && c.subject == static_cast<u8>(f.kind);
    });
    CHECK(it != st.pending.items.end()); if (it == st.pending.items.end()) return;
    const auto index = static_cast<std::size_t>(it - st.pending.items.begin());
    auto cash = st.market.margin.cash, sat = f.satisfaction;
    Fixed cost = (Fixed(6000) + f.influence * Fixed(8000)) * (Fixed(1) + Fixed::pct(10) * Fixed(colonialCapacity(st, kPlayerId).owned));
    CHECK(resolveChoice(st, index, 0, nullptr));
    CHECK_EQ(st.market.margin.cash, cash - cost); CHECK_EQ(f.satisfaction, sat + Fixed::pct(25));
    CHECK_EQ(f.demandPressure, Fixed(0));
}

TEST(mechanics, deferred_choices_allow_time_until_due_and_serialize) {
    auto st = mechanicsWorld(); st.pending.items.clear();
    PendingChoice c; c.eventId = 55; c.options = {"A", "B"}; st.pending.push(c);
    CHECK(deferChoice(st, 0, 2, nullptr));
    CHECK(!st.pending.hasBlocking(st.tick)); CHECK(st.pending.hasBlocking(st.tick + 2));
    GameState loaded; CHECK(tryDeserializeState(serializeState(st), loaded));
    CHECK_EQ(loaded.pending.items[0].deferredUntil, st.tick + 2);
    CHECK_EQ(advanceTicks(loaded, 1, true), 0); CHECK_EQ(loaded.tick, st.tick + 1);
}

TEST(mechanics, uninsured_and_negative_cover_cannot_create_cash) {
    auto st = mechanicsWorld(); st.market.margin.cash = Fixed(-100); st.market.margin.equity = Fixed(-100);
    insuranceSettle(st); CHECK_EQ(st.market.margin.cash, Fixed(-100));
    const auto before = serializeState(st);
    CHECK(!insurePosition(st, static_cast<u8>(Commodity::Alloys), -100, nullptr));
    CHECK_EQ(serializeState(st), before);
}

TEST(mechanics, insurance_pays_only_covered_losses_once_and_expires) {
    auto st = mechanicsWorld(); constexpr auto c = static_cast<u8>(Commodity::Alloys);
    st.player().stock[c] = Fixed(10); st.market.exchanges[0].books[c].mid = Fixed(100);
    CHECK(insurePosition(st, c, 10, nullptr)); CHECK(!insurePosition(st, c, 1, nullptr));
    GameState loaded; CHECK(tryDeserializeState(serializeState(st), loaded));
    CHECK_EQ(loaded.market.insurancePolicies.size(), 1u);
    loaded.market.exchanges[0].books[c].mid = Fixed(80); loaded.market.margin.equity = Fixed(-500);
    auto cash = loaded.market.margin.cash;
    insuranceSettle(loaded); CHECK_EQ(loaded.market.margin.cash, cash + Fixed(200));
    insuranceSettle(loaded); CHECK_EQ(loaded.market.margin.cash, cash + Fixed(200));
    CHECK_EQ(loaded.player().treasury, loaded.market.margin.cash);
    loaded.tick += 4; loaded.market.exchanges[0].books[c].mid = Fixed(1);
    insuranceSettle(loaded); CHECK_EQ(loaded.market.margin.cash, cash + Fixed(200));
    CHECK(loaded.market.insurancePolicies.empty());
}

TEST(mechanics, day_orders_expire_and_budgets_reset_at_quarter_end) {
    auto st = mechanicsWorld(); constexpr auto c = static_cast<u8>(Commodity::Alloys);
    auto& b = st.market.exchanges[0].books[c]; auto o = mechanicsOrder(999999, 0, true, 1, 1);
    o.tif = Tif::Day; o.placedTick = st.tick; b.orders.push_back(o);
    auto gtc = o; gtc.id++; gtc.tif = Tif::Gtc; b.orders.push_back(gtc);
    ActorMarketStats stats; stats.spentThisTick = Fixed(123); st.market.actorStats.push_back(stats);
    phaseCommit(st);
    CHECK(bookFind(b, o.id) == nullptr); CHECK(bookFind(b, gtc.id) != nullptr);
    CHECK_EQ(st.market.actorStats.back().spentThisTick, Fixed(0));
}

TEST(mechanics, futures_preserve_owner_margin_and_expire_all_terms) {
    auto st = mechanicsWorld(); constexpr auto c = static_cast<u8>(Commodity::Alloys);
    auto initial = st.empires[1].treasury;
    CHECK(futuresOpen(st, 1, c, 2, 5, false, 1, nullptr));
    CHECK_EQ(st.market.futuresPositions.size(), 1u); if (st.market.futuresPositions.empty()) return;
    const auto p = st.market.futuresPositions.front();
    CHECK_EQ(p.owner, 1u); CHECK_EQ(p.expiryTick, 11u);
    CHECK_EQ(st.empires[1].treasury + p.margin, initial);
    auto playerCash = st.market.margin.cash;
    (void)marginMarkToMarket(st); CHECK_EQ(st.market.margin.equity, playerCash);
    GameState loaded; CHECK(tryDeserializeState(serializeState(st), loaded));
    CHECK_EQ(loaded.market.futuresPositions[0].owner, 1u);
    loaded.tick = 11; loaded.market.exchanges[0].books[c].mid = p.entry;
    TickReport report; futuresSettleExpiry(loaded, report);
    CHECK_EQ(loaded.empires[1].treasury, initial); CHECK_EQ(loaded.market.margin.cash, playerCash);
    CHECK(loaded.market.futuresPositions.empty());
}

TEST(mechanics, futures_negative_close_is_atomic_and_equity_includes_margin) {
    auto st = mechanicsWorld(); constexpr auto c = static_cast<u8>(Commodity::Alloys);
    auto cash = st.market.margin.cash;
    CHECK(futuresOpen(st, c, 0, 5, false, 1, nullptr));
    CHECK(!marginMarkToMarket(st)); CHECK_EQ(st.market.margin.equity, cash);
    CHECK_EQ(st.player().treasury, st.market.margin.cash);
    const auto before = serializeState(st);
    CHECK(!futuresClose(st, c, 0, -5, nullptr)); CHECK_EQ(serializeState(st), before);
}

TEST(mechanics, volume_ewma_uses_resource_units) {
    Book b; b.var20 = Fixed(100); b.volume = 100; VolState v;
    volApplyToBook(v, b); CHECK_EQ(b.var20, Fixed(100));
}

TEST(mechanics, duplicate_conclusions_are_rejected_without_side_effects) {
    auto st = mechanicsWorld(); st.plot.committedConclusions.push_back(0);
    const auto before = serializeState(st);
    CHECK(!commitConclusion(st, 0, nullptr)); CHECK_EQ(serializeState(st), before);
}

TEST(mechanics, schema2_fixture_migrates_without_losing_pending_or_futures) {
    std::ifstream file(std::string(GF_TEST_SOURCE_DIR) + "/data/schema2-mechanics.bin", std::ios::binary);
    CHECK(file.good()); if (!file.good()) return;
    std::vector<u8> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    GameState st; CHECK(tryDeserializeState(bytes, st)); CHECK_EQ(st.schemaVersion, static_cast<u32>(kSchemaVersion)); CHECK_EQ(st.tick, 17u);
    CHECK(migration::canUpgrade(2)); CHECK_EQ(st.pending.items.size(), 1u);
    if (!st.pending.empty()) CHECK_EQ(st.pending.items[0].kind, ChoiceKind::Faction);
    CHECK_EQ(st.market.futuresPositions.size(), 1u);
    if (!st.market.futuresPositions.empty()) {
        CHECK_EQ(st.market.futuresPositions[0].owner, 0u); CHECK_EQ(st.market.futuresPositions[0].expiryTick, 23u);
    }
    GameState again; CHECK(tryDeserializeState(serializeState(st), again));
    CHECK_EQ(serializeState(st), serializeState(again));
}

TEST(mechanics, ship_costs_all_resources_and_pauses_in_captured_shipyard) {
    auto st = mechanicsWorld(); auto& e = st.player();
    e.stock.fill(Fixed(10000)); e.treasury = Fixed(1000000);
    auto& design = e.designs.front(); design.buildCost.fill(0);
    design.buildCost[static_cast<u8>(Commodity::Alloys)] = 20;
    design.buildCost[static_cast<u8>(Commodity::Components)] = 30;
    auto before = e.stock;
    CHECK(enqueueShip(st, e.id, e.capital, design.id, nullptr));
    CHECK_EQ(e.stock[static_cast<u8>(Commodity::Components)], before[static_cast<u8>(Commodity::Components)] - Fixed(30));
    CHECK_EQ(e.stock[static_cast<u8>(Commodity::Alloys)], before[static_cast<u8>(Commodity::Alloys)] - Fixed(20));
    CHECK(!e.shipQueue.empty()); if (e.shipQueue.empty()) return;
    auto ticks = e.shipQueue.front().ticksLeft; st.map.systems[e.capital].owner = 1;
    constructionPhase(st); CHECK_EQ(e.shipQueue.front().ticksLeft, ticks);
}

TEST(mechanics, production_forecast_matches_economy_and_includes_buildings) {
    auto st = mechanicsTradeWorld(Fixed(1000)); constexpr auto c = static_cast<u8>(Commodity::Minerals);
    st.player().stock[c] = Fixed(120); st.player().demand[c] = Fixed(10);
    st.planets[0].development = Fixed(4);
    for (int b = 0; b < kBuildingCount; ++b)
        if (buildingInfo(b).effect == BuildingEffect::ProdMinerals) { st.planets[0].buildings.push_back(b); break; }
    auto expected = empireProduction(st, st.player(), c);
    auto before = st.player().stock[c]; economyPhase(st);
    CHECK_EQ(st.player().stock[c] - before + Fixed(10), expected);
}

TEST(mechanics, research_materials_advance_project_within_minimum_time) {
    TechState tech; CHECK(techStartProject(tech, 0, nullptr));
    tech.rate = Fixed(0); tech.progress[0] = Fixed(100000);
    auto bank = tech.progress[0]; auto done = techTickProject(tech, Fixed(0), tech.completed);
    CHECK(done.empty()); CHECK(tech.projectProgress.rawValue() > 0); CHECK(tech.progress[0] < bank);
    const auto minimum = techMinTicks(techInfo(0).tier);
    for (int t = 1; t < minimum - 1; ++t) CHECK(techTickProject(tech, Fixed(0), tech.completed).empty());
    CHECK(!techCompleted(tech, 0));
    for (int t = 0; t < 2; ++t) (void)techTickProject(tech, Fixed(0), tech.completed);
    CHECK(techCompleted(tech, 0));
}

TEST(mechanics, ai_research_prefers_focused_branch_at_same_tier) {
    auto st = mechanicsWorld(); auto& e = st.empires[1]; e.tech = TechState{};
    e.tech.focus.fill(Fixed(0)); e.tech.focus[static_cast<u8>(TechBranch::Computing)] = Fixed(3);
    st.tick = 2; aiResearchPhase(st);
    CHECK(e.tech.project != TechState::kNoTech);
    if (e.tech.project != TechState::kNoTech) CHECK_EQ(techInfo(e.tech.project).branch, TechBranch::Computing);
}

TEST(mechanics, election_waits_for_campaign_result) {
    auto st = mechanicsWorld(); auto& e = st.player(); e.government = 17;
    e.ruler.age = 40; e.ruler.termEnd = 10; st.tick = 10;
    const auto ruler = e.ruler.name; CHECK(isElective(17));
    governmentPhase(st); CHECK(e.gov.election.active);
    rulerPhase(st); CHECK_EQ(e.ruler.name, ruler); CHECK(e.gov.election.active);
    st.tick = e.gov.election.endTick; governmentPhase(st);
    CHECK(!e.gov.election.active); CHECK(e.ruler.termEnd > st.tick);
}

TEST(mechanics, higher_id_empire_can_attack_undefended_lower_id_territory) {
    auto st = mechanicsWorld(); const u32 system = st.player().capital;
    declareWar(st, 1, 0, true);
    for (auto& f : st.fleets) {
        if (f.owner == 0) f.org = Fixed(0);
        if (f.owner == 1) { f.system = system; f.targetSystem = kNoSystem; f.order = FleetOrder::Patrol; }
    }
    TickReport report; combatPhase(st, report);
    CHECK(std::any_of(st.battles.begin(), st.battles.end(), [&](const Battle& b) {
        return b.system == system && b.attacker == 1 && b.defender == 0;
    }));
}

TEST(mechanics, betrayal_military_threshold_uses_fractional_units) {
    auto st = mechanicsWorld(); st.player().stock.fill(Fixed(10000000));
    st.player().military = Fixed(100); st.empires[1].military = Fixed(80);
    auto ev = betrayalCalculus(st, 1, 0);
    CHECK(ev.total > ev.threshold); CHECK(ev.shouldBetray);
    st.empires[1].military = Fixed(60); CHECK(!betrayalCalculus(st, 1, 0).shouldBetray);
}

TEST(mechanics, requote_never_consumes_real_orders_without_settlement) {
    auto st = mechanicsWorld(); constexpr auto c = static_cast<u8>(Commodity::Alloys);
    auto& b = mechanicsEmptyBook(st, c);
    b.orders.push_back(mechanicsOrder(999999, 0, true, 110, 2)); bookRebuildLevels(b);
    auto stock = st.player().stock[c]; auto cash = st.market.margin.cash;
    marketMakerRequote(st);
    const auto* rest = bookFind(b, 999999);
    i64 filled = rest == nullptr ? 2 : rest->filled;
    CHECK_EQ(st.player().stock[c] - stock, Fixed(filled));
    if (filled > 0) CHECK(st.market.margin.cash < cash);
}

TEST(mechanics, ai_futures_do_not_change_inferred_player_risk) {
    auto st = mechanicsWorld(); Observable obs;
    const auto risk = toModelInferRisk(st, obs);
    ToModel before, after; toModelUpdateHabits(before, obs, st);
    FuturesPosition ai; ai.owner = 1; ai.qty = 100; ai.leverage = 20;
    st.market.futuresPositions.push_back(ai);
    CHECK_EQ(toModelInferRisk(st, obs), risk);
    toModelUpdateHabits(after, obs, st); CHECK_EQ(before.habits, after.habits);
}

TEST(mechanics, futures_close_spans_player_batches_without_touching_ai) {
    auto st = mechanicsWorld(); constexpr auto c = static_cast<u8>(Commodity::Alloys);
    const auto cash = st.market.margin.cash;
    CHECK(futuresOpen(st, c, 1, 3, false, 1, nullptr));
    st.tick = 4; CHECK(futuresOpen(st, c, 1, 4, false, 1, nullptr));
    CHECK(futuresOpen(st, 1, c, 1, 2, false, 1, nullptr));
    CHECK(futuresClose(st, c, 1, 7, nullptr));
    CHECK_EQ(st.market.margin.cash, cash); CHECK_EQ(st.market.futuresPositions.size(), 1u);
    if (st.market.futuresPositions.size() == 1) {
        CHECK_EQ(st.market.futuresPositions.front().owner, 1u);
        CHECK_EQ(st.market.futuresPositions.front().qty, 2);
    }
}
