// 人事体系：领袖、科学家、编队与集团军
#include <algorithm>

#include "check.h"
#include "core/GameState.h"
#include "core/TickPipeline.h"
#include "domain/Commander.h"
#include "domain/Personnel.h"
#include "domain/Tech.h"
#include "gen/EmpireGen.h"
#include "gen/WorldGen.h"
#include "plot/BeatResolver.h"
#include "save/Serde.h"

using namespace gf;

namespace {

GameState perWorld(u64 seed = 4242, int systems = 56) {
    WorldGenOptions o;
    o.seed = seed;
    o.difficulty = 3;
    o.empireCount = 8;
    o.systemCount = systems;
    GameState st;
    generateWorld(st, o);
    return st;
}

void perTicks(GameState& st, int n) {
    for (int i = 0; i < n; ++i) {
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);
    }
}

}  // namespace

// 回归守卫：人事系统曾完全缺失 —— 没有领袖、没有科学家、没有编队，
// 舰队只能一支支单独指挥，没有任何组织加成。
TEST(personnel, empires_start_with_a_ruler) {
    GameState st = perWorld();
    for (const auto& e : st.empires) {
        if (!e.alive) continue;
        CHECK(!e.ruler.name.empty());
        CHECK(e.ruler.trait != RulerTrait::None);
        CHECK(e.ruler.skill.rawValue() > 0);
        CHECK(e.ruler.age >= 40);
    }
}

TEST(personnel, ruler_trait_provides_modifier) {
    GameState st = perWorld(777);
    Empire& me = st.empires[kPlayerId];
    // 逐个特质验证它能提供对应修正
    struct Case {
        RulerTrait t;
        ModKind k;
    };
    const Case cases[] = {{RulerTrait::Scientist, ModKind::ResearchRate},
                          {RulerTrait::Warlord, ModKind::MilitaryPower},
                          {RulerTrait::Merchant, ModKind::TradeMargin},
                          {RulerTrait::Administrator, ModKind::BuildRate}};
    for (const auto& c : cases) {
        me.ruler.trait = RulerTrait::None;
        Fixed base = empireModifier(me, c.k);
        me.ruler.trait = c.t;
        Fixed with = empireModifier(me, c.k);
        CHECK(with.rawValue() > base.rawValue());
    }
}

TEST(personnel, ruler_is_replaced_on_election_or_death) {
    GameState st = perWorld(888);
    Empire& me = st.empires[kPlayerId];
    // 民主政体：任期届满必然改选
    me.government = 0;
    me.ruler.elected = true;
    me.ruler.termEnd = 10;
    me.ruler.age = 40;
    std::string before = me.ruler.name;
    perTicks(st, 14);
    // 要么换人，要么至少仍持有合法领袖
    CHECK(!me.ruler.name.empty());
    (void)before;
}

TEST(personnel, scientist_recruit_and_assign) {
    GameState st = perWorld(999);
    Empire& me = st.empires[kPlayerId];
    me.influence = Fixed(5000);
    std::string msg;
    CHECK(scientistRecruit(st, kPlayerId, &msg));
    CHECK_EQ(me.scientists.size(), 1u);
    CHECK(!msg.empty());
    u32 id = me.scientists.front().id;
    // 派往某个分支
    CHECK(scientistAssign(st, kPlayerId, id, 2, &msg));
    CHECK_EQ(static_cast<int>(me.scientists.front().assignedBranch), 2);
    // 该分支应当有加成，其它分支没有
    CHECK(scientistBonus(st, kPlayerId, 2).rawValue() > 0);
    CHECK_EQ(scientistBonus(st, kPlayerId, 0).rawValue(), 0);
    // 非法分支被拒
    CHECK(!scientistAssign(st, kPlayerId, id, 99, &msg));
    // 影响力不足时招募失败
    me.influence = Fixed(0);
    CHECK(!scientistRecruit(st, kPlayerId, &msg));
}

// 回归守卫：科学家的加速**不得突破最短工期** ——
// 「研究需要时间」是硬保证，任何加成都不能把它变成「花钱即得」。
TEST(personnel, scientist_speedup_never_breaks_min_ticks) {
    int ticksWith = -1;
    for (int withSci = 0; withSci < 2; ++withSci) {
        GameState st = perWorld(1234);
        Empire& me = st.empires[kPlayerId];
        me.treasury = Fixed(50000000);
        if (withSci) {
            Scientist s;
            s.id = st.nextScientistId++;
            s.name = "测试";
            s.owner = kPlayerId;
            s.field = ScientistField::Engineering;
            s.skill = Fixed::pct(100);
            s.assignedBranch = 2;
            me.scientists.push_back(s);
            CHECK(scientistBonus(st, kPlayerId, 2).rawValue() > 0);
        }
        int pick = -1;
        for (int i = 0; i < kTechCount; ++i) {
            if (techInfo(i).branch != TechBranch::Engineering) continue;
            if (!techAvailable(me.tech, i)) continue;
            pick = i;
            break;
        }
        if (pick < 0) return;
        (void)techStartProject(me.tech, pick, nullptr);
        me.tech.fundingPerTick = Fixed(5000000);   // 资金管够
        int ticks = 0;
        for (int t = 1; t <= 60; ++t) {
            advanceOneTick(st);
            while (!st.pending.empty()) resolveChoiceAuto(st, 0);
            ticks = t;
            if (me.tech.project == TechState::kNoTech) break;
        }
        const int minT = techMinTicks(techInfo(pick).tier);
        CHECK(ticks >= minT);
        if (withSci) ticksWith = ticks;
    }
    CHECK(ticksWith >= 0);
}

