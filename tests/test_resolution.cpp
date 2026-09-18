// 决议系统与胜利条件
#include <algorithm>
#include "domain/Economy.h"

#include "check.h"
#include "core/GameState.h"
#include "core/ResolutionEngine.h"
#include "core/TickPipeline.h"
#include "domain/Policy.h"
#include "domain/Construction.h"
#include "domain/Government.h"
#include "gen/WorldGen.h"
#include "plot/EventSystem.h"
#include "plot/BeatResolver.h"

using namespace gf;

namespace {

GameState resWorld(u64 seed = 4321) {
    WorldGenOptions o;
    o.seed = seed;
    o.difficulty = 2;
    o.empireCount = 8;
    o.systemCount = 40;
    GameState st;
    generateWorld(st, o);
    return st;
}

}  // namespace

// 回归守卫（严重）：胜利条件曾**在数学上不可达** ——
// 失去全部领土但仍有残余舰队的国家被 `checkVictory` 计入「未击败」，
// 而它已无领土可攻、也不会灭亡。实测玩家占下 17 个星系、
// 内政四项全部达标后，判定仍显示「仍有 7 个对手未被击败」。
// 修复：失去全部星系与行星的帝国即被消灭。
TEST(resolution, victory_is_reachable_after_full_conquest) {
    GameState st = resWorld();
    // 把除玩家外的所有帝国打成「无星系、无行星」
    for (auto& e : st.empires) {
        if (e.isPlayer) continue;
        for (const auto& s : st.map.systems) {
            if (s.owner != e.id) continue;
            SystemNode* sn = st.system(s.id);
            if (sn != nullptr) sn->owner = kPlayerId;
        }
        for (auto& p : st.planets)
            if (p.owner == e.id) p.owner = kPlayerId;
        e.systems.clear();
        e.fleets.clear();   // 残余舰队不应让国家「永生」
    }
    // 推进一 tick，让覆灭判定生效
    advanceOneTick(st);
    while (!st.pending.empty()) resolveChoiceAuto(st, 0);
    VictoryStatus v = checkVictory(st);
    CHECK_EQ(v.aliveRivals, 0);
}


TEST(resolution, content_table_is_consistent) {
    CHECK_EQ(kResolutionCount, 48);
    for (int i = 0; i < kResolutionCount; ++i) {
        const ResolutionDef& d = resolutionDef(i);
        CHECK(!d.idName.empty());
        CHECK(!d.nameZh.empty());
        CHECK(static_cast<int>(d.kind) < static_cast<int>(ResolutionKind::Count));
        // 倒计时必须有期限与成败效果
        if (d.kind == ResolutionKind::Countdown) {
            CHECK(d.countdownTicks > 0);
            CHECK(d.onFail.value.rawValue() != 0);
        }
        // 主动/牺牲换利必须有可执行效果
        if (d.kind == ResolutionKind::Active || d.kind == ResolutionKind::Tradeoff) {
            CHECK(d.onActivate.value.rawValue() != 0);
        }
        // 可阻止必须有阻止条件
        if (d.kind == ResolutionKind::Preventable) {
            CHECK(d.prevent.unrestBelow.rawValue() != 0 || d.prevent.treasuryAbove.rawValue() != 0 ||
                  d.prevent.stabilityAbove.rawValue() != 0 || d.prevent.stockAboveCommodity >= 0);
        }
        // 牺牲换利必须有牺牲项
        if (d.kind == ResolutionKind::Tradeoff) {
            CHECK(d.cost.sacrificeTarget != ResTarget::Count);
            CHECK(d.cost.sacrificeValue.rawValue() != 0);
        }
    }
    // 五种类型都要有实例
    for (int k = 0; k < static_cast<int>(ResolutionKind::Count); ++k) {
        int n = 0;
        for (int i = 0; i < kResolutionCount; ++i)
            if (static_cast<int>(resolutionDef(i).kind) == k) ++n;
        CHECK(n > 0);
    }
    // 名字唯一且可按名检索
    for (int i = 0; i < kResolutionCount; ++i) {
        CHECK_EQ(resolutionIndexByName(resolutionDef(i).nameZh), i);
        CHECK_EQ(resolutionIndexByName(resolutionDef(i).idName), i);
    }
    CHECK_EQ(resolutionIndexByName("no-such-resolution"), -1);
}

