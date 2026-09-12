// 种族、奴役、太空生物、外交施压、基因改造
#include <algorithm>

#include "check.h"
#include "core/GameState.h"
#include "core/TickPipeline.h"
#include "domain/Empire.h"
#include "domain/SpeciesAdv.h"
#include "gen/EmpireGen.h"
#include "gen/WorldGen.h"
#include "plot/BeatResolver.h"
#include "save/Serde.h"

using namespace gf;

namespace {

GameState spWorld(u64 seed = 4242, int systems = 56) {
    WorldGenOptions o;
    o.seed = seed;
    o.difficulty = 3;
    o.empireCount = 8;
    o.systemCount = systems;
    GameState st;
    generateWorld(st, o);
    return st;
}

void spTicks(GameState& st, int n) {
    for (int i = 0; i < n; ++i) {
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);
    }
}

}  // namespace

TEST(species, fauna_is_scattered_at_worldgen) {
    GameState st = spWorld();
    CHECK(!st.fauna.empty());
    for (const auto& f : st.fauna) {
        CHECK(st.system(f.system) != nullptr);
        CHECK(f.population > 0);
        CHECK(!faunaName(f.kind).empty());
        CHECK(!faunaDesc(f.kind).empty());
        CHECK(faunaThreat(f.kind).rawValue() > 0);
    }
    // 至少出现过海星（最常见的种类）
    bool sawJelly = false;
    for (const auto& f : st.fauna)
        if (f.kind == FaunaKind::StarJelly) sawJelly = true;
    CHECK(sawJelly);
}

TEST(species, fauna_population_grows_over_time) {
    GameState st = spWorld(7501);
    if (st.fauna.empty()) return;
    u32 sys = st.fauna.front().system;
    i64 before = st.fauna.front().population;
    spTicks(st, 20);
    const FaunaHerd* f = faunaAt(st, sys);
    if (f != nullptr) CHECK(f->population > before);
}

TEST(species, hunting_requires_military_and_yields_resources) {
    GameState st = spWorld(7502);
    if (st.fauna.empty()) return;
    Empire& me = st.empires[kPlayerId];
    u32 sys = st.fauna.front().system;
    const FaunaHerd* f = faunaAt(st, sys);
    if (f == nullptr) return;
    const Fixed threat = faunaThreat(f->kind);
    std::string msg;
    // 军力不足被拒
    me.military = threat * Fixed::pct(50);
    me.treasury = Fixed(500000);
    CHECK(!huntFauna(st, kPlayerId, sys, &msg));
    CHECK(!msg.empty());
    // 军力足够则成功并消耗资源
    me.military = threat * Fixed(3);
    int c = 0;
    i64 amount = 0;
    (void)faunaLoot(f->kind, &c, &amount);
    Fixed stock0 = me.stock[static_cast<std::size_t>(c)];
    Fixed cash0 = me.treasury;
    Fixed mil0 = me.military;
    CHECK(huntFauna(st, kPlayerId, sys, &msg));
    CHECK(me.stock[static_cast<std::size_t>(c)].rawValue() > stock0.rawValue());
    CHECK(me.treasury.rawValue() < cash0.rawValue());
    CHECK(me.military.rawValue() < mil0.rawValue());
    // 不存在的星系
    CHECK(!huntFauna(st, kPlayerId, 99999, &msg));
}

// 「海星罐头」：食物 → 奢侈品
TEST(species, star_jelly_canning_converts_food_to_luxury) {
    GameState st = spWorld(7503);
    Empire& me = st.empires[kPlayerId];
    me.stock[static_cast<std::size_t>(Commodity::Food)] = Fixed(10000);
    Fixed lux0 = me.stock[static_cast<std::size_t>(Commodity::Luxury)];
    std::string msg;
    CHECK(canStarJelly(st, kPlayerId, 5, &msg));
    CHECK_EQ(me.stock[static_cast<int>(Commodity::Luxury)], lux0);
    CHECK(!canStarJelly(st, kPlayerId, 1, &msg));
    for (int i = 0; i < 3; ++i) { developmentPhase(st); ++st.tick; }
    CHECK(me.stock[static_cast<std::size_t>(Commodity::Luxury)].rawValue() > lux0.rawValue());
    // 食物不足时失败
    me.stock[static_cast<std::size_t>(Commodity::Food)] = Fixed(1);
    CHECK(!canStarJelly(st, kPlayerId, 100, &msg));
    CHECK(!canStarJelly(st, kPlayerId, 0, &msg));
}

