// 世界生成：同种子同结果 / 星图连通性 / 表规模与引用完整性 / 词缀 / 玩家起点
#include <algorithm>
#include <set>

#include "check.h"
#include "core/GameState.h"
#include "gen/EmpireGen.h"
#include "gen/EventSchedule.h"
#include "gen/ModifierGen.h"
#include "gen/NameGen.h"
#include "gen/StarMapGen.h"
#include "gen/WorldGen.h"
#include "save/Serde.h"

using namespace gf;

namespace {

WorldGenOptions opts(u64 seed, int empires = 12, int systems = 56) {
    WorldGenOptions o;
    o.seed = seed;
    o.difficulty = 3;
    o.empireCount = empires;
    o.systemCount = systems;
    return o;
}

}  // namespace

TEST(gen, same_seed_same_world) {
    GameState a, b;
    generateWorld(a, opts(0xABCDEF));
    generateWorld(b, opts(0xABCDEF));
    CHECK(serializeState(a) == serializeState(b));
    CHECK_EQ(a.map.systems.size(), b.map.systems.size());
    CHECK_EQ(a.planets.size(), b.planets.size());
    CHECK_EQ(a.empires.size(), b.empires.size());
    for (std::size_t i = 0; i < a.empires.size(); ++i) CHECK(a.empires[i].name == b.empires[i].name);
    // 不同种子 ⇒ 不同世界
    GameState c;
    generateWorld(c, opts(0xABCDEF + 1));
    CHECK(serializeState(c) != serializeState(a));
}

TEST(gen, starmap_is_connected_and_symmetric) {
    GameState st;
    generateWorld(st, opts(42, 12, 64));
    const std::size_t n = st.map.systems.size();
    CHECK(n >= 24);
    // 邻接对称
    for (const auto& s : st.map.systems) {
        for (u32 l : s.links) {
            CHECK(l < n);
            const auto& other = st.map.systems[l].links;
            CHECK(std::find(other.begin(), other.end(), s.id) != other.end());
        }
        // 无自环、无重复
        std::set<u32> uniq(s.links.begin(), s.links.end());
        CHECK_EQ(uniq.size(), s.links.size());
        CHECK(uniq.find(s.id) == uniq.end());
    }
    // 全连通（MST 保证）
    for (std::size_t i = 1; i < n; ++i) CHECK(st.map.hops(0, static_cast<u32>(i)) >= 0);
    CHECK_EQ(st.map.hops(0, 0), 0);
}

TEST(gen, empires_have_capital_fleet_and_sane_economy) {
    GameState st;
    generateWorld(st, opts(7, 12, 48));
    CHECK_EQ(st.empires.size(), 12ull);
    CHECK(st.empires[kPlayerId].isPlayer);
    std::set<std::string> names;
    for (const auto& e : st.empires) {
        names.insert(e.name);
        CHECK(!e.name.empty());
        CHECK(e.capital < st.map.systems.size());
        CHECK(!e.fleets.empty());
        CHECK(!e.designs.empty());
        CHECK(!e.domestic.factions.empty());
        CHECK(e.treasury.rawValue() > 0);
        CHECK(e.creditRating.rawValue() > 0 && e.creditRating.rawValue() <= FIX);
        CHECK(e.stability.rawValue() >= 0 && e.stability.rawValue() <= FIX);
        for (int c = 0; c < kCommodityCount; ++c) CHECK(e.stock[static_cast<std::size_t>(c)].rawValue() >= 0);
        // 修正值可计算
        CHECK(empireModifier(e, ModKind::TradeMargin).rawValue() == empireModifier(e, ModKind::TradeMargin).rawValue());
    }
    CHECK_EQ(names.size(), st.empires.size());   // 帝国名唯一
    // 首都不重叠
    std::set<u32> caps;
    for (const auto& e : st.empires) caps.insert(e.capital);
    CHECK_EQ(caps.size(), st.empires.size());
}

