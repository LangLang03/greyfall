// 战斗系统（HOI4 风格）：组织度驱动 / 战斗宽度 / 指挥官 / 老练度 / 地形 / 领土易手
#include <algorithm>

#include "check.h"
#include "combat/Resolver.h"
#include "core/GameState.h"
#include "core/TickPipeline.h"
#include "domain/Commander.h"
#include "gen/WorldGen.h"
#include "plot/EventSystem.h"
#include "plot/BeatResolver.h"
#include "save/Serde.h"

using namespace gf;

namespace {

GameState warWorld(u64 seed = 9090) {
    WorldGenOptions o;
    o.seed = seed;
    o.difficulty = 2;
    o.empireCount = 8;
    o.systemCount = 48;
    GameState st;
    generateWorld(st, o);
    return st;
}

/// 把两个帝国的舰队都放进同一个星系并宣战
u32 stageBattle(GameState& st, u32 attacker, u32 defender) {
    declareWar(st, attacker, defender, true);
    const Empire* d = st.empire(defender);
    u32 sys = d->systems.empty() ? d->capital : d->systems[0];
    // 必须同时锁定命令，否则移动阶段会立刻把舰队开走
    for (u32 id : st.empire(attacker)->fleets) {
        Fleet* f = st.fleet(id);
        if (f != nullptr) {
            f->system = sys;
            f->targetSystem = kNoSystem;
            f->order = FleetOrder::Patrol;
        }
    }
    for (u32 id : st.empire(defender)->fleets) {
        Fleet* f = st.fleet(id);
        if (f != nullptr) {
            f->system = sys;
            f->targetSystem = kNoSystem;
            f->order = FleetOrder::Patrol;
        }
    }
    return sys;
}

/// 只推进战斗阶段：用于隔离 AI 调度对战斗用例的干扰
void combatOnlyTicks(GameState& st, u32 attacker, u32 defender, u32 sys, int n) {
    for (int i = 0; i < n; ++i) {
        if (!atWarWith(st, attacker, defender)) declareWar(st, attacker, defender, true);
        for (u32 id : st.empire(attacker)->fleets) {
            Fleet* f = st.fleet(id);
            if (f == nullptr) continue;
            f->system = sys;
            f->targetSystem = kNoSystem;
        }
        for (u32 id : st.empire(defender)->fleets) {
            Fleet* f = st.fleet(id);
            if (f == nullptr) continue;
            f->system = sys;
            f->targetSystem = kNoSystem;
        }
        TickReport rep;
        combatPhase(st, rep);
    }
}

void runTicks(GameState& st, int n) {
    for (int i = 0; i < n; ++i) {
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);
    }
}

}  // namespace

TEST(combat, commanders_are_generated_and_assigned) {
    GameState st = warWorld();
    CHECK(!st.commanders.empty());
    // 每个帝国至少一名指挥官
    for (const auto& e : st.empires) {
        int n = 0;
        for (const auto& c : st.commanders)
            if (c.owner == e.id) ++n;
        CHECK(n >= 1);
    }
    // 舰队应绑定指挥官（首两支舰队）
    int assigned = 0;
    for (const auto& f : st.fleets)
        if (f.commander != 0xFFFFFFFFu) ++assigned;
    CHECK(assigned > 0);
    // 特质在合法范围内
    for (const auto& c : st.commanders) {
        CHECK(static_cast<int>(c.trait) < static_cast<int>(CommanderTrait::Count));
        CHECK(c.attack.rawValue() > 0);
        CHECK(c.defense.rawValue() > 0);
    }
}

TEST(combat, trait_effects_have_correct_direction) {
    CHECK(traitAttackBonus(CommanderTrait::Offensive).rawValue() > 0);
    CHECK(traitDefenseBonus(CommanderTrait::Defensive).rawValue() > 0);
    CHECK(traitLogisticsBonus(CommanderTrait::Logistician).rawValue() > 0);
    // 攻势指挥官防御为负，防御指挥官攻击为负 —— 有代价才有取舍
    CHECK(traitDefenseBonus(CommanderTrait::Offensive).rawValue() < 0);
    CHECK(traitAttackBonus(CommanderTrait::Defensive).rawValue() < 0);
    // 诡道压缩对方宽度
    CHECK(traitWidthFactor(CommanderTrait::Trickster).rawValue() < 0);
    CHECK(traitWidthFactor(CommanderTrait::Maneuver).rawValue() > 0);
}

