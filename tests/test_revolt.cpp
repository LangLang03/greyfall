// 起义与党派斗争
#include <algorithm>

#include "check.h"
#include "core/GameState.h"
#include "core/TickPipeline.h"
#include "domain/Empire.h"
#include "domain/Revolt.h"
#include "gen/WorldGen.h"
#include "plot/BeatResolver.h"
#include "save/Serde.h"

using namespace gf;

namespace {

GameState revWorld(u64 seed = 4242, int systems = 56) {
    WorldGenOptions o;
    o.seed = seed;
    o.difficulty = 3;
    o.empireCount = 8;
    o.systemCount = systems;
    GameState st;
    generateWorld(st, o);
    return st;
}

void revTicks(GameState& st, int n) {
    for (int i = 0; i < n; ++i) {
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);
    }
}

/// 找一个非首都星系
u32 nonCapitalSystem(const GameState& st, u32 empire) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return kNoSystem;
    for (u32 s : e->systems) {
        const SystemNode* n = st.system(s);
        if (n != nullptr && !n->capital) return s;
    }
    return kNoSystem;
}

/// 把帝国推到极端不满
void oppress(GameState& st, u32 empire) {
    Empire* e = st.empire(empire);
    if (e == nullptr) return;
    e->domestic.unrest = Fixed::pct(98);
    e->stability = Fixed::pct(2);
    for (auto& f : e->domestic.factions) {
        f.influence = Fixed::pct(45);
        f.satisfaction = Fixed::pct(5);
    }
}

}  // namespace

// 回归守卫：动乱原先只有「政变」一种，且只是一次性扣分 ——
// 民怨再高领土也不会动摇，玩家可以把民怨推到 100% 而毫发无伤。
TEST(revolt, stage_table_is_ordered) {
    for (int i = 0; i < static_cast<int>(RevoltStage::Count); ++i) {
        RevoltStage s = static_cast<RevoltStage>(i);
        CHECK(!revoltStageName(s).empty());
        CHECK(!revoltStageDesc(s).empty());
    }
}

TEST(revolt, risk_rises_with_unrest_and_falls_with_stability) {
    GameState st = revWorld(7301);
    u32 sys = nonCapitalSystem(st, kPlayerId);
    if (sys == kNoSystem) return;
    Empire& me = st.empires[kPlayerId];

    me.domestic.unrest = Fixed::pct(10);
    me.stability = Fixed::pct(80);
    Fixed low = revoltRiskOf(st, sys);

    me.domestic.unrest = Fixed::pct(95);
    me.stability = Fixed::pct(5);
    Fixed high = revoltRiskOf(st, sys);
    CHECK(high.rawValue() > low.rawValue());
    // 必须有上界
    CHECK(high.rawValue() <= Fixed(1).rawValue());
    // 首都更稳定
    const Empire* e2 = st.empire(kPlayerId);
    if (e2 != nullptr) {
        u32 cap = e2->capital;
        Fixed capRisk = revoltRiskOf(st, cap);
        CHECK(capRisk.rawValue() < high.rawValue());
    }
}

// 核心行为：极端压力下星系必须能走到「割据」并真的脱离
TEST(revolt, extreme_pressure_leads_to_secession) {
    GameState st = revWorld(7302);
    u32 sys = nonCapitalSystem(st, kPlayerId);
    if (sys == kNoSystem) return;
    Empire& me = st.empires[kPlayerId];
    const std::size_t before = me.systems.size();
    int maxStage = 0;
    for (int t = 0; t < 80; ++t) {
        oppress(st, kPlayerId);
        revTicks(st, 1);
        maxStage = std::max(maxStage, static_cast<int>(revoltStageOf(st, sys)));
        const SystemNode* n = st.system(sys);
        if (n == nullptr || n->owner != kPlayerId) break;
    }
    const SystemNode* n = st.system(sys);
    CHECK(n != nullptr);
    // 要么已脱离，要么至少到过割据阶段
    bool left = n->owner != kPlayerId;
    CHECK(left || maxStage >= static_cast<int>(RevoltStage::Secession));
    if (left) {
        CHECK(st.empires[kPlayerId].systems.size() < before);
    }
}