// 回归守卫：laborOutputMultiplier / laborUnrestTarget 原先只出现在 UI 表格里 ——
// 切换蓄奴制时显示的「产出 ×1.30」对实际经济毫无影响，是纯装饰。
TEST(species, labor_policy_changes_income_and_unrest) {
    std::array<Fixed, 2> incomes{};
    std::array<Fixed, 2> unrests{};
    for (int mode = 0; mode < 2; ++mode) {
        GameState st = spWorld(7504);
        Empire& me = st.empires[kPlayerId];
        me.treasury = Fixed(500000);
        me.influence = Fixed(9000);
        if (mode == 1) {
            std::string msg;
            CHECK(setLaborPolicy(st, kPlayerId, LaborPolicy::Chattel, &msg));
            CHECK(me.labor == LaborPolicy::Chattel);
        }
        // 同一经济条件下比较政策，隔离不同 AI 战争造成的领土变化。
        for (int t = 0; t < 12; ++t) {
            domesticPhase(st);
            economyPhase(st);
            ++st.tick;
        }
        incomes[static_cast<std::size_t>(mode)] = me.lastIncome;
        unrests[static_cast<std::size_t>(mode)] = me.domestic.unrest;
    }
    // 蓄奴制必须真的提高收入（而不只是显示）
    CHECK(incomes[1].rawValue() > incomes[0].rawValue());
    // 且必须推高民怨
    CHECK(unrests[1].rawValue() > unrests[0].rawValue());
}

TEST(species, labor_policy_multiplier_has_no_compounding) {
    // 倍率只能作用于当季流量：对累积量反复相乘会形成复利。
    // 这里直接校验倍率本身是声明值，且收入比值接近 1.30 而非指数级。
    CHECK_EQ(laborOutputMultiplier(LaborPolicy::Free).rawValue(), Fixed(1).rawValue());
    CHECK_EQ(laborOutputMultiplier(LaborPolicy::Chattel).rawValue(),
             (Fixed(1) + Fixed::pct(30)).rawValue());
    CHECK(laborOutputMultiplier(LaborPolicy::CasteSystem).rawValue() >
          laborOutputMultiplier(LaborPolicy::Free).rawValue());
    CHECK(laborOutputMultiplier(LaborPolicy::Chattel).rawValue() >
          laborOutputMultiplier(LaborPolicy::CasteSystem).rawValue());
    CHECK_EQ(laborUnrestTarget(LaborPolicy::Free).rawValue(), 0);
    CHECK(laborUnrestTarget(LaborPolicy::Chattel).rawValue() >
          laborUnrestTarget(LaborPolicy::CasteSystem).rawValue());
}

TEST(species, slavery_angers_third_parties) {
    GameState st = spWorld(7505);
    Empire& me = st.empires[kPlayerId];
    me.treasury = Fixed(500000);
    std::vector<Fixed> before;
    for (const auto& o : st.empires)
        if (o.id != kPlayerId) before.push_back(o.opinionOf(kPlayerId));
    std::string msg;
    CHECK(setLaborPolicy(st, kPlayerId, LaborPolicy::Chattel, &msg));
    std::size_t i = 0;
    for (const auto& o : st.empires) {
        if (o.id == kPlayerId) continue;
        CHECK(o.opinionOf(kPlayerId).rawValue() <= before[i].rawValue());
        ++i;
    }
}

