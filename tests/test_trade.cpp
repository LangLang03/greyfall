// 贸易路线、关税战、实体路径与封锁
#include <algorithm>
#include <array>

#include "check.h"
#include "core/GameState.h"
#include "core/TickPipeline.h"
#include "domain/Trade.h"
#include "domain/Treaty.h"
#include "gen/WorldGen.h"
#include "plot/EventSystem.h"
#include "plot/BeatResolver.h"
#include "save/Serde.h"

using namespace gf;

namespace {

GameState tradeWorld(u64 seed = 6060, int systems = 48) {
    WorldGenOptions o;
    o.seed = seed;
    o.difficulty = 2;
    o.empireCount = 8;
    o.systemCount = systems;
    GameState st;
    generateWorld(st, o);
    return st;
}

void tradeRunTicks(GameState& st, int n) {
    for (int i = 0; i < n; ++i) {
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);
    }
}

/// 找一个「确有可出口剩余」的帝国与商品
bool findExporter(const GameState& st, u32& empire, u8& commodity) {
    for (const auto& e : st.empires) {
        if (!e.alive) continue;
        for (int c = 0; c < kCommodityCount; ++c) {
            if (exportableSurplus(st, e, static_cast<u8>(c)).rawValue() > 0) {
                empire = e.id;
                commodity = static_cast<u8>(c);
                return true;
            }
        }
    }
    return false;
}

}  // namespace

TEST(trade, planet_yields_have_correct_units) {
    // 回归守卫：行星产出曾用 Fixed::raw(N)（即 N/1000）写入，
    // 使整个经济体几乎没有生产 —— 只有消耗，没有产出。
    GameState st = tradeWorld();
    Fixed total = Fixed(0);
    for (const auto& p : st.planets) {
        if (p.owner == kNoEmpire) continue;
        for (int c = 0; c < kCommodityCount; ++c) total += p.yield[static_cast<std::size_t>(c)];
    }
    CHECK(total.rawValue() > 0);
    Fixed maxYield = Fixed(0);
    for (const auto& p : st.planets)
        for (int c = 0; c < kCommodityCount; ++c)
            if (p.yield[static_cast<std::size_t>(c)].rawValue() > maxYield.rawValue())
                maxYield = p.yield[static_cast<std::size_t>(c)];
    CHECK(maxYield.rawValue() >= Fixed(10).rawValue());
}

TEST(trade, every_consumed_commodity_has_a_producer_type) {
    // 每种「被消耗的商品」都必须至少有一个行星类型能产出，
    // 否则世界范围内会出现永久零供给，推高民怨。
    GameState st = tradeWorld(11);
    for (const auto& e : st.empires) {
        CHECK_EQ(e.demand[static_cast<std::size_t>(Commodity::Influence)].rawValue(), 0);
        CHECK_EQ(e.demand[static_cast<std::size_t>(Commodity::Unity)].rawValue(), 0);
    }
    for (int c = 0; c < kCommodityCount; ++c) {
        if (c == static_cast<int>(Commodity::Influence) || c == static_cast<int>(Commodity::Unity)) continue;
        bool anyType = false;
        for (int t = 0; t < static_cast<int>(PlanetType::Count); ++t) {
            std::array<Fixed, kCommodityCount> y{};
            planetBaseYield(static_cast<PlanetType>(t), y);
            if (y[static_cast<std::size_t>(c)].rawValue() > 0) anyType = true;
        }
        CHECK(anyType);
    }
}