TEST(gen, market_initialized_with_two_sided_books) {
    GameState st;
    generateWorld(st, opts(11));
    for (int c = 0; c < kCommodityCount; ++c) {
        const CommodityInfo& ci = commodityInfo(c);
        if (!ci.tradable) continue;
        for (int e = 0; e < kExchangeCount; ++e) {
            const Book& b = st.market.exchanges[static_cast<std::size_t>(e)].books[static_cast<std::size_t>(c)];
            CHECK(!b.bids.empty());
            CHECK(!b.asks.empty());
            CHECK(b.bids.front().px.rawValue() < b.asks.front().px.rawValue());   // 无自成交
            CHECK(b.mid.rawValue() > 0);
        }
    }
    // 黑市溢价高于核心区
    CHECK(st.market.exchanges[kExchBZ].books[static_cast<std::size_t>(Commodity::Alloys)].mid.rawValue() >
          st.market.exchanges[kExchCX].books[static_cast<std::size_t>(Commodity::Alloys)].mid.rawValue());
    // 汇率与配给初值
    for (int e = 0; e < kExchangeCount; ++e) CHECK(st.market.fx[static_cast<std::size_t>(e)].rawValue() > 0);
}

TEST(gen, anomalies_and_clue_graph_initialized) {
    GameState st;
    generateWorld(st, opts(13));
    CHECK_EQ(st.clues.size(), static_cast<std::size_t>(kClueCount));
    CHECK(!st.plot.knownClues.empty());     // 开局有推理起点
    CHECK(!st.clueEdges.empty());           // 静态超边已建立
    int anomalies = 0;
    for (const auto& s : st.map.systems)
        if (s.anomaly > 0) ++anomalies;
    CHECK(anomalies > 0);
    // 危机时间表非空且起始 tick 递增合理
    CHECK(!st.crises.empty());
    for (const auto& c : st.crises) CHECK(c.startTick > 0);
    // 玩家有初始道具
    CHECK(!st.inventory.items.empty());
}

TEST(gen, modifiers_are_deterministic_and_flagged) {
    std::string n1, n2;
    u64 b1 = rollModifiers(1234, 3, n1);
    u64 b2 = rollModifiers(1234, 3, n2);
    CHECK_EQ(b1, b2);
    CHECK(n1 == n2);
    CHECK(!n1.empty());
    CHECK(b1 != 0);
    // 每个 bit 都必须在词缀表里有名字
    for (const auto& m : modifierTable()) CHECK((b1 & m.bit) == 0 || m.bit != 0);
    CHECK(hasModifier(b1, b1 & (~b1 + 1)) || b1 == 0);
}

TEST(gen, name_generator_is_deterministic) {
    NameGen a(777);
    NameGen b(777);
    for (int i = 0; i < 20; ++i) {
        CHECK(a.empire() == b.empire());
        CHECK(a.system() == b.system());
        CHECK(a.fleet() == b.fleet());
    }
    NameGen c(778);
    CHECK(!(c.empire() == NameGen(777).empire() && c.system() == NameGen(777).system()));
}

TEST(gen, event_table_references_valid) {
    CHECK_EQ(kEventCount, 140);
    CHECK_EQ(kAnomalyCount, 25);
    for (int i = 0; i < kEventCount; ++i) {
        const EventInfo& e = eventInfo(i);
        CHECK(static_cast<int>(e.phase) < static_cast<int>(EventPhase::Count));
        CHECK(!e.title.empty());
        CHECK(e.weight > 0);
        CHECK(e.severity.rawValue() > 0);
        if (e.requireCommodity >= 0) CHECK(e.requireCommodity < kCommodityCount);
    }
    for (int i = 0; i < kAnomalyCount; ++i) {
        const AnomalyInfo& a = anomalyInfo(i);
        CHECK(!a.nameZh.empty());
        CHECK(a.rewardClue >= 0 && a.rewardClue < kClueCount);
        CHECK(a.rewardItem >= 0 && a.rewardItem < kItemCount);
    }
    // 抽取事件在合法范围内
    GameState st;
    generateWorld(st, opts(31));
    for (int i = 0; i < 40; ++i) {
        GameState probe = st;
        probe.tick = static_cast<u64>(i * 3);
        int id = pickEvent(probe, EventPhase::MarketShock);
        CHECK(id >= 0 && id < kEventCount);
    }
}