TEST(revolt, suppression_reduces_severity) {
    GameState st = revWorld(7303);
    u32 sys = nonCapitalSystem(st, kPlayerId);
    if (sys == kNoSystem) return;
    Empire& me = st.empires[kPlayerId];
    // 推到动乱
    for (int t = 0; t < 12; ++t) {
        oppress(st, kPlayerId);
        revTicks(st, 1);
        if (revoltAt(st, sys) != nullptr) break;
    }
    const Revolt* r = revoltAt(st, sys);
    if (r == nullptr) return;
    Fixed before = r->severity;
    me.treasury = Fixed(500000);
    me.military = Fixed(5000);
    std::string msg;
    CHECK(suppressRevolt(st, kPlayerId, sys, &msg));
    const Revolt* r2 = revoltAt(st, sys);
    if (r2 != nullptr) CHECK(r2->severity.rawValue() < before.rawValue());
    // 资源不足时失败
    me.treasury = Fixed(0);
    CHECK(!suppressRevolt(st, kPlayerId, sys, &msg));
    // 非己方星系被拒
    CHECK(!suppressRevolt(st, kPlayerId, 99999, &msg));
}

TEST(revolt, normal_play_rarely_revolts) {
    // 正常玩法下动乱应当罕见，否则系统会淹没游戏
    GameState st = revWorld(7304);
    int maxRevolts = 0;
    for (int t = 0; t < 200; ++t) {
        revTicks(st, 1);
        maxRevolts = std::max(maxRevolts, static_cast<int>(st.revolts.size()));
    }
    CHECK(maxRevolts <= 5);
}

// 党派斗争：最强派系在影响力高、满意度低时发出最后通牒
TEST(revolt, faction_ultimatum_and_answers) {
    GameState st = revWorld(7305);
    Empire& me = st.empires[kPlayerId];
    if (me.domestic.factions.empty()) return;
    FactionKind which = FactionKind::Count;
    // 初始不该有通牒
    for (auto& f : me.domestic.factions) {
        f.influence = Fixed::pct(10);
        f.satisfaction = Fixed::pct(60);
    }
    CHECK(!factionUltimatumPending(st, kPlayerId, &which));
    // 制造一个强势且不满的派系
    me.domestic.factions.front().influence = Fixed::pct(45);
    me.domestic.factions.front().satisfaction = Fixed::pct(10);
    FactionKind target = me.domestic.factions.front().kind;
    CHECK(factionUltimatumPending(st, kPlayerId, &which));
    CHECK(which == target);

    // 让步：满意度上升，其他派系下降
    me.treasury = Fixed(500000);
    std::string msg;
    CHECK(answerUltimatum(st, kPlayerId, true, &msg));
    CHECK(!msg.empty());
    for (const auto& f : me.domestic.factions)
        if (f.kind == target) CHECK(f.satisfaction.rawValue() > Fixed::pct(10).rawValue());

    // 拒绝：满意度暴跌、民怨上升
    me.domestic.factions.front().satisfaction = Fixed::pct(10);
    Fixed unrest0 = me.domestic.unrest;
    CHECK(answerUltimatum(st, kPlayerId, false, &msg));
    CHECK(me.domestic.unrest.rawValue() > unrest0.rawValue());
}

TEST(revolt, state_survives_serialization) {
    GameState st = revWorld(7306);
    u32 sys = nonCapitalSystem(st, kPlayerId);
    if (sys == kNoSystem) return;
    for (int t = 0; t < 12; ++t) {
        oppress(st, kPlayerId);
        revTicks(st, 1);
        if (revoltAt(st, sys) != nullptr) break;
    }
    std::vector<u8> bytes = serializeState(st);
    GameState back;
    CHECK(tryDeserializeState(bytes, back));
    CHECK_EQ(back.revolts.size(), st.revolts.size());
    CHECK_EQ(back.stateHash(), st.stateHash());
    revTicks(st, 6);
    revTicks(back, 6);
    CHECK_EQ(st.stateHash(), back.stateHash());
}