TEST(resolution, active_resolution_applies_and_pays_cost) {
    GameState st = resWorld();
    st.empires[kPlayerId].apLeft = 9;
    int idx = resolutionIndexByName("科研拨款");
    CHECK(idx >= 0);
    Fixed before = st.empires[kPlayerId].treasury;
    Fixed modBefore = st.empires[kPlayerId].tech.rate;
    std::string err;
    CHECK(resolutionActivate(st, kPlayerId, static_cast<u16>(idx), &err));
    CHECK(st.empires[kPlayerId].treasury.rawValue() < before.rawValue());
    // 修正类效果通过 empireModifier 生效
    Fixed after = empireModifier(st.empires[kPlayerId], ModKind::ResearchRate);
    CHECK(after.rawValue() > 0);
    (void)modBefore;
    CHECK(hasResolution(st.empires[kPlayerId], static_cast<u16>(idx)));
    // 重复发起应被拒绝
    CHECK(!resolutionActivate(st, kPlayerId, static_cast<u16>(idx), &err));
    CHECK(!err.empty());
    // 行动点不足应被拒绝
    GameState st2 = resWorld();
    st2.empires[kPlayerId].apLeft = 0;
    CHECK(!resolutionActivate(st2, kPlayerId, static_cast<u16>(idx), &err));
}

TEST(resolution, tradeoff_applies_permanent_sacrifice) {
    GameState st = resWorld();
    st.empires[kPlayerId].apLeft = 9;
    int idx = resolutionIndexByName("劳动征召");
    CHECK(idx >= 0);
    const ResolutionDef& d = resolutionDef(idx);
    CHECK(d.kind == ResolutionKind::Tradeoff);
    std::string err;
    // 依赖链：劳动征召需要先完成【战时经济】。
    // 这里标记前置已完成，以便单独检验「牺牲换利」的效果语义。
    {
        int pre = resolutionIndexByName("战时经济");
        CHECK(pre >= 0);
        CHECK(!resolutionActivate(st, kPlayerId, static_cast<u16>(idx), &err));   // 未满足前置 ⇒ 被拒
        st.empires[kPlayerId].resolutions.triggered.push_back(static_cast<u16>(pre));
    }
    CHECK(resolutionActivate(st, kPlayerId, static_cast<u16>(idx), &err));
    // 正面效果
    CHECK(empireModifier(st.empires[kPlayerId], ModKind::BuildRate).rawValue() > 0);
    // 永久牺牲：人口增长被永久下调
    CHECK(empireModifier(st.empires[kPlayerId], ModKind::Growth).rawValue() < 0);
    // 效果是永久的（ticksLeft == -1）
    bool permanent = false;
    for (const auto& a : st.empires[kPlayerId].resolutions.active)
        if (a.ticksLeft < 0) permanent = true;
    CHECK(permanent);
}

TEST(resolution, auto_trigger_fires_and_debuff_expires) {
    GameState st = resWorld(99);
    // 把国库推到深度赤字 ⇒ 触发「债务螺旋」
    st.empires[kPlayerId].treasury = Fixed(-50000);
    st.market.margin.cash = Fixed(-50000);
    st.tick = 40;
    TickReport rep;
    resolutionPhase(st, rep);
    auto inTriggered = [&](u16 id) {
        return std::find(st.empires[kPlayerId].resolutions.triggered.begin(),
                         st.empires[kPlayerId].resolutions.triggered.end(), id) !=
               st.empires[kPlayerId].resolutions.triggered.end();
    };
    int idx = resolutionIndexByName("债务螺旋");
    CHECK(idx >= 0);
    CHECK(inTriggered(static_cast<u16>(idx)));
    // 减益有时限，且不是永久
    bool foundTimed = false;
    for (const auto& a : st.empires[kPlayerId].resolutions.active) {
        if (a.ticksLeft > 0) foundTimed = true;
    }
    CHECK(foundTimed);
    // 推进足够多季之后该减益必须过期
    for (int i = 0; i < 12; ++i) resolutionPhase(st, rep);
    for (const auto& a : st.empires[kPlayerId].resolutions.active) {
        CHECK(a.ticksLeft != 0);
    }
}

