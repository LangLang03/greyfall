// AI：透视读取 / 心智模型在线更新 / 意图预测 / 前瞻 EV 可解释且确定性 /
//     背叛微积分 / 信誉噪声 / 制衡联盟 / 一致性异常检测 / 代理人战 / 联邦投票
#include "mkt/MarketEngine.h"
#include <algorithm>

#include "ai/AiCore.h"
#include "ai/BetrayalCalculus.h"
#include "ai/FactionAI.h"
#include "ai/FederationVote.h"
#include "ai/ForwardSimulator.h"
#include "ai/FraudDetect.h"
#include "ai/IntentPredictor.h"
#include "ai/OmniscientReader.h"
#include "ai/Payoff.h"
#include "ai/PowerBalancing.h"
#include "ai/Reputation.h"
#include "ai/ToModel.h"
#include "check.h"
#include "core/GameState.h"
#include "core/TickPipeline.h"
#include "plot/BeatResolver.h"
#include "gen/WorldGen.h"

using namespace gf;

namespace {

GameState aiWorld(u64 seed = 555) {
    WorldGenOptions o;
    o.seed = seed;
    o.difficulty = 3;
    o.empireCount = 10;
    o.systemCount = 40;
    GameState st;
    generateWorld(st, o);
    return st;
}

}  // namespace

TEST(ai, omniscient_reader_sees_everything) {
    GameState st = aiWorld();
    // 玩家放一个未成交订单
    OrderRequest req;
    req.owner = kPlayerId;
    req.res = static_cast<u8>(Commodity::Alloys);
    req.exch = kExchCX;
    req.buy = true;
    req.qty = 500;
    // 挂在买一下方 0.5%，既在 ±30% 涨跌停带内，又不会立即成交
    Fixed mid = st.market.exchanges[kExchCX].books[static_cast<std::size_t>(Commodity::Alloys)].mid;
    req.px = mid - Fixed::ratio(mid, 5, 1000);
    (void)marketSubmitOrder(st, req);

    Observable obs = omniscientSnapshot(st, kPlayerId);
    CHECK_EQ(obs.resources[static_cast<std::size_t>(Commodity::Alloys)].rawValue(),
             st.empires[kPlayerId].stock[static_cast<std::size_t>(Commodity::Alloys)].rawValue());
    CHECK(!obs.fleets.empty());
    CHECK(!obs.orders.empty());            // 未成交订单可见（front-running 依据）
    CHECK(!obs.clues.empty());             // 含未 link 的私人线索
    CHECK(!obs.exposure.empty());          // 每个字段都有暴露路径
    CHECK(obs.coverage.rawValue() > 0);
    // 暴露路径里必须包含订单队列与读档计数
    bool hasOrders = false, hasRollback = false;
    for (const auto& f : obs.exposure) {
        if (f.field == "orders") hasOrders = true;
        if (f.field == "rollbackCount") hasRollback = true;
    }
    CHECK(hasOrders);
    CHECK(hasRollback);
    std::string report = whatTheyKnowReport(st, 1, kPlayerId);
    CHECK(report.find("泛视网络") != std::string::npos);
    CHECK(report.find("rollbackCount") != std::string::npos);
}

TEST(ai, reader_respects_forgery) {
    GameState st = aiWorld(101);
    // 布置假库存：AI 看到的读数被污染
    if (!st.inventory.items.empty()) {
        st.inventory.items[0].forged = true;
        st.inventory.items[0].contamination = Fixed::pct(60);
        // 给玩家一件 MarketFakeStock 类道具
        for (int i = 0; i < kItemCount; ++i) {
            if (itemDef(i).effect == ItemEffect::MarketFakeStock) {
                ItemInstance it;
                it.def = static_cast<u16>(i);
                it.forged = true;
                it.contamination = Fixed::pct(50);
                st.inventory.items.push_back(it);
                break;
            }
        }
    }
    Observable obs = omniscientSnapshot(st, kPlayerId);
    CHECK(obs.contaminatedFields > 0);
    CHECK(obs.resources[static_cast<std::size_t>(Commodity::Alloys)].rawValue() <
          st.empires[kPlayerId].stock[static_cast<std::size_t>(Commodity::Alloys)].rawValue());
}

