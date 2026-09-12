#include "ai/Payoff.h"

#include <algorithm>

#include "domain/Federation.h"
#include "domain/Treaty.h"

namespace gf {
namespace {

Fixed wealthScore(const GameState& st, const Empire& e) {
    Fixed stock = Fixed(0);
    for (const auto& s : e.stock) stock += s;
    Fixed cash = e.isPlayer ? st.market.margin.cash : e.treasury;
    return cash / Fixed(1000) + stock / Fixed(100);
}

}  // namespace

PayoffBreakdown payoffBreakdown(const GameState& st, u32 actor, const std::array<Fixed, kGoalDim>& w) {
    PayoffBreakdown b;
    const Empire* e = st.empire(actor);
    if (e == nullptr) return b;

    // 领土
    b.components[static_cast<std::size_t>(GoalDim::Territory)] =
        Fixed(static_cast<i64>(e->systems.size())) + Fixed(static_cast<i64>(e->colonizing.size())) * Fixed(2);
    // 财富
    b.components[static_cast<std::size_t>(GoalDim::Wealth)] = wealthScore(st, *e);
    // 科技
    b.components[static_cast<std::size_t>(GoalDim::Science)] =
        Fixed(static_cast<i64>(e->tech.completed.size())) * Fixed(2) + e->tech.rate + e->tech.rateBonus;
    // 军事
    Fixed mil = Fixed(0);
    for (u32 fid : e->fleets) {
        const Fleet* f = st.fleet(fid);
        if (f != nullptr) mil += f->strength;
    }
    b.components[static_cast<std::size_t>(GoalDim::Military)] = mil / Fixed(100);
    // 声望
    b.components[static_cast<std::size_t>(GoalDim::Prestige)] = e->influence / Fixed(100);
    // 知识
    Fixed clues = Fixed(static_cast<i64>(st.plot.knownClues.size()));
    if (actor == kPlayerId) clues += Fixed(static_cast<i64>(st.plot.committedConclusions.size())) * Fixed(2);
    else clues += Fixed(static_cast<i64>(e->tech.completed.size())) / Fixed(2);
    b.components[static_cast<std::size_t>(GoalDim::Knowledge)] = clues;
    // 稳定
    b.components[static_cast<std::size_t>(GoalDim::Stability)] =
        e->stability * Fixed(10) - e->domestic.unrest * Fixed(10);
    // 信仰
    b.components[static_cast<std::size_t>(GoalDim::Faith)] = e->unity / Fixed(100);

    for (std::size_t i = 0; i < b.components.size(); ++i) b.total += b.components[i] * w[i];
    return b;
}

Fixed payoffUtility(const GameState& st, u32 actor, const std::array<Fixed, kGoalDim>& w) {
    return payoffBreakdown(st, actor, w).total;
}

Fixed payoffVaR(const GameState& st, u32 actor, int window) {
    if (st.history.size() < 2) return Fixed(0);
    std::vector<Fixed> deltas;
    std::size_t start = st.history.size() > static_cast<std::size_t>(window) ? st.history.size() - static_cast<std::size_t>(window) : 1;
    for (std::size_t i = start; i < st.history.size(); ++i) {
        if (actor >= kMaxEmpires) break;
        deltas.push_back(st.history[i].score[actor] - st.history[i - 1].score[actor]);
    }
    if (deltas.empty()) return Fixed(0);
    std::sort(deltas.begin(), deltas.end());
    // 5% 分位
    std::size_t idx = deltas.size() / 20;
    if (idx >= deltas.size()) idx = deltas.size() - 1;
    Fixed worst = deltas[idx];
    return worst.rawValue() < 0 ? Fixed::raw(-worst.rawValue()) : Fixed(0);
}

Fixed payoffAllianceRetaliation(const GameState& st, u32 actor, u32 target) {
    Fixed cost = Fixed(0);
    // 目标的联邦成员会一起反制
    const Empire* t = st.empire(target);
    if (t != nullptr && t->federation != 0xFFFFFFFFu && t->federation < st.federations.size()) {
        for (u32 m : st.federations[t->federation].members) {
            if (m == actor || m == target) continue;
            const Empire* ally = st.empire(m);
            if (ally == nullptr || !ally->alive) continue;
            cost += ally->military / Fixed(200);
            // 与我方观感越好，越可能反制
            cost += ally->opinion[actor].rawValue() < 0 ? Fixed::pct(5) : Fixed(0);
        }
    }
    // 防御条约
    for (const auto& tr : st.treaties) {
        if (tr.kind != TreatyKind::DefensivePact && tr.kind != TreatyKind::Federation) continue;
        u32 other = 0xFFFFFFFFu;
        if (tr.a == target) other = tr.b;
        else if (tr.b == target) other = tr.a;
        if (other == 0xFFFFFFFFu || other == actor) continue;
        const Empire* o = st.empire(other);
        if (o == nullptr || !o->alive) continue;
        cost += o->military / Fixed(150) * tr.compliance;
    }
    // 共同的观感惩罚扩散
    Fixed avgOpinion = Fixed(0);
    int n = 0;
    for (const auto& e : st.empires) {
        if (e.id == actor || e.id == target) continue;
        avgOpinion += e.opinionOf(actor);
        ++n;
    }
    if (n > 0) {
        avgOpinion = Fixed::raw(avgOpinion.rawValue() / n);
        if (avgOpinion.rawValue() < 0) cost -= avgOpinion * Fixed(4);
    }
    return cost;
}

}  // namespace gf