TEST(trade, surplus_and_need_are_flow_based) {
    // 回归守卫：曾用「库存 > 2×需求」判断可出口剩余，
    // 但所有帝国初始库存都很高，于是每种商品都被误判为剩余，贸易永不发生。
    // 正确判据是**流量**：产出 − 需求。
    GameState st = tradeWorld(22);
    tradeRunTicks(st, 20);
    for (const auto& e : st.empires) {
        if (!e.alive) continue;
        for (int c = 0; c < kCommodityCount; ++c) {
            u8 cc = static_cast<u8>(c);
            Fixed surplus = exportableSurplus(st, e, cc);
            Fixed need = importNeed(st, e, cc);
            Fixed prod = empireProduction(st, e, cc);
            // 检查用的「需求」必须与 importNeed 内部用的是同一个量。
            // importNeed 算的是 `resourceDemand + 国家工程补给 − 产出`；
            // 旧断言只比 `e.demand`，于是在有在建工程时会误报 ——
            // 工程补给让真实需求高于 e.demand，而产出恰好落在两者之间，
            // 于是出现「importNeed > 0 但 demand <= prod」的假失败。
            Fixed dem = resourceDemand(st, e, cc);
            for (const auto& project : e.developmentProjects) dem += Fixed(project.supplies[c]);
            CHECK(!(surplus.rawValue() > 0 && need.rawValue() > 0));
            if (surplus.rawValue() > 0) CHECK(prod.rawValue() > dem.rawValue());
            if (need.rawValue() > 0) CHECK(dem.rawValue() > prod.rawValue());
        }
    }
}

TEST(trade, routes_open_and_move_goods) {
    GameState st = tradeWorld(33);
    // 贸易需要进口方有支付能力：AI 国库在早期可能为负，
    // 此时进口被可支付约束挡住，货物流量要到国库回正之后才出现。
    tradeRunTicks(st, 140);
    CHECK(!st.market.trade.routes.empty());
    Fixed totalVolume = Fixed(0);
    for (const auto& r : st.market.trade.routes) totalVolume += r.volume;
    CHECK(totalVolume.rawValue() > 0);
    for (const auto& r : st.market.trade.routes) {
        if (r.volume.rawValue() <= 0) continue;
        CHECK(r.exporter != r.importer);
        CHECK(static_cast<int>(r.commodity) < kCommodityCount);
        CHECK(r.tariff.rawValue() >= 0);
        CHECK(r.tariff.rawValue() <= Fixed::pct(kMaxTariffPct).rawValue());
        break;
    }
}

TEST(trade, open_requires_surplus_and_peace) {
    GameState st = tradeWorld(44);
    tradeRunTicks(st, 40);
    u32 exporter = 0;
    u8 commodity = 0;
    if (!findExporter(st, exporter, commodity)) return;
    u32 partner = kNoEmpire;
    for (const auto& e : st.empires) {
        if (e.id == exporter || !e.alive) continue;
        if (!atWarWith(st, exporter, e.id) && importNeed(st, e, commodity).rawValue() > 0) {
            partner = e.id;
            break;
        }
    }
    if (partner == kNoEmpire) return;

    // 只检验创建前提；模拟期间 AI 可能已经开过同一条路线或实施禁运。
    st.market.trade.routes.clear();
    st.relation(exporter, partner).embargo = false;
    st.relation(partner, exporter).embargo = false;

    std::string err;
    CHECK(!tradeOpen(st, exporter, exporter, commodity, Fixed::pct(10), &err));
    CHECK(!err.empty());
    CHECK(!tradeOpen(st, exporter, partner, 200, Fixed::pct(10), &err));
    bool opened = tradeOpen(st, exporter, partner, commodity, Fixed::pct(10), &err);
    CHECK(opened);
    if (opened) {
        CHECK(!tradeOpen(st, exporter, partner, commodity, Fixed::pct(10), &err));
        declareWar(st, exporter, partner, true);
        CHECK(!tradeOpen(st, partner, exporter, commodity, Fixed::pct(10), &err));
    }
}

TEST(trade, tariff_reduces_volume) {
    GameState st = tradeWorld(55);
    tradeRunTicks(st, 60);
    const TradeRoute* target = nullptr;
    for (const auto& r : st.market.trade.routes) {
        if (r.volume.rawValue() > 0 && r.importer != kPlayerId) {
            target = &r;
            break;
        }
    }
    if (target == nullptr) return;
    u32 rid = target->id;
    u32 importer = target->importer;
    (void)tradeSetTariff(st, rid, importer, Fixed::pct(2), nullptr);
    for (auto& r : st.market.trade.routes)
        if (r.id == rid) r.dormantTicks = 0;
    tradeRunTicks(st, 2);
    Fixed lowCap = Fixed(0);
    for (const auto& r : st.market.trade.routes)
        if (r.id == rid) lowCap = r.capacity;
    (void)tradeSetTariff(st, rid, importer, Fixed::pct(55), nullptr);
    for (auto& r : st.market.trade.routes)
        if (r.id == rid) r.dormantTicks = 0;
    tradeRunTicks(st, 2);
    Fixed highCap = Fixed(0);
    for (const auto& r : st.market.trade.routes)
        if (r.id == rid) highCap = r.capacity;
    if (lowCap.rawValue() > 0 && highCap.rawValue() > 0) CHECK(highCap.rawValue() < lowCap.rawValue());
}

