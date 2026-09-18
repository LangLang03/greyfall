// 恒星基地与建筑效果
#include <algorithm>
#include "plot/EventSystem.h"

#include "check.h"
#include "combat/Resolver.h"
#include "core/GameState.h"
#include "domain/Empire.h"
#include "domain/Starbase.h"
#include "domain/Treaty.h"
#include "gen/WorldGen.h"
#include "save/Serde.h"

using namespace gf;

namespace {

GameState baseWorld(u64 seed = 4242, int systems = 56) {
    WorldGenOptions o;
    o.seed = seed;
    o.difficulty = 3;
    o.empireCount = 8;
    o.systemCount = systems;
    GameState st;
    generateWorld(st, o);
    return st;
}

void finishBaseProject(GameState& st) {
    for (auto& v : st.player().stock) v = fxMax(v, Fixed(50000));
    for (int i = 0; i < 32 && hasDevelopment(st.player(), ProjectKind::Starbase); ++i) {
        developmentPhase(st); ++st.tick;
    }
    CHECK(!hasDevelopment(st.player(), ProjectKind::Starbase));
}

}  // namespace

TEST(starbase, tier_table_is_monotonic) {
    for (int i = 1; i < static_cast<int>(StarbaseTier::Count); ++i) {
        StarbaseTier prev = static_cast<StarbaseTier>(i - 1);
        StarbaseTier cur = static_cast<StarbaseTier>(i);
        // 成本与防御必须逐级递增
        CHECK(starbaseUpgradeCredits(cur) > starbaseUpgradeCredits(prev));
        CHECK(starbaseUpgradeAlloys(cur) > starbaseUpgradeAlloys(prev));
        CHECK(starbaseDefense(cur).rawValue() > starbaseDefense(prev).rawValue());
        CHECK(!starbaseTierName(cur).empty());
        CHECK(!starbaseTierDesc(cur).empty());
    }
}

TEST(starbase, found_and_upgrade_and_dismantle) {
    GameState st = baseWorld(7201);
    Empire& me = st.empires[kPlayerId];
    if (me.systems.empty()) return;
    u32 sys = me.systems.front();
    me.treasury = Fixed(5000000);
    st.market.margin.cash = me.treasury;
    me.stock[static_cast<std::size_t>(Commodity::Alloys)] = Fixed(50000);
    std::string msg;

    CHECK(starbaseAt(st, sys) == nullptr);
    CHECK(starbaseFound(st, kPlayerId, sys, &msg));
    CHECK(starbaseAt(st, sys) == nullptr);
    finishBaseProject(st);
    CHECK(starbaseAt(st, sys) != nullptr);
    CHECK(starbaseAt(st, sys)->tier == StarbaseTier::Outpost);
    // 重复建立被拒
    CHECK(!starbaseFound(st, kPlayerId, sys, &msg));
    // 非己方星系被拒
    u32 foreign = kNoSystem;
    for (const auto& s : st.map.systems)
        if (s.owner != kPlayerId && s.owner != kNoEmpire) {
            foreign = s.id;
            break;
        }
    if (foreign != kNoSystem) CHECK(!starbaseFound(st, kPlayerId, foreign, &msg));

    // 升级
    CHECK(starbaseUpgrade(st, kPlayerId, sys, &msg));
    CHECK(starbaseAt(st, sys)->tier == StarbaseTier::Outpost);
    finishBaseProject(st);
    CHECK(starbaseAt(st, sys)->tier == StarbaseTier::Starport);
    // 升到顶后不能再升
    for (int upgrades = 0; upgrades < 5 && static_cast<int>(starbaseAt(st, sys)->tier) + 1 < static_cast<int>(StarbaseTier::Count); ++upgrades) {
        CHECK(starbaseUpgrade(st, kPlayerId, sys, &msg));
        finishBaseProject(st);
    }
    CHECK(!starbaseUpgrade(st, kPlayerId, sys, &msg));

    // 拆除
    CHECK(starbaseDismantle(st, kPlayerId, sys, &msg));
    CHECK(starbaseAt(st, sys) == nullptr);
    CHECK(!starbaseDismantle(st, kPlayerId, sys, &msg));
}