TEST(ai, to_model_online_update_and_readmind) {
    GameState st = aiWorld(202);
    const ToModel before = st.empires[1].mind.playerModel;
    for (int i = 0; i < 8; ++i) advanceOneTick(st);
    const ToModel& after = st.empires[1].mind.playerModel;
    CHECK(after.observations > before.observations);
    CHECK(after.modelConfidence.rawValue() >= before.modelConfidence.rawValue());
    // 类型后验必须归一
    Fixed sum = Fixed(0);
    for (const auto& b : after.typeBelief) sum += b;
    CHECK(sum.rawValue() > Fixed::raw(900).rawValue());
    CHECK(sum.rawValue() < Fixed::raw(1100).rawValue());
    // θ 报告可读
    std::string report = toModelReport(st, 1, kPlayerId);
    CHECK(report.find("效用权重") != std::string::npos);
    CHECK(report.find("类型后验") != std::string::npos);
    // 注入噪声会削弱置信度、提高污染
    ToModel m = after;
    Fixed conf = m.modelConfidence;
    Fixed contamination = m.contamination;
    toModelInjectNoise(m, Fixed::pct(20), Fixed::pct(10));
    CHECK(m.modelConfidence.rawValue() < conf.rawValue());
    CHECK(m.deceptions == after.deceptions + 1);
    CHECK(m.contamination.rawValue() >= contamination.rawValue());
    // 污染会随时间衰减（AI 会逐渐校准）
    ToModel decaying = m;
    for (int i = 0; i < 30; ++i) {
        std::array<Fixed, kGoalDim> zero{};
        toModelUpdate(decaying, zero, Fixed(0));
    }
    CHECK(decaying.contamination.rawValue() < m.contamination.rawValue());
}

TEST(ai, intent_predictor_produces_demand_vector) {
    GameState st = aiWorld(303);
    ComputeBudget b = ComputeBudget::forDifficulty(3);
    IntentPrediction p = predictIntent(st, kPlayerId, b);
    CHECK(p.horizon >= 2);
    CHECK(p.confidence.rawValue() > 0);
    CHECK(!p.topPath.empty());
    Fixed total = Fixed(0);
    for (const auto& d : p.netDemand) total += d;
    CHECK(total.rawValue() > 0);   // 玩家总有产能缺口 ⇒ 存在净需求
    // top-M 剪枝生效
    CHECK(static_cast<int>(p.alternatives.size()) + 1 <= std::max(1, b.topM));
    CHECK(!intentSummary(p).empty());
}

TEST(ai, forward_simulator_is_deterministic_and_budgeted) {
    GameState st = aiWorld(404);
    ComputeBudget b1 = ComputeBudget::forDifficulty(3);
    ComputeBudget b2 = ComputeBudget::forDifficulty(3);
    IntentPrediction p = predictIntent(st, kPlayerId, b1);
    RolloutResult r1 = simulateAction(st, 1, AiAction{AiActionKind::MarketBuy, kPlayerId,
                                                      static_cast<u8>(Commodity::Alloys), 500, Fixed(0), Fixed(0), ""},
                                      p, 3, b1);
    RolloutResult r2 = simulateAction(st, 1, AiAction{AiActionKind::MarketBuy, kPlayerId,
                                                      static_cast<u8>(Commodity::Alloys), 500, Fixed(0), Fixed(0), ""},
                                      p, 3, b2);
    CHECK_EQ(r1.ev.rawValue(), r2.ev.rawValue());   // 同一状态同一动作 ⇒ 同一 EV
    CHECK(b1.nodeSpent > 0);
    CHECK(b1.nodeSpent == b2.nodeSpent);
    // 预算耗尽后必须判定为不可行而不是崩溃
    ComputeBudget small;
    small.nodeBudget = 10;
    RolloutResult r3 = simulateAction(st, 1,
                                      AiAction{AiActionKind::MarketBuy, kPlayerId,
                                               static_cast<u8>(Commodity::Alloys), 100, Fixed(0), Fixed(0), ""},
                                      p, 3, small);
    CHECK(!r3.feasible);
    CHECK(!r3.reason.empty());
}