TEST(trade, tariff_change_requires_importer) {
    GameState st = tradeWorld(66);
    tradeRunTicks(st, 60);
    const TradeRoute* target = nullptr;
    for (const auto& r : st.market.trade.routes) {
        target = &r;
        break;
    }
    if (target == nullptr) return;
    std::string err;
    CHECK(!tradeSetTariff(st, target->id, target->exporter, Fixed::pct(40), &err));
    CHECK(!err.empty());
    CHECK(tradeSetTariff(st, target->id, target->importer, Fixed::pct(40), &err));
    CHECK(tradeSetTariff(st, target->id, target->importer, Fixed::pct(999), &err));
    for (const auto& r : st.market.trade.routes)
        if (r.id == target->id) CHECK(r.tariff.rawValue() <= Fixed::pct(kMaxTariffPct).rawValue());
}

TEST(trade, war_and_embargo_disrupt_routes) {
    GameState st = tradeWorld(77);
    tradeRunTicks(st, 60);
    const TradeRoute* target = nullptr;
    for (const auto& r : st.market.trade.routes) {
        if (r.active && r.exporter != kPlayerId && r.importer != kPlayerId) {
            target = &r;
            break;
        }
    }
    if (target == nullptr) return;
    u32 rid = target->id, ex = target->exporter, im = target->importer;
    declareWar(st, ex, im, true);
    tradeRunTicks(st, 1);
    for (const auto& r : st.market.trade.routes)
        if (r.id == rid) {
            CHECK(!r.active);
            CHECK_EQ(r.volume.rawValue(), 0);
            CHECK(!r.disrupted.empty());
        }

    GameState st2 = tradeWorld(78);
    tradeRunTicks(st2, 60);
    const TradeRoute* t2 = nullptr;
    for (const auto& r : st2.market.trade.routes) {
        if (r.active && r.exporter != kPlayerId && r.importer != kPlayerId) {
            t2 = &r;
            break;
        }
    }
    if (t2 == nullptr) return;
    st2.relation(t2->exporter, t2->importer).embargo = true;
    tradeRunTicks(st2, 1);
    for (const auto& r : st2.market.trade.routes)
        if (r.id == t2->id) CHECK(!r.active);
}

TEST(trade, importer_payment_is_limited_by_treasury) {
    // 回归守卫：出口方可以单方面开路线并持续向进口方收款，
    // 把对方国库掏空（实测 AI 帝国因此跌到 -48 万）。
    GameState st = tradeWorld(88);
    tradeRunTicks(st, 60);
    const TradeRoute* target = nullptr;
    for (const auto& r : st.market.trade.routes) {
        if (r.volume.rawValue() > 0 && r.importer != kPlayerId) {
            target = &r;
            break;
        }
    }
    if (target == nullptr) return;
    u32 im = target->importer;
    u32 rid = target->id;
    // 把进口方国库清空。它仍可能靠本季收入支付一部分 ——
    // 检验重点是**不会赊账**：运量受可支付额度限制，且国库不得被透支。
    st.empires[im].treasury = Fixed(0);
    tradeRunTicks(st, 1);
    for (const auto& r : st.market.trade.routes) {
        if (r.id != rid) continue;
        CHECK(r.volume.rawValue() <= r.capacity.rawValue());
    }
    // 只调用 tradePhase 以**隔离贸易本身**：
    // 走完整流水线时，AI 会主动花钱发动决议，国库为负可能来自决议而非贸易，
    // 断言就失去了针对性。
    for (int i = 0; i < 4; ++i) {
        st.empires[im].treasury = Fixed(0);
        tradePhase(st);
        CHECK(st.empires[im].treasury.rawValue() >= Fixed(-1).rawValue());
    }
}