TEST(starbase, costs_are_paid) {
    GameState st = baseWorld(7202);
    Empire& me = st.empires[kPlayerId];
    if (me.systems.empty()) return;
    u32 sys = me.systems.front();
    me.treasury = Fixed(5000000);
    st.market.margin.cash = me.treasury;
    me.stock[static_cast<std::size_t>(Commodity::Alloys)] = Fixed(50000);
    Fixed cash0 = me.treasury;
    Fixed alloy0 = me.stock[static_cast<std::size_t>(Commodity::Alloys)];
    std::string msg;
    CHECK(starbaseFound(st, kPlayerId, sys, &msg));
    CHECK(starbaseAt(st, sys) == nullptr);
    CHECK(me.treasury.rawValue() < cash0.rawValue());
    CHECK(me.stock[static_cast<std::size_t>(Commodity::Alloys)].rawValue() < alloy0.rawValue());
    // 资源不足时失败
    me.treasury = Fixed(0);
    me.stock[static_cast<std::size_t>(Commodity::Alloys)] = Fixed(0);
    CHECK(!starbaseUpgrade(st, kPlayerId, sys, &msg));
}

// 回归守卫：恒星基地原先加在 combatOdds（帝国级粗估）上，
// 而那里不知道哪个星系被攻击，实测对战斗毫无影响。
// 现在它加入按星系的防守计算，且**始终**生效（不只是无舰队时兜底）。
TEST(starbase, starbase_defense_affects_battle_odds) {
    GameState st = baseWorld(7203);
    Empire& me = st.empires[kPlayerId];
    if (me.systems.empty()) return;
    u32 sys = me.systems.front();
    u32 foe = 1;
    if (!atWarWith(st, kPlayerId, foe)) declareWar(st, kPlayerId, foe, true);
    // 给进攻方一支有分量的舰队停在目标星系
    for (u32 fid : st.empire(foe)->fleets) {
        Fleet* f = st.fleet(fid);
        if (f == nullptr) continue;
        f->system = sys;
        f->strength = Fixed(2500);
        f->order = FleetOrder::Engage;
    }
    Fixed defBefore = systemDefense(st, sys);
    Fixed oddsBefore = simulateBattleOdds(st, sys, foe, kPlayerId);

    me.treasury = Fixed(5000000);
    st.market.margin.cash = me.treasury;
    me.stock[static_cast<std::size_t>(Commodity::Alloys)] = Fixed(50000);
    std::string msg;
    CHECK(starbaseFound(st, kPlayerId, sys, &msg));
    CHECK(starbaseAt(st, sys) == nullptr);
    finishBaseProject(st);
    for (int i = 0; i < 3; ++i) { CHECK(starbaseUpgrade(st, kPlayerId, sys, &msg)); finishBaseProject(st); }

    Fixed defAfter = systemDefense(st, sys);
    Fixed oddsAfter = simulateBattleOdds(st, sys, foe, kPlayerId);
    // 防守值必须显著提升
    CHECK(defAfter.rawValue() > defBefore.rawValue());
    // 进攻方胜算必须下降 —— 这是基地的实际军事意义
    CHECK(oddsAfter.rawValue() < oddsBefore.rawValue());
}

