// 议会立法：席位分配 / 立场计算 / 拉票 / 表决门槛 / 强行通过 / 效果聚合
#include <algorithm>

#include "check.h"
#include "core/GameState.h"
#include "core/TickPipeline.h"
#include "domain/Parliament.h"
#include "gen/EmpireGen.h"
#include "gen/WorldGen.h"
#include "plot/BeatResolver.h"
#include "save/Serde.h"

using namespace gf;

namespace {

GameState parWorld(u64 seed = 7070) {
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

TEST(parliament, content_table_is_consistent) {
    CHECK_EQ(kBillCount, 36);
    for (int i = 0; i < kBillCount; ++i) {
        const BillDef& b = billDef(i);
        CHECK(!b.idName.empty());
        CHECK(!b.nameZh.empty());
        CHECK(static_cast<int>(b.category) < static_cast<int>(BillCategory::Count));
        CHECK(b.effectCount > 0);
        CHECK(b.politicalCost > 0);
        // 每项法案都必须有明确的赞成方与反对方（否则不构成政治博弈）
        bool anyFor = false, anyAgainst = false;
        for (auto s : b.baseStance) {
            if (s > 0) anyFor = true;
            if (s < 0) anyAgainst = true;
        }
        CHECK(anyFor);
        CHECK(anyAgainst);
        for (u8 k = 0; k < b.effectCount && k < b.effects.size(); ++k)
            CHECK(b.effects[k].kind != ModKind::Count);
    }
    // 名字唯一且可按名检索
    for (int i = 0; i < kBillCount; ++i) {
        CHECK_EQ(billIndexByName(billDef(i).nameZh), i);
        CHECK_EQ(billIndexByName(billDef(i).idName), i);
        for (int j = i + 1; j < kBillCount; ++j) CHECK(billDef(i).nameZh != billDef(j).nameZh);
    }
    CHECK_EQ(billIndexByName("no-such-bill"), -1);
    // 类别覆盖
    for (int c = 0; c < static_cast<int>(BillCategory::Count); ++c) {
        int n = 0;
        for (int i = 0; i < kBillCount; ++i)
            if (static_cast<int>(billDef(i).category) == c) ++n;
        CHECK(n > 0);
    }
}

TEST(parliament, seats_assigned_from_influence) {
    GameState st = parWorld();
    for (const auto& e : st.empires) {
        CHECK(e.parliament.totalSeats > 0);
        int sum = 0;
        for (int i = 0; i < static_cast<int>(FactionKind::Count); ++i)
            sum += e.parliament.seats[static_cast<std::size_t>(i)];
        CHECK_EQ(sum, e.parliament.totalSeats);
        // 每个派系至少 1 席
        for (int i = 0; i < static_cast<int>(FactionKind::Count); ++i)
            CHECK(e.parliament.seats[static_cast<std::size_t>(i)] >= 1);
        // 政治资本与门槛已设定
        CHECK(e.parliament.capital.rawValue() > 0);
        CHECK(static_cast<int>(e.parliament.threshold) < static_cast<int>(VoteThreshold::Count));
    }
}

TEST(parliament, threshold_depends_on_government) {
    GameState st = parWorld(11);
    // 民主制 ⇒ 简单多数；独裁/寡头 ⇒ 绝对多数；蜂群/无政府 ⇒ 特别多数
    for (const auto& e : st.empires) {
        VoteThreshold expected = VoteThreshold::SimpleMajority;
        switch (e.government) {
            case 9:
            case 11: expected = VoteThreshold::SuperMajority; break;
            case 2:
            case 3:
            case 5:
            case 8: expected = VoteThreshold::AbsoluteMajority; break;
            default: expected = VoteThreshold::SimpleMajority; break;
        }
        CHECK_EQ(static_cast<int>(e.parliament.threshold), static_cast<int>(expected));
    }
}

TEST(parliament, propose_sets_up_session_with_stances) {
    GameState st = parWorld(22);
    // 找一项有明确支持与反对的法案
    int idx = billIndexByName("土地改革");
    CHECK(idx >= 0);
    std::string err;
    CHECK(billPropose(st, kPlayerId, static_cast<u16>(idx), &err));
    const BillSession& s = st.empires[kPlayerId].parliament.session;
    CHECK(s.active);
    CHECK_EQ(s.billId, static_cast<u8>(idx));
    CHECK(!s.seats.empty());
    CHECK(s.threshold.rawValue() > 0);
    // 至少有派系赞成与反对
    int sup = 0, opp = 0;
    for (const auto& seat : s.seats) {
        if (seat.stance == Stance::Supportive) ++sup;
        if (seat.stance == Stance::Opposed) ++opp;
    }
    CHECK(sup > 0);
    CHECK(opp > 0);
    // 不能同时审议两项
    CHECK(!billPropose(st, kPlayerId, 1, &err));
    CHECK(!err.empty());
}

TEST(parliament, persuade_shifts_stance_and_costs) {
    GameState st = parWorld(33);
    st.empires[kPlayerId].influence = Fixed(9000);
    st.empires[kPlayerId].treasury = Fixed(500000);
    st.market.margin.cash = Fixed(500000);
    st.empires[kPlayerId].parliament.capital = Fixed::pct(90);
    int idx = billIndexByName("土地改革");
    std::string err;
    CHECK(billPropose(st, kPlayerId, static_cast<u16>(idx), &err));

    // 找一个未定的派系
    FactionKind target = FactionKind::Count;
    Stance before = Stance::Count;
    for (const auto& seat : st.empires[kPlayerId].parliament.session.seats) {
        if (seat.stance == Stance::Undecided) {
            target = seat.faction;
            before = seat.stance;
            break;
        }
    }
    if (target == FactionKind::Count) return;
    Fixed capBefore = st.empires[kPlayerId].parliament.capital;
    Fixed infBefore = st.empires[kPlayerId].influence;
    CHECK(billPersuade(st, kPlayerId, target, &err));
    // 立场提升
    Stance after = Stance::Count;
    for (const auto& seat : st.empires[kPlayerId].parliament.session.seats)
        if (seat.faction == target) after = seat.stance;
    CHECK(static_cast<int>(after) > static_cast<int>(before));
    // 代价已付
    CHECK(st.empires[kPlayerId].parliament.capital.rawValue() < capBefore.rawValue());
    CHECK(st.empires[kPlayerId].influence.rawValue() < infBefore.rawValue());
    // 已支持的不能再拉
    for (const auto& seat : st.empires[kPlayerId].parliament.session.seats) {
        if (seat.stance != Stance::Supportive) continue;
        CHECK(!billPersuade(st, kPlayerId, seat.faction, &err));
        break;
    }
}

TEST(parliament, vote_respects_threshold) {
    GameState st = parWorld(44);
    int idx = billIndexByName("土地改革");
    std::string err;
    CHECK(billPropose(st, kPlayerId, static_cast<u16>(idx), &err));
    bool passed = billVote(st, kPlayerId, &err);
    const Parliament& p = st.empires[kPlayerId].parliament;
    CHECK(p.session.resolved);
    // 结果必须与票数一致：赞成比例是否达到门槛
    int yes = p.session.yesSeats, no = p.session.noSeats;
    Fixed ratio = (yes + no > 0) ? Fixed::raw(mulDivSat(yes, FIX, yes + no)) : Fixed(0);
    CHECK_EQ(passed, ratio.rawValue() >= p.session.threshold.rawValue());
    // 未拉票时激进改革通常应被否决（这是有意义的政治阻力）
    if (!passed) {
        CHECK(!p.rejected.empty());
    } else {
        CHECK(!p.passed.empty());
    }
    CHECK(!p.session.lastResult.empty());
}

TEST(parliament, enough_persuasion_can_pass_a_bill) {
    GameState st = parWorld(55);
    st.empires[kPlayerId].influence = Fixed(9000);
    st.empires[kPlayerId].treasury = Fixed(900000);
    st.market.margin.cash = Fixed(900000);
    int idx = billIndexByName("土地改革");
    std::string err;
    CHECK(billPropose(st, kPlayerId, static_cast<u16>(idx), &err));
    // 反复拉票直到通过或资本耗尽
    for (int round = 0; round < 12; ++round) {
        st.empires[kPlayerId].parliament.capital = Fixed::pct(90);   // 补足资本
        bool any = false;
        for (const auto& seat : st.empires[kPlayerId].parliament.session.seats) {
            if (seat.stance == Stance::Supportive) continue;
            if (billPersuade(st, kPlayerId, seat.faction, &err)) any = true;
        }
        if (!any) break;
    }
    bool passed = billVote(st, kPlayerId, &err);
    CHECK(passed);
    CHECK(!st.empires[kPlayerId].parliament.passed.empty());
}

TEST(parliament, force_pass_has_heavy_cost) {
    GameState st = parWorld(66);
    st.empires[kPlayerId].parliament.capital = Fixed::pct(90);
    int idx = billIndexByName("土地改革");
    std::string err;
    CHECK(billPropose(st, kPlayerId, static_cast<u16>(idx), &err));
    Fixed unrestBefore = st.empires[kPlayerId].domestic.unrest;
    Fixed legitBefore = st.empires[kPlayerId].domestic.legitimacy;
    Fixed satBefore = Fixed(0);
    for (const auto& f : st.empires[kPlayerId].domestic.factions)
        if (f.kind == FactionKind::Labor) satBefore = f.satisfaction;
    CHECK(billForcePass(st, kPlayerId, &err));
    // 民怨上涨、合法性下降
    CHECK(st.empires[kPlayerId].domestic.unrest.rawValue() > unrestBefore.rawValue());
    CHECK(st.empires[kPlayerId].domestic.legitimacy.rawValue() < legitBefore.rawValue());
    // 反对派满意度大幅下降
    for (const auto& f : st.empires[kPlayerId].domestic.factions)
        if (f.kind == FactionKind::Labor) CHECK(f.satisfaction.rawValue() <= satBefore.rawValue());
    // 法案已记录
    CHECK(!st.empires[kPlayerId].parliament.passed.empty());
    // 资本不足时不能强行通过
    GameState st2 = parWorld(67);
    st2.empires[kPlayerId].parliament.capital = Fixed(0);
    CHECK(billPropose(st2, kPlayerId, static_cast<u16>(idx), &err));
    CHECK(!billForcePass(st2, kPlayerId, &err));
}

TEST(parliament, passed_bill_effects_apply_to_modifiers) {
    GameState st = parWorld(77);
    st.empires[kPlayerId].parliament.capital = Fixed::pct(90);
    int idx = billIndexByName("土地改革");
    std::string err;
    CHECK(billPropose(st, kPlayerId, static_cast<u16>(idx), &err));
    Fixed growthBefore = empireModifier(st.empires[kPlayerId], ModKind::Growth);
    CHECK(billForcePass(st, kPlayerId, &err));
    // 法案效果（人口增长 +12%）必须反映到 empireModifier
    Fixed growthAfter = empireModifier(st.empires[kPlayerId], ModKind::Growth);
    CHECK(growthAfter.rawValue() > growthBefore.rawValue());
    // 议会修正接口也应给出相同增量
    CHECK(parliamentModifier(st, kPlayerId, ModKind::Growth).rawValue() > 0);
    // 未涉及的其他属性不应被影响
    CHECK_EQ(parliamentModifier(st, kPlayerId, ModKind::IntelDefense).rawValue(), 0);
}

TEST(parliament, cannot_propose_passed_bill_twice) {
    GameState st = parWorld(88);
    st.empires[kPlayerId].parliament.capital = Fixed::pct(90);
    int idx = billIndexByName("土地改革");
    std::string err;
    CHECK(billPropose(st, kPlayerId, static_cast<u16>(idx), &err));
    CHECK(billForcePass(st, kPlayerId, &err));
    // 同一法案不能再次提交
    CHECK(!billPropose(st, kPlayerId, static_cast<u16>(idx), &err));
    CHECK(!err.empty());
}

TEST(parliament, withdraw_clears_session) {
    GameState st = parWorld(99);
    int idx = billIndexByName("关税壁垒");
    std::string err;
    CHECK(billPropose(st, kPlayerId, static_cast<u16>(idx), &err));
    CHECK(st.empires[kPlayerId].parliament.session.active);
    CHECK(billWithdraw(st, kPlayerId, &err));
    CHECK(!st.empires[kPlayerId].parliament.session.active);
    // 撤回后可再提交
    CHECK(billPropose(st, kPlayerId, static_cast<u16>(idx), &err));
    // 没有表决时投票应报错
    GameState st2 = parWorld(100);
    CHECK(!billVote(st2, kPlayerId, &err));
    CHECK(!err.empty());
}

TEST(parliament, capital_recovers_and_seats_track_influence) {
    GameState st = parWorld(111);
    st.empires[kPlayerId].parliament.capital = Fixed(0);
    for (int i = 0; i < 10; ++i) parliamentPhase(st);
    CHECK(st.empires[kPlayerId].parliament.capital.rawValue() > 0);
    // 改变影响力后席位应重算
    for (auto& f : st.empires[kPlayerId].domestic.factions) {
        if (f.kind == FactionKind::Labor) f.influence = Fixed::pct(80);
        else f.influence = Fixed::pct(2);
    }
    parliamentPhase(st);
    int laborSeats = st.empires[kPlayerId].parliament.seats[static_cast<std::size_t>(FactionKind::Labor)];
    int others = 0;
    for (int i = 0; i < static_cast<int>(FactionKind::Count); ++i)
        if (i != static_cast<int>(FactionKind::Labor)) others += st.empires[kPlayerId].parliament.seats[static_cast<std::size_t>(i)];
    CHECK(laborSeats > 0);
    CHECK(others > 0);
}

TEST(parliament, state_survives_serialization) {
    GameState st = parWorld(222);
    st.empires[kPlayerId].parliament.capital = Fixed::pct(90);
    int idx = billIndexByName("土地改革");
    std::string err;
    CHECK(billPropose(st, kPlayerId, static_cast<u16>(idx), &err));
    CHECK(billForcePass(st, kPlayerId, &err));

    std::vector<u8> bytes = serializeState(st);
    GameState back;
    CHECK(tryDeserializeState(bytes, back));
    CHECK_EQ(back.empires[kPlayerId].parliament.totalSeats, st.empires[kPlayerId].parliament.totalSeats);
    CHECK_EQ(back.empires[kPlayerId].parliament.passed.size(), st.empires[kPlayerId].parliament.passed.size());
    CHECK_EQ(back.empires[kPlayerId].parliament.legislationCount,
             st.empires[kPlayerId].parliament.legislationCount);
    for (int i = 0; i < static_cast<int>(FactionKind::Count); ++i) {
        CHECK_EQ(back.empires[kPlayerId].parliament.seats[static_cast<std::size_t>(i)],
                 st.empires[kPlayerId].parliament.seats[static_cast<std::size_t>(i)]);
    }
    CHECK_EQ(back.empires[kPlayerId].parliament.capital.rawValue(),
             st.empires[kPlayerId].parliament.capital.rawValue());
}

// 回归守卫：未定席位在倾向接近 0 时曾被全部归入反对，
// 导致任何法案的赞成比恒在 0.32~0.36，议会机制形同虚设。
TEST(parliament, undecided_seats_split_proportionally) {
    GameState st = parWorld(4001);
    // 让满意度全部接近中性（倾向接近 0）
    for (auto& f : st.empires[kPlayerId].domestic.factions) f.satisfaction = Fixed::pct(50);
    int idx = billIndexByName("关税壁垒");
    VoteEstimate est = estimateVotes(st, kPlayerId, static_cast<u16>(idx));
    // 中立情况下未定席位应大致对半，而不是一边倒
    CHECK(est.yes > 0);
    CHECK(est.no > 0);
    Fixed ratio = est.ratio;
    CHECK(ratio.rawValue() > Fixed::pct(35).rawValue());
    CHECK(ratio.rawValue() < Fixed::pct(65).rawValue());
    // 估算与正式表决必须一致
    std::string err;
    CHECK(billPropose(st, kPlayerId, static_cast<u16>(idx), &err));
    bool passed = billVote(st, kPlayerId, &err);
    CHECK_EQ(passed, est.wouldPass);
}

// 回归守卫：派系满意度曾因「+2%/季 + 财富项」的线性累加而在数十季内
// 必然饱和到 0% 或 100%，派系/议会/政变机制随之失效。
TEST(parliament, faction_satisfaction_does_not_saturate) {
    GameState st = parWorld(4002);
    for (int i = 0; i < 200; ++i) {
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);
    }
    // 至少要有派系处于中间区间（既不贴 0 也不贴 1）
    int middling = 0;
    for (const auto& f : st.empires[kPlayerId].domestic.factions) {
        if (f.satisfaction.rawValue() > Fixed::pct(15).rawValue() &&
            f.satisfaction.rawValue() < Fixed::pct(85).rawValue())
            ++middling;
    }
    CHECK(middling >= 2);
    // 各派系满意度不应全部相同（政策应当造成分化）
    Fixed first = st.empires[kPlayerId].domestic.factions.front().satisfaction;
    bool differs = false;
    for (const auto& f : st.empires[kPlayerId].domestic.factions)
        if (f.satisfaction.rawValue() != first.rawValue()) differs = true;
    CHECK(differs);
}

// 回归守卫：民怨曾因「结构性因素按季累加」（种族张力 3%/季、
// Unrest 修正被当作绝对增量）而在 13~45 季内必然饱和到 100%。
// 结构性因素必须作用在**均衡值**上。
TEST(parliament, unrest_does_not_saturate) {
    GameState st = parWorld(5001);
    // 完全不作弊的帝国会陷入危机周期，但**必须存在恢复能力** ——
    // 关键区别是「周期性震荡」而非「单向钉死在上限」。
    Fixed lowest = Fixed(1);
    int pinned = 0;
    for (int i = 0; i < 200; ++i) {
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);
        Fixed u = st.empires[kPlayerId].domestic.unrest;
        if (u.rawValue() < lowest.rawValue()) lowest = u;
        if (u.rawValue() >= Fixed::pct(99).rawValue()) ++pinned;
    }
    // 民怨必须出现过明显回落（证明不是单向饱和）
    CHECK(lowest.rawValue() < Fixed::pct(60).rawValue());
    // 且不应长期被钉在 100%
    CHECK(pinned < 120);
    // 民怨始终在合法区间
    CHECK(st.empires[kPlayerId].domestic.unrest.rawValue() >= 0);
    CHECK(st.empires[kPlayerId].domestic.unrest.rawValue() <= Fixed(1).rawValue());
}