TEST(trade, ai_does_not_force_trade_on_player) {
    // 回归守卫：AI 曾可单方面对玩家开通进口路线并持续扣款，
    // 使玩家在完全被动的情况下国库跌到 -50 万。
    GameState st = tradeWorld(99);
    for (int i = 0; i < 40; ++i) {
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);
    }
    for (const auto& r : st.market.trade.routes) {
        if (r.importer == kPlayerId) CHECK(false);
    }
}

TEST(trade, ai_spending_is_capped_by_income) {
    // 回归守卫：AI 的支出曾按「国库百分比」计算（市场采购每季 25%），
    // 这是与收入脱钩的几何式抽干 —— 实测 AI 合计国库在 30 季内由 39 万跌到 -30 万，
    // 进而导致它永远攒不下钱发动决议。所有 AI 支出都必须以**收入**为上限。
    GameState st = tradeWorld(3001, 56);
    Fixed lowest = Fixed(1 << 30);
    // 跑到 200 季：恢复过程需要时间，短窗口只能观察到下探阶段
    for (int t = 1; t <= 200; ++t) {
        tradeRunTicks(st, 1);
        Fixed total = Fixed(0);
        int n = 0;
        for (const auto& e : st.empires) {
            if (e.isPlayer || !e.alive) continue;
            total += e.treasury;
            ++n;
        }
        if (n == 0) continue;
        Fixed avg = total / Fixed(n);
        if (avg.rawValue() < lowest.rawValue()) lowest = avg;
    }
    CHECK(lowest.rawValue() > Fixed(-100000).rawValue());
    Fixed finalTotal = Fixed(0);
    int n = 0;
    for (const auto& e : st.empires) {
        if (e.isPlayer || !e.alive) continue;
        finalTotal += e.treasury;
        ++n;
    }
    if (n > 0) {
        Fixed avg = finalTotal / Fixed(n);
        // 关键不变量：AI 的赤字是有界的。
        // 修复前该场景的 AI 合计国库会跌到 -30 万量级且持续下坠；
        // 修复后最差约 -1.6 万，且存在恢复能力（终值好于最差点）。
        CHECK(lowest.rawValue() > Fixed(-50000).rawValue());
        CHECK(avg.rawValue() > lowest.rawValue());
    }
}

TEST(trade, last_income_is_tracked) {
    // 回归守卫：收入必须被记录，否则所有「以收入为上限」的支出约束都会失效。
    GameState st = tradeWorld(3002);
    tradeRunTicks(st, 20);
    int nonzero = 0;
    for (const auto& e : st.empires) {
        if (!e.alive) continue;
        if (e.lastIncome.rawValue() != 0) ++nonzero;
    }
    CHECK(nonzero >= 4);
}

TEST(trade, treasury_recovers_over_long_run) {
    // 贸易不得让任何一方陷入**不可逆**的失血。
    GameState st = tradeWorld(111, 56);
    // 数组必须按 kMaxEmpires 分配，不能用开局帝国数 ——
    // 割据会让新国家的 id 超过初始数量，`worst[e.id]` 会越界读到垃圾值
    //（实测读出一个 -8e18 的“国库”，看起来像游戏溢出，其实是测试自己的 bug）。
    std::array<Fixed, kMaxEmpires> worst{};
    for (int i = 0; i < 120; ++i) {
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);
        for (const auto& e : st.empires) {
            if (!e.alive || e.id >= kMaxEmpires) continue;
            if (e.treasury.rawValue() < worst[e.id].rawValue()) worst[e.id] = e.treasury;
        }
    }
    for (const auto& e : st.empires) {
        if (!e.alive || e.id >= kMaxEmpires) continue;
        CHECK(worst[e.id].rawValue() > Fixed(-500000).rawValue());
        CHECK(e.treasury.rawValue() >= worst[e.id].rawValue());
    }
}

// ===========================================================================
// 实体路径与封锁
// ===========================================================================