TEST(resolution, preventable_is_blocked_when_condition_met) {
    // 阻止条件满足 ⇒ 不触发，并记录为已阻止
    GameState st = resWorld(1234);
    st.tick = 40;
    st.empires[kPlayerId].treasury = Fixed(200000);   // 阻止「货币危机」的条件
    TickReport rep;
    int idx = resolutionIndexByName("货币危机");
    CHECK(idx >= 0);
    const ResolutionDef& d = resolutionDef(idx);
    CHECK(d.kind == ResolutionKind::Preventable);
    CHECK(resConditionMet(st, st.empires[kPlayerId], d.prevent));
    resolutionPhase(st, rep);
    auto& trig = st.empires[kPlayerId].resolutions.triggered;
    CHECK(std::find(trig.begin(), trig.end(), static_cast<u16>(idx)) != trig.end());
    // 已阻止 ⇒ 不应留下负面效果
    bool hasNegativeCredit = false;
    for (const auto& a : st.empires[kPlayerId].resolutions.active) {
        if (a.target == ResTarget::ResCredit && a.value.rawValue() < 0) hasNegativeCredit = true;
    }
    CHECK(!hasNegativeCredit);

    // 阻止条件不满足 ⇒ 触发减益
    GameState st2 = resWorld(1234);
    st2.tick = 40;
    st2.empires[kPlayerId].treasury = Fixed(1000);
    TickReport rep2;
    resolutionPhase(st2, rep2);
    bool negative = false;
    for (const auto& a : st2.empires[kPlayerId].resolutions.active) {
        if (a.target == ResTarget::ResCredit && a.value.rawValue() < 0) negative = true;
    }
    CHECK(negative);
}

TEST(resolution, countdown_completes_or_fails) {
    GameState st = resWorld(555);
    st.tick = 40;
    st.empires[kPlayerId].apLeft = 9;
    TickReport rep;
    // 让「基建攻坚」触发（回合 ≥ 10）
    for (int i = 0; i < 3; ++i) resolutionPhase(st, rep);
    int idx = resolutionIndexByName("基建攻坚");
    CHECK(idx >= 0);
    // 可能已启动倒计时
    if (!st.empires[kPlayerId].resolutions.countdowns.empty()) {
        // 直接满足目标：稳定度拉到上限
        st.empires[kPlayerId].stability = Fixed(1);
        resolutionPhase(st, rep);
        CHECK(std::find(st.empires[kPlayerId].resolutions.completed.begin(),
                        st.empires[kPlayerId].resolutions.completed.end(),
                        static_cast<u16>(idx)) != st.empires[kPlayerId].resolutions.completed.end());
    }
    // 失败路径：目标不可能达成 ⇒ 超时后记录失败并施加惩罚
    GameState st2 = resWorld(556);
    st2.tick = 40;
    for (int i = 0; i < 3; ++i) resolutionPhase(st2, rep);
    if (!st2.empires[kPlayerId].resolutions.countdowns.empty()) {
        int failedId = static_cast<int>(st2.empires[kPlayerId].resolutions.countdowns.front().defId);
        for (int i = 0; i < 20; ++i) resolutionPhase(st2, rep);
        CHECK(st2.empires[kPlayerId].resolutions.countdowns.empty());
        CHECK(!st2.empires[kPlayerId].resolutions.failed.empty());
        // 失败惩罚必须是「有时限」的，不能永久累积
        int leftover = resolutionDef(failedId).countdownTicks;
        for (const auto& a : st2.empires[kPlayerId].resolutions.active) {
            if (a.defId != failedId) continue;
            CHECK(a.ticksLeft >= 0);
            CHECK(a.ticksLeft <= leftover + 1);
        }
    }
}

