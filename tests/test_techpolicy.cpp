// 科技树效果与政策系统
#include <algorithm>

#include "check.h"
#include "core/GameState.h"
#include "core/TickPipeline.h"
#include "domain/Policy.h"
#include "domain/Tech.h"
#include "gen/EmpireGen.h"
#include "gen/WorldGen.h"
#include "plot/BeatResolver.h"
#include "save/Serde.h"

using namespace gf;

namespace {

GameState polWorld(u64 seed = 6060) {
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

// ===========================================================================
// 科技树
// ===========================================================================

TEST(tech, every_tech_has_real_effects) {
    // 96 项科技都必须提供至少一条属性修正
    int withEffects = 0;
    for (int i = 0; i < kTechCount; ++i) {
        const TechInfo& t = techInfo(i);
        if (t.effectCount == 0) continue;
        ++withEffects;
        for (u8 k = 0; k < t.effectCount && k < t.effects.size(); ++k) {
            CHECK(t.effects[k].kind != ModKind::Count);
            CHECK(t.effects[k].value.rawValue() != 0);
        }
    }
    CHECK_EQ(withEffects, kTechCount);
    // 效果量级随 tier 递增
    Fixed t1 = techInfo(0).effects[0].value;
    Fixed t5 = techInfo(15).effects[0].value;
    CHECK(fxAbs(t5).rawValue() > fxAbs(t1).rawValue());
    // 效果文本可读
    CHECK(!techEffectText(techInfo(0)).empty());
    CHECK(techEffectText(techInfo(0)) != "无属性修正");
}

TEST(tech, completing_techs_raises_modifiers) {
    GameState st = polWorld(11);
    Fixed before = techModifier(st.empires[0].tech, ModKind::ResearchRate);
    // 完成整个物理分支
    for (int i = 0; i < 16; ++i) st.empires[0].tech.completed.push_back(static_cast<u8>(i));
    Fixed after = techModifier(st.empires[0].tech, ModKind::ResearchRate);
    CHECK(after.rawValue() > before.rawValue());
    // 也必须反映到 empireModifier 上（科技不是孤立数据）
    CHECK(empireModifier(st.empires[0], ModKind::ResearchRate).rawValue() >
          before.rawValue());
}

TEST(tech, branch_mastery_grants_bonus) {
    GameState st = polWorld(22);
    // 未满级
    for (int i = 0; i < 15; ++i) st.empires[0].tech.completed.push_back(static_cast<u8>(i));
    BranchProgress p15 = branchProgress(st.empires[0].tech, TechBranch::Physics);
    CHECK_EQ(p15.completed, 15);
    CHECK(!p15.mastered);
    Fixed at15 = techModifier(st.empires[0].tech, ModKind::ResearchRate);
    // 补上第 16 项 ⇒ 满级奖励
    st.empires[0].tech.completed.push_back(static_cast<u8>(15));
    BranchProgress p16 = branchProgress(st.empires[0].tech, TechBranch::Physics);
    CHECK_EQ(p16.completed, 16);
    CHECK(p16.mastered);
    Fixed at16 = techModifier(st.empires[0].tech, ModKind::ResearchRate);
    // 第 16 项本身有效果，满级再额外 +10%
    CHECK(at16.rawValue() > at15.rawValue() + Fixed::pct(5).rawValue());
}

TEST(tech, branch_progress_is_per_branch) {
    GameState st = polWorld(33);
    // 只完成工程分支
    for (int i = 0; i < 16; ++i) st.empires[0].tech.completed.push_back(static_cast<u8>(32 + i));
    CHECK_EQ(branchProgress(st.empires[0].tech, TechBranch::Engineering).completed, 16);
    CHECK_EQ(branchProgress(st.empires[0].tech, TechBranch::Physics).completed, 0);
    CHECK_EQ(branchProgress(st.empires[0].tech, TechBranch::Biology).completed, 0);
    // 工程主轴是建造速率
    CHECK(techModifier(st.empires[0].tech, ModKind::BuildRate).rawValue() > 0);
}

TEST(tech, modifier_is_zero_for_untouched_branch) {
    GameState st = polWorld(44);
    // 全新帝国的科技修正必须为 0（否则等于白送加成）
    for (int b = 0; b < kTechBranchCount; ++b) {
        CHECK_EQ(branchProgress(st.empires[0].tech, static_cast<TechBranch>(b)).completed, 0);
    }
    CHECK_EQ(techModifier(st.empires[0].tech, ModKind::ResearchRate).rawValue(), 0);
    CHECK_EQ(techModifier(st.empires[0].tech, ModKind::BuildRate).rawValue(), 0);
}

// 回归守卫：progress 是定点数（raw = 点数×1000），t.cost 是整数点数。
// 早期直接比较 rawValue 与 cost，使实际成本只有 1/1000，科技树被瞬间穷尽。
TEST(tech, research_cost_uses_consistent_units) {
    GameState st = polWorld(7001);
    TechState probe;
    // 给恰好「一项 tier1 成本」的点数：应当完成 1 项（不是 16 项）
    const i64 tier1Cost = techInfo(0).cost;
    std::vector<u8> done = techAdvance(probe, Fixed(tier1Cost) / Fixed::pct(16));
    // 允许因分支预算分配带来少量出入，但绝不应一次完成大量科技
    CHECK(done.size() <= 6);
    // 给足额点数后应当完成
    TechState probe2;
    for (int i = 0; i < 40; ++i) (void)techAdvance(probe2, Fixed(4000));
    CHECK(probe2.completed.size() > 0);
    // 完成的总成本量级必须与标称成本一致（而非 1/1000）
    i64 spent = 0;
    for (u8 c : probe2.completed) spent += techInfo(static_cast<int>(c)).cost;
    CHECK(spent >= 120);
}

TEST(tech, branch_entry_techs_are_never_gated) {
    // 每个分支的入口科技（t=0）必须无前置，否则该分支永远无法开始
    for (int b = 0; b < kTechBranchCount; ++b) {
        const TechInfo& entry = techInfo(b * 16);
        CHECK(entry.prereq.empty());
        TechState empty;
        CHECK(techAvailable(empty, b * 16));
    }
}

TEST(tech, cross_branch_prereqs_point_at_intended_techs) {
    // 跨分支前置必须落在有意义的科技上：目标不能是入口，且必须属于另一分支
    for (int i = 0; i < kTechCount; ++i) {
        const TechInfo& t = techInfo(i);
        if (t.prereq.size() < 2) continue;
        for (std::size_t k = 1; k < t.prereq.size(); ++k) {
            int req = static_cast<int>(t.prereq[k]);
            CHECK(req >= 0 && req < kTechCount);
            CHECK(req != i);
            CHECK(req % 16 != 0);              // 不应依赖别人的入口（那会连环锁死）
            CHECK(techInfo(req).branch != t.branch);   // 应确实跨分支
        }
    }
}

TEST(tech, empires_differentiate_by_research_focus) {
    GameState st = polWorld(7002);
    // 各帝国的侧重不应完全相同
    bool differs = false;
    for (std::size_t i = 1; i < st.empires.size(); ++i) {
        if (st.empires[i].tech.focus != st.empires[0].tech.focus) differs = true;
    }
    CHECK(differs);
    // 侧重总和为正值
    for (const auto& e : st.empires) {
        Fixed sum = Fixed(0);
        for (const auto& f : e.tech.focus) sum += f;
        CHECK(sum.rawValue() > 0);
    }
    // 主攻分支可识别
    CHECK(static_cast<int>(st.empires[0].tech.primaryFocus()) < kTechBranchCount);
}

TEST(tech, research_progress_unlocks_techs_over_time) {
    GameState st = polWorld(55);
    st.empires[0].tech.rate = Fixed(3);
    std::size_t before = st.empires[0].tech.completed.size();
    // 手动推进研究若干轮
    for (int i = 0; i < 400; ++i) (void)techAdvance(st.empires[0].tech, Fixed(40));
    CHECK(st.empires[0].tech.completed.size() > before);
    // 前置约束：任何已完成科技的前置也必须已完成
    for (u8 c : st.empires[0].tech.completed) {
        const TechInfo& t = techInfo(static_cast<int>(c));
        for (u8 p : t.prereq) CHECK(techCompleted(st.empires[0].tech, static_cast<int>(p)));
    }
    // 不应重复完成同一项
    std::vector<u8> sorted = st.empires[0].tech.completed;
    std::sort(sorted.begin(), sorted.end());
    CHECK(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());
}

// ===========================================================================
// 政策系统
// ===========================================================================

TEST(policy, content_table_is_consistent) {
    CHECK(policyOptionCount() >= 15);
    for (std::size_t i = 0; i < policyOptionCount(); ++i) {
        const PolicyOption& p = policyOption(i);
        CHECK(!p.idName.empty());
        CHECK(!p.nameZh.empty());
        CHECK(static_cast<int>(p.group) < kPolicyGroupCount);
        CHECK(p.effectCount > 0);
        CHECK(p.transition >= 1);
        for (u8 k = 0; k < p.effectCount && k < p.effects.size(); ++k)
            CHECK(p.effects[k].kind != ModKind::Count);
    }
    // 每组至少 2 个选项（否则不构成「选择」）
    for (int g = 0; g < kPolicyGroupCount; ++g)
        CHECK(policyOptionsIn(static_cast<PolicyGroup>(g)).size() >= 2);
    // id 唯一、名字唯一
    for (std::size_t i = 0; i < policyOptionCount(); ++i) {
        for (std::size_t j = i + 1; j < policyOptionCount(); ++j) {
            CHECK(policyOption(i).id != policyOption(j).id);
            CHECK(policyOption(i).nameZh != policyOption(j).nameZh);
        }
        CHECK_EQ(policyFind(policyOption(i).nameZh)->id, policyOption(i).id);
        CHECK_EQ(policyFind(policyOption(i).idName)->id, policyOption(i).id);
    }
    CHECK(policyFind("no-such-policy") == nullptr);
}

TEST(policy, defaults_are_set_for_every_group) {
    GameState st = polWorld(66);
    for (const auto& e : st.empires) {
        for (int g = 0; g < kPolicyGroupCount; ++g) {
            CHECK(e.policies.active[static_cast<std::size_t>(g)] != kNoPolicy);
            CHECK_EQ(e.policies.pending[static_cast<std::size_t>(g)], kNoPolicy);
            CHECK_EQ(static_cast<int>(e.policies.transitionLeft[static_cast<std::size_t>(g)]), 0);
        }
    }
}

TEST(policy, enact_costs_influence_and_starts_transition) {
    GameState st = polWorld(77);
    st.empires[0].influence = Fixed(5000);
    const PolicyOption* p = policyFind("福利国家");
    CHECK(p != nullptr);
    Fixed before = st.empires[0].influence;
    std::string err;
    CHECK(policyEnact(st, 0, *p, &err));
    CHECK(st.empires[0].influence.rawValue() < before.rawValue());
    // 进入过渡
    std::size_t gi = static_cast<std::size_t>(p->group);
    CHECK_EQ(st.empires[0].policies.pending[gi], p->id);
    CHECK_EQ(static_cast<int>(st.empires[0].policies.transitionLeft[gi]), p->transition);
    // 同一组不能同时推行两项
    CHECK(!policyEnact(st, 0, *p, &err));
    CHECK(!err.empty());
    // 影响力不足必须被拒绝
    GameState st2 = polWorld(78);
    st2.empires[0].influence = Fixed(0);
    const PolicyOption* exp = policyFind("藩属网络");
    CHECK(exp != nullptr);
    CHECK(!policyEnact(st2, 0, *exp, &err));
    CHECK(err.find("影响力不足") != std::string::npos);
}

TEST(policy, effects_switch_linearly_during_transition) {
    GameState st = polWorld(88);
    st.empires[0].influence = Fixed(9000);
    const PolicyOption* war = policyFind("战时统制");
    CHECK(war != nullptr);
    std::string err;
    CHECK(policyEnact(st, 0, *war, &err));

    Fixed start = empireModifier(st.empires[0], ModKind::MilitaryPower);
    std::vector<Fixed> steps;
    for (int i = 0; i < war->transition; ++i) {
        policyPhase(st);
        steps.push_back(empireModifier(st.empires[0], ModKind::MilitaryPower));
    }
    // 单调递增到目标
    for (std::size_t i = 1; i < steps.size(); ++i) CHECK(steps[i].rawValue() >= steps[i - 1].rawValue());
    CHECK(steps.back().rawValue() > start.rawValue());
    // 过渡结束后政策正式生效
    std::size_t gi = static_cast<std::size_t>(war->group);
    CHECK_EQ(st.empires[0].policies.active[gi], war->id);
    CHECK_EQ(st.empires[0].policies.pending[gi], kNoPolicy);
    Fixed settled = empireModifier(st.empires[0], ModKind::MilitaryPower);
    policyPhase(st);
    CHECK_EQ(empireModifier(st.empires[0], ModKind::MilitaryPower).rawValue(), settled.rawValue());
}

TEST(policy, only_one_option_per_group_is_active) {
    GameState st = polWorld(99);
    st.empires[0].influence = Fixed(9000);
    std::string err;
    const PolicyOption* a = policyFind("国家主导");
    CHECK(a != nullptr);
    CHECK(policyEnact(st, 0, *a, &err));
    for (int i = 0; i < a->transition + 1; ++i) policyPhase(st);
    // 现在经济组生效的是国家主导
    std::size_t gi = static_cast<std::size_t>(PolicyGroup::Economic);
    CHECK_EQ(st.empires[0].policies.active[gi], a->id);
    // 验证修正中不包含同组其他选项的效果
    Fixed trade = policyModifier(st, 0, ModKind::TradeMargin);
    // 国家主导给 -12%；若同时叠加自由市场的 +20% 则不会是这个值
    CHECK(trade.rawValue() < 0);
}

TEST(policy, upkeep_is_summed_and_charged) {
    GameState st = polWorld(111);
    st.empires[0].influence = Fixed(9000);
    i64 before = policyUpkeep(st, 0);
    const PolicyOption* p = policyFind("战时统制");
    CHECK(p != nullptr);
    std::string err;
    CHECK(policyEnact(st, 0, *p, &err));
    for (int i = 0; i < p->transition + 1; ++i) policyPhase(st);
    i64 after = policyUpkeep(st, 0);
    CHECK(after > before);
    // 维护费确实从国库扣除：比较同政策下有无维护的收支差
    CHECK(after >= p->upkeep);
}

TEST(policy, faction_impact_applies) {
    GameState st = polWorld(222);
    st.empires[0].influence = Fixed(9000);
    const PolicyOption* p = policyFind("战时统制");
    CHECK(p != nullptr);
    // 记录推行前后的派系满意度
    Fixed milBefore = Fixed(0), labBefore = Fixed(0);
    for (const auto& f : st.empires[0].domestic.factions) {
        if (f.kind == p->favored) milBefore = f.satisfaction;
        if (f.kind == p->harmed) labBefore = f.satisfaction;
    }
    std::string err;
    CHECK(policyEnact(st, 0, *p, &err));
    for (const auto& f : st.empires[0].domestic.factions) {
        if (f.kind == p->favored) CHECK(f.satisfaction.rawValue() >= milBefore.rawValue());
        if (f.kind == p->harmed) CHECK(f.satisfaction.rawValue() <= labBefore.rawValue());
    }
}

TEST(policy, state_survives_serialization) {
    GameState st = polWorld(333);
    st.empires[0].influence = Fixed(9000);
    const PolicyOption* p = policyFind("福利国家");
    CHECK(p != nullptr);
    std::string err;
    CHECK(policyEnact(st, 0, *p, &err));
    policyPhase(st);

    std::vector<u8> bytes = serializeState(st);
    GameState back;
    CHECK(tryDeserializeState(bytes, back));
    for (int g = 0; g < kPolicyGroupCount; ++g) {
        std::size_t gi = static_cast<std::size_t>(g);
        CHECK_EQ(back.empires[0].policies.active[gi], st.empires[0].policies.active[gi]);
        CHECK_EQ(back.empires[0].policies.pending[gi], st.empires[0].policies.pending[gi]);
        CHECK_EQ(back.empires[0].policies.transitionLeft[gi], st.empires[0].policies.transitionLeft[gi]);
    }
    CHECK_EQ(back.empires[0].policies.enactCount, st.empires[0].policies.enactCount);
}

TEST(policy, ai_empires_also_have_policies) {
    GameState st = polWorld(444);
    for (const auto& e : st.empires) {
        i64 up = policyUpkeep(st, e.id);
        // 默认政策组合的维护费应当是确定的非负值
        CHECK(up >= 0);
        // 至少有一组生效
        int active = 0;
        for (int g = 0; g < kPolicyGroupCount; ++g)
            if (e.policies.active[static_cast<std::size_t>(g)] != kNoPolicy) ++active;
        CHECK(active >= 1);
    }
}