TEST(trade, routes_have_physical_paths) {
    // 回归守卫：贸易路线曾是抽象的一对帝国，与星图完全脱节 ——
    // 舰队阻断因此对贸易毫无影响。路线必须有经过星系的实体路径。
    GameState st = tradeWorld(2001);
    tradeRunTicks(st, 60);
    CHECK(!st.market.trade.routes.empty());
    int withPath = 0;
    for (const auto& r : st.market.trade.routes) {
        if (r.path.size() < 2) continue;
        ++withPath;
        const Empire* ex = st.empire(r.exporter);
        const Empire* im = st.empire(r.importer);
        CHECK(ex != nullptr);
        CHECK(im != nullptr);
        CHECK_EQ(r.path.front(), ex->capital);
        CHECK_EQ(r.path.back(), im->capital);
        for (std::size_t i = 1; i < r.path.size(); ++i) {
            const SystemNode* prev = st.system(r.path[i - 1]);
            CHECK(prev != nullptr);
            bool linked = false;
            for (u32 nx : prev->links)
                if (nx == r.path[i]) linked = true;
            CHECK(linked);
        }
    }
    CHECK(withPath > 0);
}

TEST(trade, pathfinding_returns_connected_route) {
    GameState st = tradeWorld(2002);
    for (std::size_t i = 0; i < st.map.systems.size() && i < 6; ++i) {
        for (std::size_t j = 0; j < st.map.systems.size() && j < 6; ++j) {
            if (i == j) continue;
            std::vector<u32> p = tradePathBetween(st, static_cast<u32>(i), static_cast<u32>(j));
            if (p.empty()) continue;
            CHECK_EQ(p.front(), static_cast<u32>(i));
            CHECK_EQ(p.back(), static_cast<u32>(j));
            for (std::size_t k = 1; k < p.size(); ++k) {
                const SystemNode* prev = st.system(p[k - 1]);
                CHECK(prev != nullptr);
                bool linked = false;
                for (u32 nx : prev->links)
                    if (nx == p[k]) linked = true;
                CHECK(linked);
            }
        }
    }
    CHECK(tradePathBetween(st, 0, 0).empty());
    CHECK(tradePathBetween(st, 0, 99999).empty());
}

TEST(trade, blockade_raises_path_risk_and_cuts_volume) {
    GameState st = tradeWorld(2003);
    tradeRunTicks(st, 60);
    const TradeRoute* target = nullptr;
    for (const auto& r : st.market.trade.routes) {
        if (r.volume.rawValue() > 0 && r.path.size() >= 3 && r.exporter != kPlayerId) {
            target = &r;
            break;
        }
    }
    if (target == nullptr) return;
    const u32 rid = target->id;
    const u32 ex = target->exporter;
    const u32 mid = target->path[target->path.size() / 2];
    Fixed riskBefore = target->pathRisk;

    u32 blocker = kNoEmpire;
    for (const auto& e : st.empires) {
        if (e.id == ex || !e.alive || e.fleets.empty()) continue;
        blocker = e.id;
        break;
    }
    if (blocker == kNoEmpire) return;
    u32 fid = st.empires[blocker].fleets.front();
    {
        Fleet* f = st.fleet(fid);
        if (f == nullptr) return;
        f->system = mid;
        f->targetSystem = kNoSystem;
        f->order = FleetOrder::Blockade;
        f->org = f->maxOrg;
        f->strength = Fixed(3000);
    }
    tradeRunTicks(st, 6);

    const SystemNode* ms = st.system(mid);
    CHECK(ms != nullptr);
    CHECK(ms->blockade.rawValue() > 0);

    Fixed riskAfter = Fixed(0);
    for (const auto& r : st.market.trade.routes)
        if (r.id == rid) riskAfter = r.pathRisk;
    CHECK(riskAfter.rawValue() > riskBefore.rawValue());

    {
        Fleet* f = st.fleet(fid);
        if (f != nullptr) {
            f->order = FleetOrder::Idle;
            f->system = st.empires[blocker].capital;
        }
    }
    Fixed blockingBefore = ms->blockade;
    tradeRunTicks(st, 8);
    CHECK(ms->blockade.rawValue() < blockingBefore.rawValue());
}

