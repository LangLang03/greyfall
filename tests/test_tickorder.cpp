// tick 阶段顺序的回归守护。
//
// 为什么需要这个文件：`TickPipeline::advanceOneTick` 里的阶段顺序是**载荷**，
// 但此前只靠注释维持。`TickPipeline.cpp` 里留下过两次真实事故的记录：
//   · 「AI 研究必须在这里跑：… 夹在两者之间（原位置在 phaseEvents 内）
//      会让研究永远分不到预算 —— 实测 AI 国库被精确抽到等于一季收入，
//      科技 120 季只完成 3~4 项」
//   · 「AI 决议也要在这里决策：决议成本 2,500~11,500 cr，而 phaseMarket
//      会在 tick 开头把国库花掉。原先决议在市场之后决策，实测 AI
//      『缺钱』的候选决议有 20~31 项、生效决议长期停在 2~3 项」
//
// 注释无法阻止回归；这里把「谁必须先于谁」变成可执行的断言。
// 每条断言都对应一个已经发生过的、静默的经济后果。

#include "domain/Economy.h"

#include <string>

#include "ai/AiCore.h"
#include "check.h"
#include "core/GameState.h"
#include "core/ResolutionEngine.h"
#include "core/TickPipeline.h"
#include "domain/Development.h"
#include "domain/Empire.h"
#include "domain/Planet.h"
#include "domain/Tech.h"
#include "gen/EmpireGen.h"
#include "plot/BeatResolver.h"

using namespace gf;
namespace {

/// 最小可控世界：2 个帝国（0 = 玩家，1 = AI），2 个星系，各 1 颗行星。
/// 阶段顺序的断言只需要「有收入、有国库、有立项」这些前置，
/// 不需要完整宇宙 —— 完整世界会引入 AI 动作与事件的噪声。
GameState orderWorld() {
    GameState st;
    st.relations.resize(static_cast<std::size_t>(kMaxEmpires) * kMaxEmpires);
    st.empires.resize(2);
    st.map.systems.resize(2);
    for (u32 i = 0; i < 2; ++i) {
        auto& sys = st.map.systems[i];
        sys.id = i;
        sys.name = "S" + std::to_string(i);
        sys.owner = i;
        if (i == 0) sys.links.push_back(1);
        else sys.links.push_back(0);
        Planet p;
        p.id = i;
        p.system = i;
        p.name = "P" + std::to_string(i);
        p.type = PlanetType::Rocky;
        p.size = 12;
        p.habitability = Fixed::pct(80);
        p.owner = i;
        p.colonized = true;
        p.pops = 5000;
        p.stability = Fixed::pct(70);
        p.yield.fill(Fixed(0));
        p.yield[static_cast<std::size_t>(Commodity::Credits)] = Fixed(50);
        p.yield[static_cast<std::size_t>(Commodity::Energy)] = Fixed(50);
        p.yield[static_cast<std::size_t>(Commodity::Food)] = Fixed(50);
        st.planets.push_back(p);
        sys.planets.push_back(i);
    }
    for (auto& e : st.empires) {
        e.id = e.id;   // 由下方索引赋值
    }
    for (std::size_t i = 0; i < st.empires.size(); ++i) {
        Empire& e = st.empires[i];
        e.id = static_cast<u32>(i);
        e.alive = true;
        e.isPlayer = (i == 0);
        e.name = (i == 0) ? "玩家" : "AI";
        e.species = 0;
        e.government = 0;
        e.treasury = Fixed(200000);
        e.capital = static_cast<u32>(i);
        e.systems.push_back(static_cast<u32>(i));
        e.stock.fill(Fixed(4000));
        e.military = Fixed(100);
        e.stability = Fixed::pct(70);
        e.legitimacy = Fixed::pct(70);
        e.domestic.legitimacy = Fixed::pct(70);
        e.domestic.unrest = Fixed::pct(10);
        e.tech.rate = Fixed(1);
        refreshEmpireBonuses(st);
    }
    st.market.margin.cash = st.empires[kPlayerId].treasury;
    return st;
}

/// 让某帝国立项一项 1 层科技（成本 181 点，最短工期 6 季），
/// 并把进度推到「本季只要拿到任何推进就会完成」。
void armResearch(GameState& st, u32 empire, Fixed progress, u32 ticks) {
    Empire& e = st.empires[empire];
    const int tech = 0;   // 物理分支第 1 项
    const TechInfo& ti = techInfo(tech);
    CHECK(ti.prereq.empty());
    e.tech.project = static_cast<u32>(tech);
    e.tech.projectProgress = progress;
    e.tech.projectTicks = ticks;
    e.tech.fundingPerTick = Fixed(4000);   // 4000 cr → 100 研究点/季
}

}  // namespace