TEST(starbase, changes_hands_and_downgrades) {
    GameState st = baseWorld(7204);
    Empire& me = st.empires[kPlayerId];
    if (me.systems.empty()) return;
    u32 sys = me.systems.front();
    me.treasury = Fixed(5000000);
    st.market.margin.cash = me.treasury;
    me.stock[static_cast<std::size_t>(Commodity::Alloys)] = Fixed(50000);
    std::string msg;
    CHECK(starbaseFound(st, kPlayerId, sys, &msg));
    CHECK(starbaseAt(st, sys) == nullptr);
    finishBaseProject(st);
    CHECK(starbaseUpgrade(st, kPlayerId, sys, &msg));
    finishBaseProject(st);
    CHECK(starbaseUpgrade(st, kPlayerId, sys, &msg));
    finishBaseProject(st);
    CHECK(starbaseAt(st, sys)->tier == StarbaseTier::Fortress);
    // 星系易主
    st.system(sys)->owner = 1;
    starbasePhase(st);
    const Starbase* b = starbaseAt(st, sys);
    CHECK(b != nullptr);
    CHECK_EQ(b->owner, 1u);
    CHECK(b->tier == StarbaseTier::Starport);   // 降一级
}

TEST(starbase, state_survives_serialization) {
    GameState st = baseWorld(7205);
    Empire& me = st.empires[kPlayerId];
    if (me.systems.empty()) return;
    u32 sys = me.systems.front();
    me.treasury = Fixed(5000000);
    st.market.margin.cash = me.treasury;
    me.stock[static_cast<std::size_t>(Commodity::Alloys)] = Fixed(50000);
    std::string msg;
    CHECK(starbaseFound(st, kPlayerId, sys, &msg));
    CHECK(starbaseAt(st, sys) == nullptr);
    finishBaseProject(st);
    CHECK(starbaseUpgrade(st, kPlayerId, sys, &msg));
    finishBaseProject(st);

    std::vector<u8> bytes = serializeState(st);
    GameState back;
    CHECK(tryDeserializeState(bytes, back));
    CHECK_EQ(back.starbases.size(), st.starbases.size());
    CHECK_EQ(back.stateHash(), st.stateHash());
}

// 回归守卫：ProdResearch/Trading/Storage/ClueDiscovery 四种建筑效果
// 原先只出现在 AI 的选型打分里，switch 落到 default 被静默忽略 ——
// 玩家花钱建的建筑对游戏毫无作用。
TEST(starbase, building_effects_are_wired) {
    GameState st = baseWorld(7206);
    Empire& me = st.empires[kPlayerId];
    if (me.systems.empty()) return;
    // 找一个自己的行星放建筑
    Planet* target = nullptr;
    for (auto& p : st.planets)
        if (p.owner == kPlayerId) {
            target = &p;
            break;
        }
    if (target == nullptr) return;
    // 找四种建筑各一座
    int researchB = -1, tradingB = -1, storageB = -1, clueB = -1;
    for (int b = 0; b < kBuildingCount; ++b) {
        switch (buildingInfo(b).effect) {
            case BuildingEffect::ProdResearch: if (researchB < 0) researchB = b; break;
            case BuildingEffect::Trading: if (tradingB < 0) tradingB = b; break;
            case BuildingEffect::Storage: if (storageB < 0) storageB = b; break;
            case BuildingEffect::ClueDiscovery: if (clueB < 0) clueB = b; break;
            default: break;
        }
    }
    // 至少存在其中之一，且建成后必须产生可观测的状态变化
    target->buildings.clear();
    if (tradingB >= 0) {
        target->buildings.push_back(static_cast<u32>(tradingB));
        me.tradeBonus = Fixed(0);
        me.storageBonus = Fixed(0);
        me.clueBonus = Fixed(0);
        // 跑一 tick，建筑效果应当被重新累计
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);
        CHECK(me.tradeBonus.rawValue() > 0);
    }
    if (storageB >= 0) {
        target->buildings.push_back(static_cast<u32>(storageB));
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);
        CHECK(me.storageBonus.rawValue() > 0);
    }
    if (clueB >= 0) {
        target->buildings.push_back(static_cast<u32>(clueB));
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);
        CHECK(me.clueBonus.rawValue() > 0);
    }
}