TEST(combat, veterancy_scales_monotonically) {
    // 经验以 0..1000 点存于 raw
    CHECK_EQ(veterancyLevel(Fixed::raw(0)), 0);
    CHECK_EQ(veterancyLevel(Fixed::raw(100)), 1);
    CHECK_EQ(veterancyLevel(Fixed::raw(300)), 2);
    CHECK_EQ(veterancyLevel(Fixed::raw(600)), 3);
    CHECK_EQ(veterancyLevel(Fixed::raw(900)), 4);
    for (int i = 1; i <= 4; ++i)
        CHECK(veterancyMultiplier(i).rawValue() > veterancyMultiplier(i - 1).rawValue());
    CHECK(veterancyMultiplier(4).rawValue() > FIX);
    CHECK(!std::string_view(veterancyName(0)).empty());
}

TEST(combat, encounter_creates_battle_with_owner_as_defender) {
    GameState st = warWorld(11);
    u32 sys = stageBattle(st, 2, 1);
    u32 owner = st.system(sys)->owner;
    combatOnlyTicks(st, 2, 1, sys, 1);
    bool found = false;
    for (const auto& b : st.battles) {
        if (b.system != sys) continue;
        found = true;
        // 星系所有者必须是防守方，另一方是入侵者
        CHECK_EQ(b.defender, owner);
        CHECK(b.attacker != owner);
        CHECK(!b.attackerFleets.empty());
        CHECK(!b.terrainName.empty());
    }
    CHECK(found);
}

TEST(combat, org_depletes_and_battle_is_multi_tick) {
    GameState st = warWorld(22);
    u32 sys = stageBattle(st, 2, 1);
    // 记录参战舰队的初始组织度
    std::vector<std::pair<u32, Fixed>> orgBefore;
    for (const auto& f : st.fleets)
        if (f.system == sys) orgBefore.emplace_back(f.id, f.org);

    combatOnlyTicks(st, 2, 1, sys, 4);

    // 必须有舰队组织度下降（战斗真的在消耗组织度，而不是兵力硬拼）
    bool anyOrgDrop = false;
    for (const auto& [id, before] : orgBefore) {
        const Fleet* f = st.fleet(id);
        if (f == nullptr) continue;
        if (f->org.rawValue() < before.rawValue()) anyOrgDrop = true;
    }
    CHECK(anyOrgDrop);
    // 战斗应持续多 tick（不是一次性结算）
    bool multiTick = false;
    for (const auto& b : st.battles) {
        if (b.system != sys) continue;
        if (b.ticks >= 2) multiTick = true;
        // 累计伤害与组织度损失都应被记录
        CHECK(b.defenderLoss.rawValue() > 0 || b.attackerLoss.rawValue() > 0);
    }
    CHECK(multiTick);
}

TEST(combat, overwhelming_force_captures_system) {
    GameState st = warWorld(33);
    u32 sys = stageBattle(st, 2, 1);
    u32 originalOwner = st.system(sys)->owner;
    // 给入侵者压倒性兵力
    for (u32 id : st.empire(2)->fleets) {
        Fleet* f = st.fleet(id);
        if (f != nullptr) f->strength = f->strength * Fixed(6);
    }
    // 削弱防守方
    for (u32 id : st.empire(1)->fleets) {
        Fleet* f = st.fleet(id);
        if (f != nullptr) f->strength = f->strength / Fixed(3);
    }
    // 只调用 combatPhase：本用例检验的是**战斗与占领逻辑**本身。
    // 走完整流水线时，AI 会在战斗阶段之前下达入侵命令并移动舰队，
    // 使参战舰队离开战场，用例就变成在检验 AI 调度而非战斗系统。
    for (int t = 0; t < 14; ++t) {
        if (!atWarWith(st, 2, 1)) declareWar(st, 2, 1, true);
        for (u32 id : st.empire(2)->fleets) {
            Fleet* f = st.fleet(id);
            if (f == nullptr) continue;
            f->system = sys;
            f->targetSystem = kNoSystem;
        }
        for (u32 id : st.empire(1)->fleets) {
            Fleet* f = st.fleet(id);
            if (f == nullptr) continue;
            f->system = sys;
            f->targetSystem = kNoSystem;
        }
        TickReport rep2;
        combatPhase(st, rep2);
    }
    // 星系应当易主
    CHECK(st.system(sys)->owner != originalOwner);
    CHECK_EQ(st.system(sys)->owner, 2u);
    // 战报应记录进攻方胜利
    bool won = false;
    for (const auto& b : st.battles) {
        if (b.system != sys) continue;
        if (b.resolved && b.attackerWon) won = true;
    }
    CHECK(won);
}