// ---------------------------------------------------------------------------
// 研究必须先于市场结算。
//
// 旧事故：研究若在 phaseMarket 之后跑，市场会把国库花到「恰好等于一季收入」，
// 研究永远分不到预算 —— AI 在 120 季里只完成 3~4 项科技。
// 断言方式：给帝国足够现金并让项目只差最后一点进度，
// 推进 1 tick 后必须完成；若市场先跑并抽干国库，这里会失败。
// ---------------------------------------------------------------------------
TEST(tickorder, research_runs_before_market_drain) {
    GameState st = orderWorld();
    const int tech = 0;
    const Fixed cost = Fixed(static_cast<i64>(techInfo(tech).cost));
    // 只差 1 点：任何推进都会完成；工期也刚好满足
    armResearch(st, kPlayerId, cost - Fixed(1), static_cast<u32>(techMinTicks(techInfo(tech).tier)) - 1);
    CHECK(!techCompleted(st.empires[kPlayerId].tech, tech));

    advanceOneTick(st);

    CHECK(techCompleted(st.empires[kPlayerId].tech, tech));
}

TEST(tickorder, ai_research_also_runs_before_market_drain) {
    GameState st = orderWorld();
    const int tech = 0;
    const Fixed cost = Fixed(static_cast<i64>(techInfo(tech).cost));
    armResearch(st, 1, cost - Fixed(1), static_cast<u32>(techMinTicks(techInfo(tech).tier)) - 1);

    advanceOneTick(st);

    // AI 与玩家共用同一条推进路径 —— 顺序修复若只覆盖玩家侧，这里会失败
    CHECK(techCompleted(st.empires[1].tech, tech));
}

// ---------------------------------------------------------------------------
// 研究不得因「国库见底」而静默停摆。
//
// `researchTick` 扣不起时会把 `fundingPerTick` 降档（这是有意设计）。
// 但**降档必须由真实余额驱动**，而不是被前序阶段抽干 ——
// 否则一次市场结算就能让研究永久停摆（降档后不会再自动升回）。
// ---------------------------------------------------------------------------
TEST(tickorder, research_funding_survives_one_market_settlement) {
    GameState st = orderWorld();
    const int tech = 0;
    const Fixed cost = Fixed(static_cast<i64>(techInfo(tech).cost));
    armResearch(st, kPlayerId, cost - Fixed(1), static_cast<u32>(techMinTicks(techInfo(tech).tier)) - 1);
    const Fixed before = st.empires[kPlayerId].treasury;
    CHECK(before.rawValue() > 0);

    advanceOneTick(st);

    // 投入被真实扣除（说明研究拿到了预算），而不是被降档到 0
    CHECK(st.empires[kPlayerId].tech.fundingPerTick.rawValue() > 0);
    CHECK(techCompleted(st.empires[kPlayerId].tech, tech));
}

// ---------------------------------------------------------------------------
// 军备积累必须先于战斗结算。
//
// 旧事故记录：「军备积累先于战斗：造舰需要时间，军力由经济体量决定并逐步逼近目标」。
// 若战斗先跑，本季的军力增长对当季战斗毫无影响，强国的优势会被推迟一季体现。
// 断言方式：让 AI 的军力远低于其经济目标，推进 1 tick 后军力必须已上升。
// ---------------------------------------------------------------------------
TEST(tickorder, military_buildup_runs_before_combat) {
    GameState st = orderWorld();
    Empire& ai = st.empires[1];
    ai.military = Fixed(1);              // 远低于经济支撑的目标值
    const Fixed before = ai.military;

    advanceOneTick(st);

    CHECK(st.empires[1].military.rawValue() > before.rawValue());
}

// ---------------------------------------------------------------------------
// 经济结算是 tick 的最后一个阶段（收入到账后才能被下一季使用）。
//
// 断言方式：经济结算若在 tick 内被调用两次，国库会因为重复计入收入而偏离
// 单季净收入；这里用「国库增量 ≈ lastIncome」来锁定它只结算一次。
// ---------------------------------------------------------------------------
TEST(tickorder, economy_settles_exactly_once_per_tick) {
    GameState st = orderWorld();
    const Fixed before = st.empires[kPlayerId].treasury;

    advanceOneTick(st);

    const Fixed delta = st.empires[kPlayerId].treasury - before;
    const Fixed income = st.empires[kPlayerId].lastIncome;
    // 允许少量偏差（事件/决议的离散扣款），但绝不能是「两倍收入」
    CHECK(delta.rawValue() <= income.rawValue() + FIX);
    CHECK(income.rawValue() > 0);
}