// 回归守卫：政变曾把全体派系满意度重置到 30% 并进一步降低合法性，
// 使政变风险立刻回到高位 —— 形成「政变 → 更不满 → 再政变」的死亡螺旋。
TEST(parliament, coups_do_not_form_death_spiral) {
    GameState st = parWorld(5002);
    st.empires[kPlayerId].domestic.coupRisk = Fixed::pct(90);
    for (int i = 0; i < 200; ++i) {
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);
    }
    // 派系满意度必须回到中间区间（政变应当重置政治秩序，而非永久压低）
    int recovered = 0;
    for (const auto& f : st.empires[kPlayerId].domestic.factions)
        if (f.satisfaction.rawValue() >= Fixed::pct(35).rawValue()) ++recovered;
    CHECK(recovered >= 4);
    // 政变次数应有节制（冷却生效）
    CHECK(st.empires[kPlayerId].domestic.lastCoupTick < 1000);
}

// 回归守卫：默认政策必须中立 —— 否则均衡满意度会被系统性压低，
// 进而推高民怨并让政变风险长期钉在上限。
TEST(parliament, default_policies_are_neutral) {
    GameState st = parWorld(5003);
    for (int g = 0; g < kPolicyGroupCount; ++g) {
        auto opts = policyOptionsIn(static_cast<PolicyGroup>(g));
        CHECK(!opts.empty());
        // 每组第一项是默认项，不应偏袒或打击任何派系
        CHECK_EQ(static_cast<int>(opts.front()->favored), static_cast<int>(FactionKind::Count));
        CHECK_EQ(static_cast<int>(opts.front()->harmed), static_cast<int>(FactionKind::Count));
        CHECK_EQ(opts.front()->factionDelta.rawValue(), 0);
    }
}