TEST(ai, action_candidates_include_front_running) {
    GameState st = aiWorld(505);
    ComputeBudget b = ComputeBudget::forDifficulty(4);
    IntentPrediction p = predictIntent(st, kPlayerId, b);
    std::vector<AiAction> cands = generateCandidates(st, 1, p, b);
    CHECK(!cands.empty());
    CHECK(static_cast<int>(cands.size()) <= std::max(2, b.topK));
    bool market = false;
    for (const auto& a : cands)
        if (a.kind == AiActionKind::MarketBuy) market = true;
    CHECK(market);
}

TEST(ai, betrayal_ev_decomposes_and_is_explainable) {
    GameState st = aiWorld(606);
    BetrayalEV e1 = betrayalCalculus(st, 1, kPlayerId);
    BetrayalEV e2 = betrayalCalculus(st, 1, kPlayerId);
    CHECK_EQ(e1.total.rawValue(), e2.total.rawValue());   // 同一状态同 EV
    CHECK(!e1.decomposition.empty());
    CHECK(e1.decomposition.find("PV(背叛收益)") != std::string::npos);
    CHECK(e1.decomposition.find("阈值") != std::string::npos);
    CHECK(e1.threshold.rawValue() > 0);
    // EV 分解必须自洽：total == gain - compliance - repCost - warCost + exploit
    Fixed recomputed = e1.gain - e1.complianceValue - e1.reputationCost - e1.warCost + e1.exploitGain;
    CHECK_EQ(recomputed.rawValue(), e1.total.rawValue());
    // 读档会抬高战争成本 ⇒ EV 下降
    GameState st2 = st;
    st2.rollbackCount = 5;
    BetrayalEV e3 = betrayalCalculus(st2, 1, kPlayerId);
    CHECK(e3.warCost.rawValue() >= e1.warCost.rawValue());
    CHECK(!betrayalReport(st, 1, kPlayerId).empty());
}

TEST(ai, reputation_noise_does_not_dominate) {
    // 精确性质：零事件时，纯观测噪声不得把信誉推离中性太远
    GameState st = aiWorld(707);
    Fixed start = reputationOf(st, 1, kPlayerId);
    for (int i = 0; i < 30; ++i) reputationUpdate(st, 1, kPlayerId, Fixed(0));
    Fixed end = reputationOf(st, 1, kPlayerId);
    CHECK(fxAbs(end - start).rawValue() <= Fixed::pct(12).rawValue());
    // 明确的负向事件必须显著压低信誉
    for (int i = 0; i < 5; ++i) reputationUpdate(st, 1, kPlayerId, Fixed::pct(-30));
    CHECK(reputationOf(st, 1, kPlayerId).rawValue() < start.rawValue());
    // 长期无事件 ⇒ 向中性回归
    for (int i = 0; i < 200; ++i) reputationUpdate(st, 1, kPlayerId, Fixed(0));
    CHECK(fxAbs(reputationOf(st, 1, kPlayerId) - Fixed::pct(50)).rawValue() < Fixed::pct(20).rawValue());
}

TEST(ai, power_balancing_triggers_on_hegemony) {
    GameState st = aiWorld(808);
    // 让玩家变得极强
    st.empires[kPlayerId].military = Fixed(50000);
    st.empires[kPlayerId].economy = Fixed(50000);
    st.empires[kPlayerId].unity = Fixed(5000);
    Fixed rank = powerIndexRank(st, kPlayerId);
    CHECK(rank.rawValue() > Fixed::pct(50).rawValue());
    Coalition c = evaluateCoalition(st, kPlayerId);
    CHECK(c.threshold.rawValue() > 0);
    CHECK(!c.members.empty());
    CHECK(!c.reason.empty());
    for (int i = 0; i < 4; ++i) powerBalancingPhase(st);
    bool anyThreat = false;
    for (const auto& e : st.empires)
        if (!e.isPlayer && e.mind.threat[kPlayerId].rawValue() > 0) anyThreat = true;
    CHECK(anyThreat);
    CHECK(!coalitionReport(st, kPlayerId).empty());
}