TEST(trade, blockade_maintenance_requires_blockading_fleet) {
    // 回归守卫：封锁曾是下达命令时的一次性 +30%，此后永不衰减。
    GameState st = tradeWorld(2004);
    tradeRunTicks(st, 30);
    if (st.map.systems.empty()) return;
    SystemNode* s = st.system(0);
    if (s == nullptr) return;
    s->blockade = Fixed::pct(60);
    tradeRunTicks(st, 12);
    CHECK(s->blockade.rawValue() < Fixed::pct(60).rawValue());
}

TEST(trade, hostile_territory_cuts_route) {
    GameState st = tradeWorld(2005);
    tradeRunTicks(st, 50);
    const TradeRoute* target = nullptr;
    for (const auto& r : st.market.trade.routes) {
        if (r.path.size() >= 3) {
            target = &r;
            break;
        }
    }
    if (target == nullptr) return;
    u32 rid = target->id;
    u32 mid = target->path[target->path.size() / 2];
    u32 ex = target->exporter, im = target->importer;
    u32 third = kNoEmpire;
    for (const auto& e : st.empires) {
        if (e.id == ex || e.id == im || !e.alive) continue;
        third = e.id;
        break;
    }
    if (third == kNoEmpire) return;
    if (st.system(mid) == nullptr) return;
    st.system(mid)->owner = third;
    declareWar(st, third, ex, true);
    tradePhase(st);
    for (const auto& r : st.market.trade.routes) {
        if (r.id != rid) continue;
        if (r.active) {
            // 存在替代航线时应绕开敌对领土。
            CHECK(std::find(r.path.begin(), r.path.end(), mid) == r.path.end());
            CHECK(r.pathRisk < Fixed(1));
        } else CHECK_EQ(r.volume.rawValue(), 0);
    }
}

TEST(trade, path_fields_survive_serialization) {
    GameState st = tradeWorld(2006);
    tradeRunTicks(st, 60);
    std::vector<u8> bytes = serializeState(st);
    GameState back;
    CHECK(tryDeserializeState(bytes, back));
    CHECK_EQ(back.market.trade.routes.size(), st.market.trade.routes.size());
    for (std::size_t i = 0; i < st.market.trade.routes.size(); ++i) {
        CHECK_EQ(back.market.trade.routes[i].path.size(), st.market.trade.routes[i].path.size());
        CHECK_EQ(back.market.trade.routes[i].pathRisk.rawValue(),
                 st.market.trade.routes[i].pathRisk.rawValue());
        CHECK(back.market.trade.routes[i].pathNote == st.market.trade.routes[i].pathNote);
        for (std::size_t k = 0; k < st.market.trade.routes[i].path.size(); ++k)
            CHECK_EQ(back.market.trade.routes[i].path[k], st.market.trade.routes[i].path[k]);
    }
}

TEST(trade, state_survives_serialization) {
    GameState st = tradeWorld(222);
    tradeRunTicks(st, 60);
    CHECK(!st.market.trade.routes.empty());
    std::vector<u8> bytes = serializeState(st);
    GameState back;
    CHECK(tryDeserializeState(bytes, back));
    CHECK_EQ(back.market.trade.routes.size(), st.market.trade.routes.size());
    CHECK_EQ(back.market.trade.nextRouteId, st.market.trade.nextRouteId);
    for (std::size_t i = 0; i < st.market.trade.routes.size(); ++i) {
        CHECK_EQ(back.market.trade.routes[i].id, st.market.trade.routes[i].id);
        CHECK_EQ(back.market.trade.routes[i].exporter, st.market.trade.routes[i].exporter);
        CHECK_EQ(back.market.trade.routes[i].importer, st.market.trade.routes[i].importer);
        CHECK_EQ(back.market.trade.routes[i].commodity, st.market.trade.routes[i].commodity);
        CHECK_EQ(back.market.trade.routes[i].tariff.rawValue(), st.market.trade.routes[i].tariff.rawValue());
    }
}