// ---------------------------------------------------------------------------
// 阶段顺序必须对**所有**帝国一致。
//
// 旧事故：「双方都是 AI 才自动结算；涉及玩家则等玩家在会议中决定」——
// 结果玩家的和平会议没有期限，永久挂起并阻塞后续所有战争建立新会议。
// 这里锁定一个更基础的不变式：同一 tick 内，玩家与 AI 走过相同的阶段序列，
// 不会出现「玩家被跳过某个阶段」的情况。
// ---------------------------------------------------------------------------
TEST(tickorder, phases_apply_to_player_and_ai_alike) {
    GameState st = orderWorld();
    const Fixed playerBefore = st.empires[kPlayerId].treasury;
    const Fixed aiBefore = st.empires[1].treasury;

    advanceOneTick(st);

    // 双方国库都发生了变化（都被经济阶段结算过）
    CHECK(st.empires[kPlayerId].treasury.rawValue() != playerBefore.rawValue());
    CHECK(st.empires[1].treasury.rawValue() != aiBefore.rawValue());
    // 双方都记录了本季净收入
    CHECK(st.empires[kPlayerId].lastIncome.rawValue() != 0);
    CHECK(st.empires[1].lastIncome.rawValue() != 0);
}

// ---------------------------------------------------------------------------
// 决议的持续代价必须在**同一 tick 内**对所有帝国一致施加。
//
// 旧事故：离散资源（国库/影响力）的损失被按 duration 重复施加 ——
// 「面包暴动」Treasury -8,000 × 24 季 = 192,000，
// 「基建攻坚」失败惩罚 -12,000 × 8 季 = 96,000（而启动成本只有 6,000）。
// 断言方式：注入一条 duration=1 的国库消耗，推进 1 tick 后只能扣一次。
// ---------------------------------------------------------------------------
TEST(tickorder, resolution_discrete_cost_applies_once_per_tick) {
    GameState st = orderWorld();
    Empire& p = st.empires[kPlayerId];
    p.treasury = Fixed(100000);
    st.market.margin.cash = p.treasury;

    ActiveEffect burn;
    burn.target = ResTarget::Treasury;
    burn.value = Fixed(-5000);
    burn.ticksLeft = 1;                 // 只持续 1 季 ⇒ 只应扣一次
    p.resolutions.active.push_back(burn);

    const Fixed before = p.treasury;
    advanceOneTick(st);

    // 注意：同一 tick 内 economyPhase 还会把本季收入加进国库，
    // 所以不能直接断言「净减少 == 5000」。用收入界定上界：
    // 若决议被重复施加 N 次，减少量会是 N × 5000 减去一季收入 ——
    // 这里锁定它 < 5000 + 一季收入（即只扣了一次）。
    const Fixed spent = before - p.treasury;
    const Fixed income = fxMax(p.lastIncome, Fixed(0));
    CHECK(spent.rawValue() >= Fixed(5000).rawValue() - income.rawValue());
    CHECK(spent.rawValue() < Fixed(5000).rawValue() + income.rawValue());
    // 该效果必须在同 tick 内被消耗掉
    CHECK(p.resolutions.active.empty() ||
          p.resolutions.active.front().ticksLeft != 1);
}

// ---------------------------------------------------------------------------
// 阶段顺序的**直接**断言。
//
// 上面那些基于副作用的测试（研究能否拿到预算、国库增量是否等于收入）
// 在最小测试世界里**捕获不到顺序回归** —— 因为市场几乎没有活动，
// 把研究移到市场之后，国库照样够用、测试照样通过。
// 这一点是实测得到的：注入「研究延后到市场之后」的突变后，
// 基于副作用的 7 个用例**全部通过**。所以顺序本身必须被直接断言。
//
// `TickReport::phaseTrace` 记录本 tick 实际执行过的阶段名，
// 下面的用例把它与期望顺序逐项比对。
// ---------------------------------------------------------------------------
namespace {

std::size_t phaseIndex(const TickReport& rep, const char* name) {
    for (std::size_t i = 0; i < rep.phaseTrace.size(); ++i)
        if (rep.phaseTrace[i] == name) return i;
    return rep.phaseTrace.size();   // 未出现 ⇒ 排在最后，比较必然失败
}

}  // namespace