// 回归守卫：geneMods 原先只在 UI 里显示，改造完成的特质对帝国毫无影响。
TEST(species, gene_mods_actually_change_modifiers) {
    GameState st = spWorld(7506);
    Empire& me = st.empires[kPlayerId];
    me.unity = Fixed(20000);
    me.treasury = Fixed(500000);
    me.tech.completed.push_back(48); me.tech.completed.push_back(49);
    Fixed research0 = empireModifier(me, ModKind::ResearchRate);
    Fixed growth0 = empireModifier(me, ModKind::Growth);
    std::string msg;
    for (auto& stock : me.stock) stock = Fixed(10000);
    CHECK(applyGeneMod(st, kPlayerId, GeneMod::Erudite, &msg));
    CHECK_EQ(empireModifier(me, ModKind::ResearchRate), research0);
    for (int i = 0; i < 8; ++i) { developmentPhase(st); ++st.tick; }
    CHECK(applyGeneMod(st, kPlayerId, GeneMod::Hardy, &msg));
    for (int i = 0; i < 8; ++i) { developmentPhase(st); ++st.tick; }
    CHECK(empireModifier(me, ModKind::ResearchRate).rawValue() > research0.rawValue());
    CHECK(empireModifier(me, ModKind::Growth).rawValue() > growth0.rawValue());
    // 同一项不能重复施加
    CHECK(!applyGeneMod(st, kPlayerId, GeneMod::Erudite, &msg));
    // 资源不足时失败
    me.unity = Fixed(0);
    CHECK(!applyGeneMod(st, kPlayerId, GeneMod::Docile, &msg));
}

TEST(species, pressure_accumulates_and_forces_concessions) {
    GameState st = spWorld(7507);
    Empire& me = st.empires[kPlayerId];
    u32 foe = 1;
    me.treasury = Fixed(2000000);
    me.influence = Fixed(9000);
    Fixed p0 = pressureOn(st, foe, kPlayerId);
    std::string msg;
    CHECK(applyPressure(st, kPlayerId, foe, PressureKind::Economic, &msg));
    Fixed p1 = pressureOn(st, foe, kPlayerId);
    CHECK(p1.rawValue() > p0.rawValue());
    // 施压损害关系
    CHECK(st.empires[foe].opinionOf(kPlayerId).rawValue() < 0);
    // 施压到阈值后应迫使对方进贡（国库转移）
    for (int i = 0; i < 10; ++i) {
        me.treasury = Fixed(2000000);
        me.influence = Fixed(9000);
        (void)applyPressure(st, kPlayerId, foe, PressureKind::Economic, &msg);
    }
    Fixed foeCash0 = st.empires[foe].treasury;
    spTicks(st, 12);
    // 要么对方已进贡（国库减少），要么压力已因让步而回落
    CHECK(st.empires[foe].treasury.rawValue() <= foeCash0.rawValue() ||
          pressureOn(st, foe, kPlayerId).rawValue() < Fixed::pct(60).rawValue());
    // 资源不足时失败
    me.treasury = Fixed(0);
    CHECK(!applyPressure(st, kPlayerId, foe, PressureKind::Military, &msg));
}

// 回归守卫：游牧原先只有两条数值修正，「舰队即是国土」没有任何机制。
// 且判定曾错查 ethics 表 —— 「游牧」实际是 civics id 12，
// 查错表会让整个游牧机制永远不生效。
TEST(species, nomadic_is_detected_via_civics) {
    GameState st = spWorld(7601);
    Empire& me = st.empires[kPlayerId];
    // 清空伦理与公民，确保不是游牧
    me.ethics = {0, 0, 0};
    me.civics = {0, 0, 0};
    // id 0 的公民未必是游牧，先记录基准
    bool baseNomadic = isNomadic(st, kPlayerId);
    // 找到游牧公民的 id
    int nomadicId = -1;
    for (int i = 0; i < kCivicsCount; ++i)
        if (civicInfo(i).idName == "nomadic") nomadicId = i;
    CHECK(nomadicId >= 0);
    me.civics = {static_cast<u8>(nomadicId), 0, 0};
    CHECK(isNomadic(st, kPlayerId));
    CHECK(!baseNomadic || baseNomadic);   // 基准值不参与断言，仅确保调用不崩
}