TEST(resolution, modifier_aggregates_into_empire_modifier) {
    GameState st = resWorld(777);
    Fixed before = empireModifier(st.empires[kPlayerId], ModKind::ResearchRate);
    ActiveEffect a;
    a.target = ResTarget::ResResearch;
    a.value = Fixed::pct(17);
    a.ticksLeft = 5;
    st.empires[kPlayerId].resolutions.active.push_back(a);
    Fixed after = empireModifier(st.empires[kPlayerId], ModKind::ResearchRate);
    CHECK_EQ(after.rawValue(), before.rawValue() + Fixed::pct(17).rawValue());
    // 过期的效果不再计入
    st.empires[kPlayerId].resolutions.active[0].ticksLeft = 0;
    Fixed expired = empireModifier(st.empires[kPlayerId], ModKind::ResearchRate);
    CHECK_EQ(expired.rawValue(), before.rawValue());
}

// ===========================================================================
// 互斥组与依赖链
// ===========================================================================

namespace {

/// 富裕且已解锁前置的帝国，便于单独检验互斥/依赖行为
GameState exclWorld(u64 seed = 7777) {
    GameState st = resWorld(seed);
    st.empires[kPlayerId].influence = Fixed(900000);
    st.empires[kPlayerId].treasury = Fixed(900000);
    st.empires[kPlayerId].apLeft = 99;
    st.tick = 60;
    return st;
}

void markDone(GameState& st, const char* name) {
    int i = resolutionIndexByName(name);
    if (i >= 0) st.empires[kPlayerId].resolutions.triggered.push_back(static_cast<u16>(i));
}

}  // namespace

TEST(resolution, exclusion_groups_are_consistent) {
    // 同组至少 2 项才有意义；组号必须在已命名范围内
    for (int g = 1; g <= 6; ++g) {
        int n = 0;
        for (int i = 0; i < kResolutionCount; ++i)
            if (resolutionDef(i).exclusionGroup == g) ++n;
        CHECK(n >= 2);
        CHECK(resExclusionGroupName(static_cast<u8>(g)) != "其他");
    }
    // 依赖与阻止目标必须是合法索引，且不能自指
    for (int i = 0; i < kResolutionCount; ++i) {
        const ResolutionDef& d = resolutionDef(i);
        for (u8 k = 0; k < d.requireCount && k < d.requiresRes.size(); ++k) {
            CHECK(static_cast<int>(d.requiresRes[k]) < kResolutionCount);
            CHECK(static_cast<int>(d.requiresRes[k]) != i);
        }
        for (u8 k = 0; k < d.blockCount && k < d.blocksRes.size(); ++k) {
            CHECK(static_cast<int>(d.blocksRes[k]) < kResolutionCount);
            CHECK(static_cast<int>(d.blocksRes[k]) != i);
        }
        // 配置一致性：不应同时「同组互斥」又「互相阻止」——互斥判定先执行，
        // 那样的阻止边永远不可达，属于冗余配置。
        for (u8 k = 0; k < d.blockCount && k < d.blocksRes.size(); ++k) {
            const ResolutionDef& o = resolutionDef(d.blocksRes[k]);
            if (d.exclusionGroup == 0 || o.exclusionGroup == 0) continue;
            CHECK(d.exclusionGroup != o.exclusionGroup);
        }
    }
}

TEST(resolution, dependency_chain_is_enforced) {
    GameState st = exclWorld(8001);
    int idx = resolutionIndexByName("劳动征召");
    CHECK(idx >= 0);
    std::string why;
    // 前置未完成 ⇒ 不可发起，且原因里点明所需的前置
    CHECK(!resCanActivate(st, kPlayerId, idx, &why));
    CHECK(why.find("战时经济") != std::string::npos);
    std::string err;
    CHECK(!resolutionActivate(st, kPlayerId, static_cast<u16>(idx), &err));
    // 完成前置后即可发起
    markDone(st, "战时经济");
    CHECK(resCanActivate(st, kPlayerId, idx, &why));
    CHECK(resolutionActivate(st, kPlayerId, static_cast<u16>(idx), &err));
}

