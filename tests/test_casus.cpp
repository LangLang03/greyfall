// 正当战争理由与战争疲劳
#include <algorithm>

#include "check.h"
#include "core/GameState.h"
#include "core/TickPipeline.h"
#include "domain/CasusBelli.h"
#include "domain/Treaty.h"
#include "gen/WorldGen.h"
#include "plot/BeatResolver.h"
#include "save/Serde.h"

using namespace gf;

namespace {

GameState casusWorld(u64 seed = 4242, int systems = 56) {
    WorldGenOptions o;
    o.seed = seed;
    o.difficulty = 3;
    o.empireCount = 8;
    o.systemCount = systems;
    GameState st;
    generateWorld(st, o);
    return st;
}

void casusRunTicks(GameState& st, int n) {
    for (int i = 0; i < n; ++i) {
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);
    }
}

/// 找一个与玩家接壤的帝国
u32 borderingFoe(const GameState& st) {
    for (const auto& e : st.empires) {
        if (e.isPlayer || !e.alive) continue;
        for (u32 sid : e.systems) {
            const SystemNode* s = st.system(sid);
            if (s == nullptr) continue;
            for (u32 nx : s->links) {
                const SystemNode* m = st.system(nx);
                if (m != nullptr && m->owner == kPlayerId) return e.id;
            }
        }
    }
    return kNoEmpire;
}

}  // namespace

// 回归守卫：宣战原本没有任何门槛 —— 可以随时对任何国家开战，
// 最优解因此永远是「尽快开打」，这是开局零伤亡打全图的制度性原因。
TEST(casus, territorial_dispute_arises_from_adjacency) {
    GameState st = casusWorld();
    u32 foe = borderingFoe(st);
    if (foe == kNoEmpire) return;
    // 保持边界不变，只推进理由生成，避免战争吞并先清除测试目标。
    for (int i = 0; i < 40; ++i) { casusBelliPhase(st); ++st.tick; }
    const CasusBelli* cb = findCasusBelli(st, kPlayerId, foe);
    CHECK(cb != nullptr);
    if (cb == nullptr) return;
    CHECK(cb->kind == CasusBelliKind::TerritorialDispute);
    CHECK(!cb->note.empty());
}

TEST(casus, fabrication_costs_influence_and_expires) {
    GameState st = casusWorld(777);
    u32 foe = 1;
    st.empires[kPlayerId].influence = Fixed(2000);
    // 先清掉可能已有的理由
    clearCasusBelli(st, kPlayerId, foe);
    Fixed before = st.empires[kPlayerId].influence;
    std::string msg;
    CHECK(fabricateClaim(st, kPlayerId, foe, &msg));
    CHECK(st.empires[kPlayerId].influence.rawValue() < before.rawValue());
    const CasusBelli* cb = findCasusBelli(st, kPlayerId, foe);
    CHECK(cb != nullptr);
    CHECK(cb->kind == CasusBelliKind::FabricatedClaim);
    CHECK(cb->expireTick > 0);           // 伪造的宣称会过期
    // 影响力不足时失败
    clearCasusBelli(st, kPlayerId, foe);
    st.empires[kPlayerId].influence = Fixed(1);
    CHECK(!fabricateClaim(st, kPlayerId, foe, &msg));
    CHECK(!msg.empty());
}

TEST(casus, peace_clears_reasons) {
    GameState st = casusWorld(888);
    u32 foe = 1;
    st.empires[kPlayerId].influence = Fixed(2000);
    clearCasusBelli(st, kPlayerId, foe);
    std::string msg;
    CHECK(fabricateClaim(st, kPlayerId, foe, &msg));
    CHECK(hasCasusBelli(st, kPlayerId, foe));
    clearCasusBelli(st, kPlayerId, foe);
    CHECK(!hasCasusBelli(st, kPlayerId, foe));
}