TEST(parliament, ai_empires_legislate_over_time) {
    GameState st = parWorld(4003);
    for (int i = 0; i < 150; ++i) {
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);
    }
    // AI 帝国应当确实推动过立法（提案或通过）
    int active = 0;
    for (const auto& e : st.empires) {
        if (e.isPlayer) continue;
        if (!e.parliament.passed.empty() || !e.parliament.rejected.empty()) ++active;
    }
    CHECK(active >= 3);
    // 至少有一个 AI 通过了法案
    int passedAny = 0;
    for (const auto& e : st.empires) {
        if (e.isPlayer) continue;
        if (!e.parliament.passed.empty()) ++passedAny;
    }
    CHECK(passedAny >= 1);
    // 通过的法案效果必须真实生效
    for (const auto& e : st.empires) {
        if (e.parliament.passed.empty()) continue;
        Fixed sum = Fixed(0);
        for (u16 id : e.parliament.passed) {
            const BillDef& b = billDef(id);
            for (u8 k = 0; k < b.effectCount && k < b.effects.size(); ++k)
                sum += fxAbs(b.effects[k].value);
        }
        CHECK(sum.rawValue() > 0);
    }
}

TEST(parliament, text_renders_for_all_empires) {
    GameState st = parWorld(333);
    for (const auto& e : st.empires) {
        std::string txt = parliamentText(st, e.id);
        CHECK(!txt.empty());
        CHECK(txt.find("席位") != std::string::npos);
    }
    // 非法主体不崩溃
    CHECK(!parliamentText(st, 9999).empty());
    // 法案详情
    for (int i = 0; i < kBillCount; ++i) {
        std::string d = billDetailText(st, kPlayerId, static_cast<u16>(i));
        CHECK(!d.empty());
        CHECK(d.find(billDef(i).nameZh) != std::string::npos);
    }
    CHECK(!billDetailText(st, kPlayerId, 9999).empty());
}