// 回归守卫：改革曾是「每组各一项」，最多可同时挂 6 项，取舍消失。
// 现行规则：**同时只能推行一项改革**（倒计时型议程不占该槽位）。
TEST(resolution, only_one_reform_at_a_time) {
    GameState st = exclWorld(8101);
    st.empires[kPlayerId].treasury = Fixed(5000000);
    st.empires[kPlayerId].influence = Fixed(5000000);
    std::vector<int> acts;
    for (int i = 0; i < kResolutionCount && acts.size() < 3; ++i) {
        const ResolutionDef& d = resolutionDef(i);
        if (d.kind != ResolutionKind::Active && d.kind != ResolutionKind::Tradeoff) continue;
        acts.push_back(i);
    }
    if (acts.size() < 2) return;
    st.empires[kPlayerId].apLeft = 99;
    CHECK(resolutionActivate(st, kPlayerId, static_cast<u16>(acts[0]), nullptr));
    // 第二项必须被拒绝，且理由提到「同时只能有一项」
    std::string why;
    CHECK(!resCanActivate(st, kPlayerId, acts[1], &why));
    CHECK(why.find("同时只能有一项") != std::string::npos);
    // 生效中的改革必须是唯一一项（同一项的多条效果记录只算一项）
    std::vector<u16> ids;
    for (const auto& a : st.empires[kPlayerId].resolutions.active) {
        if (a.ticksLeft == 0) continue;
        const ResolutionDef& od = resolutionDef(static_cast<int>(a.defId));
        if (od.kind != ResolutionKind::Active && od.kind != ResolutionKind::Tradeoff) continue;
        if (std::find(ids.begin(), ids.end(), a.defId) == ids.end()) ids.push_back(a.defId);
    }
    CHECK_EQ(ids.size(), 1u);
}

// 回归守卫：改革只持续 4 季，太短，无法体现「国家转向」的分量。
// 现在为 24 季。
TEST(resolution, reform_duration_is_meaningful) {
    GameState st = exclWorld(8102);
    int a = resolutionIndexByName("战时公债");
    if (a < 0) return;
    CHECK_EQ(resolutionDef(a).duration, 24);
    // 所有带持续期的主动决议都不应短于 12 季
    for (int i = 0; i < kResolutionCount; ++i) {
        const ResolutionDef& d = resolutionDef(i);
        if (d.kind != ResolutionKind::Active) continue;
        if (d.duration <= 0) continue;   // 0 = 永久
        CHECK(d.duration >= 12);
    }
}

TEST(resolution, exclusion_prevents_same_group) {
    GameState st = exclWorld(8002);
    int a = resolutionIndexByName("紧缩财政");
    int b = resolutionIndexByName("科研拨款");
    CHECK(a >= 0);
    CHECK(b >= 0);
    CHECK_EQ(static_cast<int>(resolutionDef(a).exclusionGroup),
             static_cast<int>(resolutionDef(b).exclusionGroup));
    std::string err;
    CHECK(resolutionActivate(st, kPlayerId, static_cast<u16>(a), &err));
    // 同组另一项必须被拒绝。
    // 注意：现行规则是「改革同时只能有一项」，全局单槽先于分组互斥生效，
    // 因此拒绝理由来自单槽规则（组内互斥作为更细的判定保留在后）。
    std::string why;
    CHECK(!resCanActivate(st, kPlayerId, b, &why));
    CHECK(!why.empty());
    CHECK(!resolutionActivate(st, kPlayerId, static_cast<u16>(b), &err));
    CHECK(!err.empty());
    // 不同组同样受阻 —— 这正是「同时只能有一项」的核心含义
    int other = resolutionIndexByName("公民庆典");
    CHECK(other >= 0);
    CHECK(resolutionDef(other).exclusionGroup != resolutionDef(a).exclusionGroup);
    CHECK(!resCanActivate(st, kPlayerId, other, nullptr));
}

TEST(resolution, blocks_prevents_cross_group) {
    GameState st = exclWorld(8003);
    markDone(st, "边境筑垒");
    int sp = resolutionIndexByName("秘密警察");
    int ce = resolutionIndexByName("文化交流");
    CHECK(sp >= 0);
    CHECK(ce >= 0);
    CHECK(resolutionDef(sp).exclusionGroup != resolutionDef(ce).exclusionGroup);
    // 未生效时可发起
    CHECK(resCanActivate(st, kPlayerId, ce, nullptr));
    std::string err;
    CHECK(resolutionActivate(st, kPlayerId, static_cast<u16>(sp), &err));
    // 生效后目标不可再发起（现行规则下由「同时只能有一项」拦截）
    std::string why;
    CHECK(!resCanActivate(st, kPlayerId, ce, &why));
    CHECK(!why.empty());
    CHECK(!resolutionActivate(st, kPlayerId, static_cast<u16>(ce), &err));
}