// 战争疲劳：随战争上升，触发反战阈值，停战后清除
TEST(casus, war_weariness_accumulates_and_clears_on_peace) {
    GameState st = casusWorld(999);
    u32 foe = borderingFoe(st);
    if (foe == kNoEmpire) foe = 1;
    declareWar(st, kPlayerId, foe, true);
    // 战争现在**必然收敛**（Peace.cpp 有战争疲劳上限与自动清算），
    // 因此 60 季后不能假定这场战争还在进行 —— 疲劳峰值必须在推进过程中采集。
    // 旧写法直接断言「60 季后仍存在该对手的疲劳记录」并取其引用，
    // 隐含要求战争永不结束（与设计冲突），且对 vector 元素取指针后
    // 继续调用（可能重分配的）阶段函数属于悬垂引用 —— 实测 SIGSEGV。
    Fixed peak = Fixed(0);
    bool seen = false;
    for (int i = 0; i < 60; ++i) {
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);
        for (const auto& x : st.empires[kPlayerId].weariness)
            if (x.enemy == foe) {
                seen = true;
                peak = fxMax(peak, x.value);
            }
    }
    CHECK(seen);
    CHECK(peak.rawValue() > 0);
    // 疲劳必须有上界（不能越界累加）
    CHECK(peak.rawValue() <= Fixed(1).rawValue());
    // 减益方向契约：只要稳定度减益存在，民怨项就必须非负
    if (wearinessStabilityPenalty(st, kPlayerId).rawValue() < 0)
        CHECK(wearinessUnrestPenalty(st, kPlayerId).rawValue() >= 0);

    // 停战后：**对该对手**的疲劳记录清空。
    // 注意不能断言「全局减益归零」—— 起义系统可能让玩家同时与别人交战，
    // 那些战争的疲劳是独立的，不该由本用例负责。
    declareWar(st, kPlayerId, foe, false);
    casusRunTicks(st, 3);
    bool stillThere = false;
    for (const auto& x : st.empires[kPlayerId].weariness)
        if (x.enemy == foe) stillThere = true;
    CHECK(!stillThere);
    // 逐条检查：凡仍处于交战状态的对手才会有减益
    for (const auto& x : st.empires[kPlayerId].weariness) {
        if (!atWarWith(st, kPlayerId, x.enemy)) {
            // 不在交战的记录不该产生减益（会在下一 tick 被清理）
            continue;
        }
        // 交战中：允许有减益，只需确认它不是 foe
        CHECK(x.enemy != foe);
    }
}

TEST(casus, weariness_penalties_are_bounded) {
    // 减益必须有上限，否则长期战争会把稳定度打到不可恢复
    GameState st = casusWorld(1234);
    u32 foe = 1;
    declareWar(st, kPlayerId, foe, true);
    Empire& me = st.empires[kPlayerId];
    WarWeariness w;
    w.enemy = foe;
    w.value = Fixed(1);
    w.startTick = 0;
    w.peaceDemanded = true;
    me.weariness.clear();
    me.weariness.push_back(w);
    Fixed sp = wearinessStabilityPenalty(st, kPlayerId);
    Fixed up = wearinessUnrestPenalty(st, kPlayerId);
    Fixed sap = wearinessSatisfactionPenalty(st, kPlayerId);
    CHECK(sp.rawValue() < 0);
    CHECK(sp.rawValue() >= Fixed::pct(-20).rawValue());   // 不超过 -20%
    CHECK(up.rawValue() > 0);
    CHECK(up.rawValue() <= Fixed::pct(15).rawValue());    // 不超过 +15%
    CHECK(sap.rawValue() >= 0);
    CHECK(sap.rawValue() <= Fixed::pct(25).rawValue());
}

TEST(casus, state_survives_serialization) {
    GameState st = casusWorld(555);
    u32 foe = 1;
    st.empires[kPlayerId].influence = Fixed(2000);
    clearCasusBelli(st, kPlayerId, foe);
    std::string msg;
    (void)fabricateClaim(st, kPlayerId, foe, &msg);
    declareWar(st, kPlayerId, foe, true);
    casusRunTicks(st, 20);

    std::vector<u8> bytes = serializeState(st);
    GameState back;
    CHECK(tryDeserializeState(bytes, back));
    CHECK_EQ(back.empires[kPlayerId].casusBelli.size(), st.empires[kPlayerId].casusBelli.size());
    CHECK_EQ(back.empires[kPlayerId].weariness.size(), st.empires[kPlayerId].weariness.size());
    for (std::size_t i = 0; i < st.empires[kPlayerId].weariness.size(); ++i) {
        CHECK_EQ(back.empires[kPlayerId].weariness[i].enemy,
                 st.empires[kPlayerId].weariness[i].enemy);
        CHECK_EQ(back.empires[kPlayerId].weariness[i].value.rawValue(),
                 st.empires[kPlayerId].weariness[i].value.rawValue());
    }
}