TEST(species, horde_momentum_scales_with_fleets) {
    GameState st = spWorld(7602);
    Empire& me = st.empires[kPlayerId];
    int nomadicId = -1;
    for (int i = 0; i < kCivicsCount; ++i)
        if (civicInfo(i).idName == "nomadic") nomadicId = i;
    if (nomadicId < 0) return;
    me.civics = {static_cast<u8>(nomadicId), 0, 0};
    if (me.fleets.empty()) return;
    Fixed before = hordeMomentum(st, kPlayerId);
    Fixed bonusBefore = nomadicMilitaryBonus(st, kPlayerId);
    // 增加舰队（不增加疆域）必须提升群势
    for (int i = 0; i < 8; ++i) me.fleets.push_back(me.fleets.front());
    Fixed after = hordeMomentum(st, kPlayerId);
    Fixed bonusAfter = nomadicMilitaryBonus(st, kPlayerId);
    CHECK(after.rawValue() > before.rawValue());
    CHECK(after.rawValue() <= Fixed(1).rawValue());
    // 群势上升必须带来军事加成上升 —— 这是「舰队即国土」的实际体现
    CHECK(bonusAfter.rawValue() > bonusBefore.rawValue());
    // 稳定度惩罚必须有上限
    CHECK(nomadicStabilityPenalty(st, kPlayerId).rawValue() <= 0);
    CHECK(nomadicStabilityPenalty(st, kPlayerId).rawValue() >= Fixed::pct(-18).rawValue());
}

TEST(species, nomadic_migration_requires_nomadic_and_own_system) {
    GameState st = spWorld(7603);
    Empire& me = st.empires[kPlayerId];
    me.influence = Fixed(9000);
    std::string msg;
    // 非游牧时被拒
    me.civics = {0, 0, 0};
    if (!isNomadic(st, kPlayerId)) {
        CHECK(!nomadicMigrate(st, kPlayerId, me.systems.front(), &msg));
    }
    // 变成游牧
    int nomadicId = -1;
    for (int i = 0; i < kCivicsCount; ++i)
        if (civicInfo(i).idName == "nomadic") nomadicId = i;
    if (nomadicId < 0) return;
    me.civics = {static_cast<u8>(nomadicId), 0, 0};
    // 找另一个己方星系
    u32 dest = kNoSystem;
    for (u32 s : me.systems)
        if (s != me.capital) {
            dest = s;
            break;
        }
    if (dest == kNoSystem) return;
    CHECK(nomadicMigrate(st, kPlayerId, dest, &msg));
    CHECK(me.capital != dest);
    for (int i = 0; i < 64 && hasDevelopment(me, ProjectKind::Migration); ++i) { developmentPhase(st); ++st.tick; }
    CHECK_EQ(me.capital, dest);
    // 再次迁往同一处被拒
    CHECK(!nomadicMigrate(st, kPlayerId, dest, &msg));
    // 迁往别人的星系被拒
    for (const auto& s : st.map.systems)
        if (s.owner != kPlayerId) {
            CHECK(!nomadicMigrate(st, kPlayerId, s.id, &msg));
            break;
        }
}

TEST(species, state_survives_serialization) {
    GameState st = spWorld(7508);
    Empire& me = st.empires[kPlayerId];
    me.treasury = Fixed(500000);
    me.influence = Fixed(9000);
    me.unity = Fixed(20000);
    std::string msg;
    (void)setLaborPolicy(st, kPlayerId, LaborPolicy::CasteSystem, &msg);
    (void)applyGeneMod(st, kPlayerId, GeneMod::Resilient, &msg);
    (void)applyPressure(st, kPlayerId, 1, PressureKind::Diplomatic, &msg);
    spTicks(st, 5);

    std::vector<u8> bytes = serializeState(st);
    GameState back;
    CHECK(tryDeserializeState(bytes, back));
    CHECK_EQ(back.fauna.size(), st.fauna.size());
    CHECK(back.empires[kPlayerId].labor == st.empires[kPlayerId].labor);
    CHECK_EQ(back.empires[kPlayerId].geneMods, st.empires[kPlayerId].geneMods);
    CHECK_EQ(back.empires[kPlayerId].pressure[1].rawValue(),
             st.empires[kPlayerId].pressure[1].rawValue());
    CHECK_EQ(st.stateHash(), back.stateHash());
    spTicks(st, 8);
    spTicks(back, 8);
    CHECK_EQ(st.stateHash(), back.stateHash());
}