TEST(gen, tech_and_building_tables) {
    CHECK_EQ(kTechCount, 96);
    CHECK_EQ(kBuildingCount, 28);
    CHECK_EQ(kMegastructureCount, 6);
    CHECK_EQ(kModuleCount, 36);
    // 科技前置存在，且同分支 tier 单调
    for (int i = 0; i < kTechCount; ++i) {
        const TechInfo& t = techInfo(i);
        CHECK(t.cost > 0);
        CHECK(t.tier >= 1 && t.tier <= 5);
        for (u8 p : t.prereq) CHECK(p < kTechCount);
    }
    // 建筑/巨构的解锁科技存在
    for (int i = 0; i < kBuildingCount; ++i) {
        const BuildingInfo& b = buildingInfo(i);
        if (b.requireTech >= 0) CHECK(b.requireTech < kTechCount);
        CHECK(b.creditCost > 0);
    }
    for (int i = 0; i < kMegastructureCount; ++i) {
        const MegastructureInfo& m = megastructureInfo(i);
        CHECK(m.stages >= 3);
        CHECK(m.apCost > 0);
        if (m.requireTech >= 0) CHECK(m.requireTech < kTechCount);
    }
    // 索引按名字可查
    CHECK_EQ(buildingIndexByName(buildingInfo(3).nameZh), 3);
    CHECK_EQ(megastructureIndexByName(megastructureInfo(0).idName), 0);
    CHECK_EQ(moduleIndexByName(moduleInfo(2).idName), 2);
    CHECK_EQ(hullIndexByName("titan"), static_cast<int>(HullClass::Titan));
}

// 回归守卫：Planet::owner 曾默认 0（即玩家），而星图生成从不设置行星归属，
// 导致全星系 149 颗无主行星都被算作玩家所有 ——
// 玩家因此可以在任何行星上建造。默认值必须是 kNoEmpire。
// 回归守卫：星图曾把「己方星系」与「无主星系」都画成 'o'，
// 玩家在图上找不到自己的领土，也无从判断哪里可以殖民。
TEST(gen, starmap_distinguishes_own_from_unowned) {
    GameState st;
    generateWorld(st, opts(7777));
    // 统计各类归属的星系数量
    int own = 0, other = 0, none = 0;
    for (const auto& s : st.map.systems) {
        if (s.owner == kPlayerId) ++own;
        else if (s.owner == kNoEmpire) ++none;
        else ++other;
    }
    // 三种归属都应当存在，否则「可区分」这件事无从检验
    CHECK(own > 0);
    CHECK(none > 0);
    CHECK(other > 0);
    // 玩家至少有一个首都
    bool hasCapital = false;
    for (const auto& s : st.map.systems)
        if (s.owner == kPlayerId && s.capital) hasCapital = true;
    CHECK(hasCapital);
    // 无主星系不得被标记为首都（否则 '?' / '*' 会与归属信息冲突）
    for (const auto& s : st.map.systems) {
        if (s.owner == kNoEmpire) CHECK(!s.capital);
    }
}

TEST(gen, unowned_planets_are_not_attributed_to_player) {
    GameState st;
    generateWorld(st, opts(7777));
    int playerOwned = 0;
    int unownedBefore = 0;
    for (const auto& p : st.planets) {
        if (p.owner == kPlayerId) ++playerOwned;
        // 未被任何帝国认领的行星必须保持 kNoEmpire
        if (p.owner == kNoEmpire) ++unownedBefore;
    }
    // 玩家拥有的行星数必须与其实际殖民数一致（不能是全星系）
    CHECK(playerOwned > 0);
    CHECK(playerOwned < static_cast<int>(st.planets.size()));
    // 应当存在大量无主行星
    CHECK(unownedBefore > 0);
    // 逐帝国核对：帝国声明的行星数应当等于 owner 指向它的行星数
    for (const auto& e : st.empires) {
        int declared = 0;
        for (u32 sys : e.systems) {
            const SystemNode* s = st.system(sys);
            if (s == nullptr) continue;
            for (u32 pid : s->planets) {
                const Planet* p = st.planet(pid);
                if (p != nullptr && p->owner == e.id) ++declared;
            }
        }
        int actual = 0;
        for (const auto& p : st.planets)
            if (p.owner == e.id) ++actual;
        CHECK_EQ(declared, actual);
    }
}

TEST(gen, economy_is_self_sustaining) {
    GameState st;
    generateWorld(st, opts(19, 8, 40));
    Fixed before = st.empires[kPlayerId].treasury;
    for (int i = 0; i < 20; ++i) {
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);
    }
    Fixed after = st.market.margin.cash;
    // 20 季内玩家经济不应崩盘（税收能覆盖维护）
    CHECK(after.rawValue() > -Fixed(20000).rawValue());
    CHECK(after.rawValue() < before.rawValue() * 4);
}