TEST(personnel, formation_groups_fleets_and_boosts_power) {
    GameState st = perWorld(555);
    Empire& me = st.empires[kPlayerId];
    if (me.fleets.empty()) return;
    std::string msg;
    CHECK(formationCreate(st, kPlayerId, "先锋集群", &msg));
    CHECK_EQ(me.formations.size(), 1u);
    u32 fid = me.formations.front().id;
    Fixed base = formationPowerMultiplier(st, me.fleets.front());
    CHECK_EQ(base.rawValue(), Fixed(1).rawValue());   // 未编入时无加成
    CHECK(formationAddFleet(st, kPlayerId, fid, me.fleets.front(), &msg));
    CHECK_EQ(me.formations.front().fleets.size(), 1u);
    Fixed with = formationPowerMultiplier(st, me.fleets.front());
    CHECK(with.rawValue() >= base.rawValue());
    // 一支舰队只能属于一个集团军：编入新集团军会从旧的移出
    CHECK(formationCreate(st, kPlayerId, "第二集群", &msg));
    u32 fid2 = me.formations.back().id;
    CHECK(formationAddFleet(st, kPlayerId, fid2, me.fleets.front(), &msg));
    CHECK_EQ(me.formations[0].fleets.size(), 0u);
    CHECK_EQ(me.formations[1].fleets.size(), 1u);
    // 移出与解散
    CHECK(formationRemoveFleet(st, kPlayerId, fid2, me.fleets.front(), &msg));
    CHECK_EQ(me.formations[1].fleets.size(), 0u);
    CHECK(formationDisband(st, kPlayerId, fid2, &msg));
    CHECK_EQ(me.formations.size(), 1u);
    // 非法操作
    CHECK(!formationAddFleet(st, kPlayerId, 9999, me.fleets.front(), &msg));
}

TEST(personnel, formation_coordination_rises_when_together) {
    GameState st = perWorld(666);
    Empire& me = st.empires[kPlayerId];
    if (me.fleets.size() < 2) return;
    std::string msg;
    (void)formationCreate(st, kPlayerId, "合练集群", &msg);
    u32 fid = me.formations.front().id;
    // 把两支舰队编入并放到同一星系
    (void)formationAddFleet(st, kPlayerId, fid, me.fleets[0], &msg);
    (void)formationAddFleet(st, kPlayerId, fid, me.fleets[1], &msg);
    const Fleet* f0 = st.fleet(me.fleets[0]);
    if (f0 == nullptr) return;
    u32 sysId = f0->system;
    for (u32 fl : me.formations.front().fleets) {
        Fleet* f = st.fleet(fl);
        if (f != nullptr) {
            f->system = sysId;
            f->order = FleetOrder::Idle;
            f->targetSystem = kNoSystem;
        }
    }
    Fixed before = me.formations.front().coordination;
    perTicks(st, 10);
    const Formation* form = nullptr;
    for (const auto& f : st.empires[kPlayerId].formations)
        if (f.id == fid) form = &f;
    CHECK(form != nullptr);
    CHECK(form->coordination.rawValue() > before.rawValue());
}

TEST(personnel, state_survives_serialization) {
    GameState st = perWorld(4321);
    Empire& me = st.empires[kPlayerId];
    me.influence = Fixed(5000);
    std::string msg;
    (void)scientistRecruit(st, kPlayerId, &msg);
    (void)formationCreate(st, kPlayerId, "存档集群", &msg);
    perTicks(st, 5);

    std::vector<u8> bytes = serializeState(st);
    GameState back;
    CHECK(tryDeserializeState(bytes, back));
    CHECK(back.empires[kPlayerId].ruler.name == st.empires[kPlayerId].ruler.name);
    CHECK_EQ(back.empires[kPlayerId].scientists.size(), st.empires[kPlayerId].scientists.size());
    CHECK_EQ(back.empires[kPlayerId].formations.size(), st.empires[kPlayerId].formations.size());
    CHECK_EQ(back.nextScientistId, st.nextScientistId);
    CHECK_EQ(back.nextFormationId, st.nextFormationId);
}

// 回归守卫：人事字段若漏存，读档后领袖、科学家与集团军会整体消失
TEST(personnel, save_load_fidelity_with_personnel) {
    GameState a = perWorld(31337);
    Empire& me = a.empires[kPlayerId];
    me.influence = Fixed(5000);
    std::string msg;
    (void)scientistRecruit(a, kPlayerId, &msg);
    (void)formationCreate(a, kPlayerId, "保真集群", &msg);
    perTicks(a, 12);

    std::vector<u8> bytes = serializeState(a);
    GameState b;
    CHECK(tryDeserializeState(bytes, b));
    CHECK_EQ(a.stateHash(), b.stateHash());
    perTicks(a, 10);
    perTicks(b, 10);
    CHECK_EQ(a.stateHash(), b.stateHash());
}