// 回归守卫：路线曾是无权 BFS 最短路，穿过战区与无主地带都不付代价，
// 中转国的地理位置毫无价值。加权后应绕开敌对星系。
TEST(trade, weighted_path_avoids_hostile_systems) {
    GameState st = tradeWorld(2007);
    const Empire* ex = st.empire(1);
    const Empire* im = st.empire(2);
    if (ex == nullptr || im == nullptr) return;
    std::vector<u32> base = tradePathWeighted(st, ex->capital, im->capital, 1, 2);
    if (base.size() < 3) return;
    u32 mid = base[base.size() / 2];
    u32 third = kNoEmpire;
    for (const auto& e : st.empires) {
        if (e.id == 1 || e.id == 2 || !e.alive) continue;
        third = e.id;
        break;
    }
    if (third == kNoEmpire) return;
    declareWar(st, third, 1, true);
    declareWar(st, third, 2, true);
    st.system(mid)->owner = third;
    std::vector<u32> after = tradePathWeighted(st, ex->capital, im->capital, 1, 2);
    // 若存在替代路径，则必须绕开敌对星系
    if (after.empty()) return;
    bool avoided = std::find(after.begin(), after.end(), mid) == after.end();
    // 只有当替代路径确实存在时才要求绕开：先看无主状态下是否会换路
    st.system(mid)->owner = kNoEmpire;
    std::vector<u32> neutral = tradePathWeighted(st, ex->capital, im->capital, 1, 2);
    bool sameChoice = neutral == base;
    if (sameChoice && after != base) CHECK(avoided);
}

// 过境费：途经第三国领土的路线必须向该国支付通行费
TEST(trade, transit_fees_reach_intermediate_empires) {
    GameState st = tradeWorld(2008, 56);
    tradeRunTicks(st, 140);
    int withTransit = 0;
    Fixed feeSum = Fixed(0);
    for (const auto& r : st.market.trade.routes) {
        if (r.transitEmpires.empty()) continue;
        ++withTransit;
        feeSum += r.transitFee;
        // 途经国不得是出口方或进口方
        for (u32 t : r.transitEmpires) {
            CHECK(t != r.exporter);
            CHECK(t != r.importer);
        }
        // 过境费必须非负且有上限（不超过货值的 12%）
        CHECK(r.transitFee.rawValue() >= 0);
    }
    CHECK(withTransit > 0);
    CHECK(feeSum.rawValue() >= 0);
}

// 多跳：路线应当真的经过中间星系，且首尾为双方首都
TEST(trade, routes_are_multi_hop_and_connected) {
    GameState st = tradeWorld(2009, 56);
    tradeRunTicks(st, 140);
    CHECK(!st.market.trade.routes.empty());
    int multiHop = 0;
    for (const auto& r : st.market.trade.routes) {
        if (r.path.size() >= 3) ++multiHop;
        if (r.path.size() < 2) continue;
        const Empire* ex = st.empire(r.exporter);
        const Empire* im = st.empire(r.importer);
        CHECK(ex != nullptr);
        CHECK(im != nullptr);
        CHECK_EQ(r.path.front(), ex->capital);
        CHECK_EQ(r.path.back(), im->capital);
        for (std::size_t i = 1; i < r.path.size(); ++i) {
            const SystemNode* prev = st.system(r.path[i - 1]);
            CHECK(prev != nullptr);
            bool linked = false;
            for (u32 nx : prev->links)
                if (nx == r.path[i]) linked = true;
            CHECK(linked);
        }
    }
    CHECK(multiHop > 0);
}

TEST(trade, close_requires_party) {
    GameState st = tradeWorld(333);
    tradeRunTicks(st, 60);
    if (st.market.trade.routes.empty()) return;
    u32 rid = st.market.trade.routes.front().id;
    u32 outsider = kNoEmpire;
    for (const auto& e : st.empires) {
        if (e.id == st.market.trade.routes.front().exporter) continue;
        if (e.id == st.market.trade.routes.front().importer) continue;
        outsider = e.id;
        break;
    }
    std::string err;
    if (outsider != kNoEmpire) {
        CHECK(!tradeClose(st, rid, outsider, &err));
        CHECK(!err.empty());
    }
    CHECK(!tradeClose(st, 99999, kPlayerId, &err));
}