TEST(ai, fraud_detect_flags_unexplained_stock_jump) {
    GameState st = aiWorld(909);
    Observable prev = omniscientSnapshot(st, kPlayerId);
    // 凭空增加库存（没有对应的订单流）
    st.empires[kPlayerId].stock[static_cast<std::size_t>(Commodity::Alloys)] += Fixed(50000);
    Observable now = omniscientSnapshot(st, kPlayerId);
    std::vector<FraudFinding> f = fraudDetect(st, 1, kPlayerId, now, prev);
    CHECK(!f.empty());
    bool stockFlag = false;
    for (const auto& x : f)
        if (x.field.find("alloys") != std::string::npos) stockFlag = true;
    CHECK(stockFlag);
    Fixed score = fraudOverallScore(f);
    CHECK(score.rawValue() > 0);
    CHECK(!f.front().reason.empty());
    // 成本信号估计
    CHECK(signalCostEstimate(st, kPlayerId, ProvChannel::Forgery).rawValue() >= 0);
}

TEST(ai, domestic_factions_and_proxy_war) {
    GameState st = aiWorld(1010);
    CHECK(!st.empires[kPlayerId].domestic.factions.empty());
    Fixed risk0 = coupRiskOf(st, kPlayerId);
    CHECK(risk0.rawValue() >= 0);
    // 让所有派系都不满 ⇒ 政变风险必须上升
    GameState st2 = st;
    for (auto& f : st2.empires[kPlayerId].domestic.factions) {
        f.satisfaction = Fixed(0);
        f.influence = Fixed::pct(90);
    }
    st2.empires[kPlayerId].domestic.legitimacy = Fixed(0);
    st2.empires[kPlayerId].domestic.unrest = Fixed::pct(90);
    Fixed risk1 = coupRiskOf(st2, kPlayerId);
    CHECK(risk1.rawValue() > risk0.rawValue());
    // 满足派系要花国库并提升满意度
    Fixed before = st.empires[kPlayerId].treasury;
    std::string err;
    CHECK(satisfyFaction(st, kPlayerId, FactionKind::Military, &err));
    CHECK(st.empires[kPlayerId].treasury.rawValue() < before.rawValue());
    for (const auto& f : st.empires[kPlayerId].domestic.factions)
        if (f.kind == FactionKind::Military) CHECK(f.satisfaction.rawValue() > Fixed(0).rawValue());
    // 压制会增加民怨
    Fixed unrest = st.empires[kPlayerId].domestic.unrest;
    CHECK(suppressFaction(st, kPlayerId, FactionKind::Populist, &err));
    CHECK(st.empires[kPlayerId].domestic.unrest.rawValue() > unrest.rawValue());
    // 代理人战：AI 会资助玩家的派系
    bool patronized = false;
    for (int i = 0; i < 40 && !patronized; ++i) {
        factionAiPhase(st);
        for (const auto& f : st.empires[kPlayerId].domestic.factions)
            if (f.patron != 0xFFFFFFFFu) patronized = true;
    }
    CHECK(patronized);
    CHECK(!domesticReport(st, kPlayerId).empty());
}

TEST(ai, federation_weighted_vote_and_logrolling) {
    GameState st = aiWorld(1111);
    if (st.federations.empty()) return;
    u32 fid = 0;
    FederalMotion m = proposeMotion(st, fid, VoteSubject::TaxHarmonize, 0xFFFFFFFFu, st.federations[fid].founder);
    bool passed = resolveMotion(st, fid, m);
    (void)passed;
    CHECK(m.resolved);
    CHECK_EQ(m.yesWeight.rawValue() + m.noWeight.rawValue() >= 0, true);
    // 投票权重随实力与让利上升
    Fixed w1 = federationVoteWeight(Fixed(100), Fixed(10), Fixed(0));
    Fixed w2 = federationVoteWeight(Fixed(200), Fixed(10), Fixed(0));
    Fixed w3 = federationVoteWeight(Fixed(100), Fixed(10), Fixed::pct(20));
    CHECK(w2.rawValue() > w1.rawValue());
    CHECK(w3.rawValue() > w1.rawValue());
}