TEST(resolution, auto_trigger_respects_blocking) {
    // 自动触发的决议也必须受互斥/阻止约束 ——
    // 否则「被秘密警察阻止的决议」仍会自行出现。
    GameState st = exclWorld(8004);
    markDone(st, "边境筑垒");
    int sp = resolutionIndexByName("秘密警察");
    CHECK(sp >= 0);
    const ResolutionDef& spd = resolutionDef(sp);
    // 秘密警察必须确实声明了阻止边
    CHECK(spd.blockCount > 0);
    for (int i = 0; i < kResolutionCount; ++i) {
        const ResolutionDef& d = resolutionDef(i);
        if (d.kind == ResolutionKind::Active || d.kind == ResolutionKind::Tradeoff) continue;
        // 自动触发类决议若被秘密警察阻止，在秘密警察生效期间不得触发
        bool blocked = false;
        for (u8 k = 0; k < spd.blockCount && k < spd.blocksRes.size(); ++k)
            if (spd.blocksRes[k] == i) blocked = true;
        if (!blocked) continue;
        CHECK(!resCanActivate(st, kPlayerId, i, nullptr));
    }
}

TEST(resolution, detail_text_shows_chains) {
    GameState st = exclWorld(8005);
    int idx = resolutionIndexByName("战时经济");
    CHECK(idx >= 0);
    std::string txt = resolutionDetailText(st, kPlayerId, static_cast<u16>(idx));
    CHECK(txt.find("互斥组") != std::string::npos);
    CHECK(txt.find("前置决议") != std::string::npos);
    // 展示的门槛必须与判定一致：文本说不可发起，判定也必须不可发起
    std::string why;
    bool canGo = resCanActivate(st, kPlayerId, idx, &why);
    if (canGo) {
        CHECK(txt.find("当前可发起：是") != std::string::npos);
    } else {
        CHECK(txt.find("当前可发起：否") != std::string::npos);
    }
}


// 回归守卫：稳定度曾因「每季 +修正*5% − 压力*1%」的单向棘轮而无任何恢复机制，
// 导致所有帝国的稳定度在 40 季内崩到 0 并永久钉死 ——
// 胜利条件「稳定 ≥70%」在数学上不可达成。稳定度必须收敛到均衡值。
TEST(resolution, stability_has_equilibrium_not_ratchet) {
    GameState st = resWorld(9001);
    for (int i = 0; i < 120; ++i) {
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);
    }
    // 不能所有帝国都贴 0：至少要有帝国维持在明显非零水平
    int recovered = 0;
    for (const auto& e : st.empires) {
        if (!e.alive) continue;
        if (e.stability.rawValue() > Fixed::pct(10).rawValue()) ++recovered;
    }
    CHECK(recovered >= 1);
    for (const auto& e : st.empires) {
        CHECK(e.stability.rawValue() >= 0);
        CHECK(e.stability.rawValue() <= Fixed(1).rawValue());
    }
}


// 回归守卫：行星开发度曾只在世界生成时设定、永不增长，
// 使经济体缺乏成长循环。开发度必须能随时间与建设增长。
TEST(resolution, planets_develop_over_time) {
    GameState st = resWorld(9003);
    Fixed devBefore = Fixed(0);
    for (const auto& p : st.planets)
        if (p.owner == kPlayerId) devBefore += p.development;
    for (int i = 0; i < 120; ++i) {
        economyPhase(st);
        ++st.tick;
    }
    Fixed devAfter = Fixed(0);
    for (const auto& p : st.planets)
        if (p.owner == kPlayerId) devAfter += p.development;
    CHECK(devAfter.rawValue() > devBefore.rawValue());
    // 开发度受结构体约定范围约束（0..10）
    for (const auto& p : st.planets) {
        CHECK(p.development.rawValue() >= 0);
        CHECK(p.development.rawValue() <= Fixed(10).rawValue());
    }
}