TEST(tickorder, research_is_traced_before_market) {
    GameState st = orderWorld();
    TickReport rep = advanceOneTick(st);
    CHECK(phaseIndex(rep, "aiResearchPhase") < phaseIndex(rep, "phaseMarket"));
    // 两者都必须真的执行过（防止「都没出现」也算通过）
    CHECK(phaseIndex(rep, "aiResearchPhase") < rep.phaseTrace.size());
    CHECK(phaseIndex(rep, "phaseMarket") < rep.phaseTrace.size());
}

TEST(tickorder, resolutions_are_traced_before_market) {
    GameState st = orderWorld();
    TickReport rep = advanceOneTick(st);
    CHECK(phaseIndex(rep, "resolutionAiPhase") < phaseIndex(rep, "phaseMarket"));
    CHECK(phaseIndex(rep, "resolutionAiPhase") < rep.phaseTrace.size());
}

TEST(tickorder, military_is_traced_before_combat) {
    GameState st = orderWorld();
    TickReport rep = advanceOneTick(st);
    CHECK(phaseIndex(rep, "phaseMilitary") < phaseIndex(rep, "phaseCombat"));
    CHECK(phaseIndex(rep, "phaseCombat") < rep.phaseTrace.size());
}

TEST(tickorder, economy_is_the_last_settlement_before_commit) {
    GameState st = orderWorld();
    TickReport rep = advanceOneTick(st);
    CHECK(phaseIndex(rep, "phaseEconomy") < phaseIndex(rep, "phaseCommit"));
    // 经济结算必须在绝大多数阶段之后（收入是 tick 的产出，不是输入）
    CHECK(phaseIndex(rep, "phaseEconomy") > phaseIndex(rep, "phaseMarket"));
    CHECK(phaseIndex(rep, "phaseEconomy") > phaseIndex(rep, "phaseCombat"));
    CHECK(phaseIndex(rep, "phaseEconomy") > phaseIndex(rep, "phaseEvents"));
}

TEST(tickorder, trace_records_every_phase_exactly_once) {
    GameState st = orderWorld();
    TickReport rep = advanceOneTick(st);
    // 阶段不应被重复执行（此前出现过 nationalEdictPhase 每 tick 跑 3 次）
    for (std::size_t i = 0; i < rep.phaseTrace.size(); ++i)
        for (std::size_t j = i + 1; j < rep.phaseTrace.size(); ++j)
            CHECK(rep.phaseTrace[i] != rep.phaseTrace[j]);
    CHECK(rep.phaseTrace.size() >= 20);
}

// ---------------------------------------------------------------------------
// 整条流水线的**全序**断言。
//
// 上面的用例只锁定了几条关键相对顺序；这一条锁定**全部 29 个阶段**。
// 它同时是一份可执行的文档：改错顺序、漏掉阶段、意外插入阶段，都会在这里失败。
//
// 顺序不是随意的，几条关键约束及其后果：
//   · aiResearchPhase / resolutionAiPhase 在 phaseMarket **之前** ——
//     否则市场先把国库花到「恰好等于一季收入」，研究与决议永远分不到预算
//     （实测 AI 120 季只完成 3~4 项科技）
//   · phaseMilitary 在 phaseCombat **之前** —— 军备积累要能影响当季战斗
//   · phaseEconomy 在倒数第二 —— 收入是 tick 的产出，
//     必须在本季所有开支之后到账，才能被下一季使用
//   · phaseCommit 最后 —— 不变式收口、tick++、胜利评估
// ---------------------------------------------------------------------------
TEST(tickorder, full_pipeline_order_is_locked) {
    GameState st = orderWorld();
    TickReport rep = advanceOneTick(st);
    static const char* kExpected[] = {
        "phaseActionPoints", "phaseResolvePending",
        "aiResearchPhase", "resolutionAiPhase",
        "phaseMarket", "phaseVolMargin",
        "phaseReadPlayer", "phaseUpdateModel", "phaseAiActions",
        "phaseFederation", "phaseDomestic", "phaseProposals", "phaseCasus",
        "phaseIntel", "phaseStarbase", "phaseRevolt", "phaseIdeology",
        "phaseConstruction", "phaseCorruption", "phaseSpecies",
        "phaseGovernment", "phasePersonnel",
        "phaseMilitary", "phaseCombat",
        "phaseEvents", "phaseClues", "phasePlot",
        "phaseEconomy", "phaseCommit",
    };
    const std::size_t expected = sizeof(kExpected) / sizeof(kExpected[0]);
    CHECK_EQ(rep.phaseTrace.size(), expected);
    for (std::size_t i = 0; i < expected && i < rep.phaseTrace.size(); ++i)
        CHECK(rep.phaseTrace[i] == kExpected[i]);
}