// 回归守卫：AI 的 Build 行动曾与 Research 合并，只做研究、从不建造 ——
// 全世界 240 季零建筑，AI 国库一路囤到 19.6 万却无处分花，
// 建筑系统（产出/研究/防御）对 AI 完全失效。
// 回归守卫：AI 曾几乎不研究 —— 每 tick 只投入「收入的 20%」，
// 换算后约 55 研究点/次、进度约 13/tick，120 季只完成 3~4 项科技，
// 而相同收入的玩家靠**攒够 8000 cr 一次性投入**可完成 29 项。
// 根因是投入粒度：AI 缺少「为研究储蓄」的跨期预算能力。
TEST(ai, empires_research_over_time) {
    GameState st = aiWorld(8001);
    for (int t = 0; t < 120; ++t) {
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);
    }
    int total = 0, n = 0, best = 0;
    for (const auto& e : st.empires) {
        if (e.isPlayer || !e.alive) continue;
        int k = static_cast<int>(e.tech.completed.size());
        total += k;
        best = std::max(best, k);
        ++n;
    }
    CHECK(n > 0);
    // 必须真的在研究。注意：研究已改为**立项制 + 最短工期**
    //（1 级 6 季、6 级 16 季，钱多也不能更快），因此节奏远慢于改造前 ——
    // 20 万点的整棵树需要跨纪元才能穷尽，120 季完成 2~6 项是正常区间。
    CHECK(best >= 2);
    CHECK(total >= n);
}

// 研究投入不得把 AI 的内政拖垮：赤字可以出现，但稳定度/民怨必须保持健康
TEST(ai, research_spending_does_not_wreck_domestic_state) {
    GameState st = aiWorld(8002);
    for (int t = 0; t < 150; ++t) {
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);
    }
    int n = 0;
    Fixed stbSum = Fixed(0), unrSum = Fixed(0);
    for (const auto& e : st.empires) {
        if (e.isPlayer || !e.alive) continue;
        stbSum += e.stability;
        unrSum += e.domestic.unrest;
        ++n;
    }
    CHECK(n > 0);
    // 稳定度不应崩到 0，民怨不应钉在 100%
    CHECK(stbSum.rawValue() / n > Fixed::pct(30).rawValue());
    CHECK(unrSum.rawValue() / n < Fixed::pct(60).rawValue());
}

TEST(ai, empires_construct_buildings_over_time) {
    GameState st = aiWorld(7001);
    int before = 0;
    for (const auto& p : st.planets)
        if (p.owner != kNoEmpire) before += static_cast<int>(p.buildings.size());
    CHECK_EQ(before, 0);
    for (int t = 0; t < 120; ++t) {
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);
    }
    int after = 0;
    int empiresWithBuildings = 0;
    for (const auto& e : st.empires) {
        int n = 0;
        for (const auto& p : st.planets)
            if (p.owner == e.id) n += static_cast<int>(p.buildings.size());
        after += n;
        if (n > 0) ++empiresWithBuildings;
    }
    // 必须真的建了东西
    CHECK(after > 0);
    // 且不应只有一个帝国会建
    CHECK(empiresWithBuildings >= 2);
}

TEST(ai, construction_respects_upkeep_sustainability) {
    // 回归守卫：没有维护费闸门时 AI 会一路建到国库为负
    //（实测 240 季 273 座、平均国库 -4.4 万）。
    GameState st = aiWorld(7002);
    for (int t = 0; t < 150; ++t) {
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);
    }
    // AI 国库不得因过度建造而长期为负
    int negative = 0;
    int alive = 0;
    for (const auto& e : st.empires) {
        if (e.isPlayer || !e.alive) continue;
        ++alive;
        if (e.treasury.rawValue() < Fixed(-20000).rawValue()) ++negative;
    }
    CHECK(alive > 0);
    CHECK(negative <= alive / 2);
}

TEST(ai, payoff_var_and_retaliation) {
    GameState st = aiWorld(1212);
    for (int i = 0; i < 10; ++i) advanceOneTick(st);
    PayoffBreakdown b = payoffBreakdown(st, kPlayerId, st.empires[kPlayerId].mind.playerModel.wGoal);
    CHECK(b.total.rawValue() != 0);
    CHECK(payoffVaR(st, kPlayerId).rawValue() >= 0);
    CHECK(payoffAllianceRetaliation(st, 1, kPlayerId).rawValue() >= 0);
}