TEST(combat, equal_force_against_fortress_fails) {
    GameState st = warWorld(44);
    u32 sys = stageBattle(st, 2, 1);
    u32 originalOwner = st.system(sys)->owner;
    runTicks(st, 16);
    // 均势进攻设防星系应当失败（星系不易手）
    CHECK_EQ(st.system(sys)->owner, originalOwner);
}

TEST(combat, combat_width_limits_engagement) {
    GameState st = warWorld(55);
    (void)stageBattle(st, 2, 1);
    runTicks(st, 2);
    for (const auto& b : st.battles) {
        // 展开宽度必须是有限正数
        CHECK(b.attackerWidth.rawValue() > 0);
        CHECK(b.defenderWidth.rawValue() > 0);
        CHECK(b.widthCap.rawValue() > 0);
    }
}

TEST(combat, terrain_modifier_depends_on_system) {
    GameState st = warWorld(66);
    // 首都防御加成最高
    Fixed capBonus = terrainDefenseBonus(st, st.empires[1].capital);
    CHECK(capBonus.rawValue() > 0);
    CHECK(!terrainNameOf(st, st.empires[1].capital).empty());
    // 无主空域没有加成
    u32 empty = kNoSystem;
    for (const auto& s : st.map.systems) {
        if (s.owner == kNoEmpire && s.anomaly == 0 && !s.capital) {
            empty = s.id;
            break;
        }
    }
    if (empty != kNoSystem) {
        CHECK(terrainDefenseBonus(st, empty).rawValue() <= capBonus.rawValue());
    }
}

TEST(combat, fleets_recover_org_when_not_fighting) {
    GameState st = warWorld(77);
    // 打一场把组织度打下去
    (void)stageBattle(st, 2, 1);
    runTicks(st, 4);
    // 把所有舰队撤出战斗并移到不同星系
    for (auto& f : st.fleets) {
        f.battle = 0xFFFFFFFFu;
        f.org = Fixed(10);
        f.order = FleetOrder::Idle;
    }
    st.battles.clear();
    for (auto& r : st.relations) r.atWar = false;
    runTicks(st, 6);
    // 组织度必须恢复
    int recovered = 0;
    for (const auto& f : st.fleets)
        if (f.org.rawValue() > Fixed(20).rawValue()) ++recovered;
    CHECK(recovered > 0);
}

TEST(combat, commander_assignment_rules) {
    GameState st = warWorld(88);
    u32 cid = st.commanders.front().id;
    u32 myFleet = st.empires[kPlayerId].fleets.empty() ? 0xFFFFFFFFu : st.empires[kPlayerId].fleets[0];
    if (myFleet == 0xFFFFFFFFu) return;
    // 跨阵营分配必须被拒绝
    u32 otherFleet = 0xFFFFFFFFu;
    for (const auto& e : st.empires) {
        if (e.isPlayer || e.fleets.empty()) continue;
        otherFleet = e.fleets[0];
        break;
    }
    std::string err;
    if (otherFleet != 0xFFFFFFFFu) {
        CHECK(!assignCommander(st, cid, otherFleet, &err));
        CHECK(!err.empty());
    }
    // 同阵营分配成功
    uint32_t myCmd = 0xFFFFFFFFu;
    for (const auto& c : st.commanders)
        if (c.owner == kPlayerId) myCmd = c.id;
    if (myCmd != 0xFFFFFFFFu) {
        CHECK(assignCommander(st, myCmd, myFleet, &err));
        const Fleet* f = st.fleet(myFleet);
        CHECK_EQ(f->commander, myCmd);
    }
    // 不存在的目标必须报错
    CHECK(!assignCommander(st, 99999, myFleet, &err));
}

TEST(combat, commander_experience_grows_in_battle) {
    GameState st = warWorld(99);
    stageBattle(st, 2, 1);
    // 记录所有指挥官经验
    std::vector<Fixed> before;
    for (const auto& c : st.commanders) before.push_back(c.experience);
    runTicks(st, 6);
    bool grew = false;
    for (std::size_t i = 0; i < st.commanders.size() && i < before.size(); ++i) {
        if (st.commanders[i].experience.rawValue() > before[i].rawValue()) grew = true;
    }
    CHECK(grew);
}

TEST(combat, org_never_negative_and_bounded) {
    GameState st = warWorld(111);
    stageBattle(st, 2, 1);
    runTicks(st, 20);
    for (const auto& f : st.fleets) {
        CHECK(f.org.rawValue() >= 0);
        CHECK(f.maxOrg.rawValue() > 0);
        CHECK(f.org.rawValue() <= f.maxOrg.rawValue() + FIX);
        CHECK(f.strength.rawValue() > 0);
    }
}

TEST(combat, battle_state_survives_serialization) {
    GameState st = warWorld(222);
    u32 sys2 = stageBattle(st, 2, 1);
    // 只调用 combatPhase（隔离 AI 调度），每 tick 维持接触
    for (int t = 0; t < 3; ++t) {
        if (!atWarWith(st, 2, 1)) declareWar(st, 2, 1, true);
        for (u32 id : st.empire(2)->fleets) {
            Fleet* f = st.fleet(id);
            if (f == nullptr) continue;
            f->system = sys2;
            f->targetSystem = kNoSystem;
        }
        for (u32 id : st.empire(1)->fleets) {
            Fleet* f = st.fleet(id);
            if (f == nullptr) continue;
            f->system = sys2;
            f->targetSystem = kNoSystem;
        }
        TickReport rep2;
        combatPhase(st, rep2);
    }
    CHECK(!st.battles.empty());
    CHECK(!st.commanders.empty());
    std::vector<u8> bytes = serializeState(st);
    GameState back;
    CHECK(tryDeserializeState(bytes, back));
    CHECK_EQ(back.battles.size(), st.battles.size());
    CHECK_EQ(back.commanders.size(), st.commanders.size());
    CHECK_EQ(back.nextBattleId, st.nextBattleId);
    for (std::size_t i = 0; i < st.battles.size(); ++i) {
        CHECK_EQ(back.battles[i].id, st.battles[i].id);
        CHECK_EQ(back.battles[i].ticks, st.battles[i].ticks);
        CHECK_EQ(back.battles[i].progress.rawValue(), st.battles[i].progress.rawValue());
    }
    for (std::size_t i = 0; i < st.commanders.size(); ++i) {
        CHECK_EQ(back.commanders[i].battlesWon, st.commanders[i].battlesWon);
        CHECK(back.commanders[i].name == st.commanders[i].name);
    }
}

TEST(combat, odds_function_is_bounded_and_monotone) {
    GameState st = warWorld(333);
    // 强化进攻方 ⇒ 胜率上升
    Fixed o1 = combatOdds(st, 2, 1);
    st.empires[2].military = st.empires[2].military * Fixed(5);
    for (u32 id : st.empire(2)->fleets) {
        Fleet* f = st.fleet(id);
        if (f != nullptr) f->strength = f->strength * Fixed(5);
    }
    Fixed o2 = combatOdds(st, 2, 1);
    CHECK(o2.rawValue() >= o1.rawValue());
    CHECK(o1.rawValue() >= Fixed::pct(2).rawValue());
    CHECK(o2.rawValue() <= Fixed::pct(98).rawValue());
}
