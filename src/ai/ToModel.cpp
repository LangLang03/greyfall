#include "util/Fmt.h"
#include "ai/ToModel.h"

#include <algorithm>

#include "util/Str.h"

namespace gf {
namespace {

Fixed normalize(Fixed v, Fixed scale) {
    if (scale.rawValue() <= 0) return Fixed(0);
    return fxClamp(v / scale, Fixed(-1), Fixed(1));
}

}  // namespace

std::array<Fixed, kGoalDim> actionGradient(const GameState& st, const Observable& obs, const Observable& prev) {
    std::array<Fixed, kGoalDim> g{};
    for (auto& v : g) v = Fixed(0);

    // 领土：殖民队列 + 巨构
    Fixed territory = Fixed(static_cast<i64>(obs.colonizing.size())) * Fixed::pct(30);
    territory += Fixed(static_cast<i64>(obs.megastructureIds.size())) * Fixed::pct(20);
    g[static_cast<std::size_t>(GoalDim::Territory)] = normalize(territory, Fixed(1));

    // 财富：国库变化 + 库存变化
    Fixed dCash = obs.treasury - prev.treasury;
    Fixed wealth = normalize(dCash, Fixed(50000));
    g[static_cast<std::size_t>(GoalDim::Wealth)] = wealth;

    // 科技：已完成科技的增量
    Fixed dTech = Fixed(static_cast<i64>(obs.techCompleted.size())) - Fixed(static_cast<i64>(prev.techCompleted.size()));
    g[static_cast<std::size_t>(GoalDim::Science)] = normalize(dTech, Fixed(2));

    // 军事：舰队战力变化
    Fixed mil = Fixed(0);
    for (const auto& f : obs.fleets) mil += f.strength;
    Fixed prevMil = Fixed(0);
    for (const auto& f : prev.fleets) prevMil += f.strength;
    g[static_cast<std::size_t>(GoalDim::Military)] = normalize(mil - prevMil, Fixed(200));

    // 声望：影响力变化
    g[static_cast<std::size_t>(GoalDim::Prestige)] = normalize(obs.influence - prev.influence, Fixed(300));

    // 知识：线索增量
    Fixed dClue = Fixed(static_cast<i64>(obs.clues.size())) - Fixed(static_cast<i64>(prev.clues.size()));
    g[static_cast<std::size_t>(GoalDim::Knowledge)] = normalize(dClue, Fixed(3));

    // 稳定：稳定度变化
    g[static_cast<std::size_t>(GoalDim::Stability)] = normalize(obs.stability - prev.stability, Fixed::pct(20));

    // 信仰：凝聚力变化
    g[static_cast<std::size_t>(GoalDim::Faith)] = normalize(obs.unity - prev.unity, Fixed(200));

    // 内幕/前置交易会污染梯度（观测本身被布置过）
    for (const auto& f : obs.exposure) {
        if (!f.forged) continue;
        for (auto& v : g) v = v * Fixed::raw(700);   // 衰减 30%
    }
    (void)st;
    return g;
}

void toModelUpdate(ToModel& m, const std::array<Fixed, kGoalDim>& grad, Fixed learningRate) {
    // E[grad] 用当前权重下的期望近似：w_goal 归一化后的方向
    Fixed sum = Fixed(0);
    for (auto& w : m.wGoal) sum += fxAbs(w);
    if (sum.rawValue() <= 0) sum = Fixed(1);
    for (std::size_t i = 0; i < grad.size(); ++i) {
        Fixed expected = m.wGoal[i] / sum;
        Fixed delta = (grad[i] - expected) * learningRate;
        m.wGoal[i] = fxClamp(m.wGoal[i] + delta, Fixed(0), Fixed(3));
    }
    ++m.observations;
    // 污染随时间衰减（AI 会逐渐校准），否则会被一次伪造永久致盲
    m.contamination = m.contamination * Fixed::pct(96);
    if (m.contamination.rawValue() < 1) m.contamination = Fixed(0);
    // 置信度：观测越多、越一致 → 越高（上限 0.95）
    Fixed target = Fixed::raw(FIX - mulDivSat(FIX, 1, 1 + static_cast<i64>(m.observations) / 3));
    m.modelConfidence = fxLerp(m.modelConfidence, fxMin(target, Fixed::pct(95)), Fixed::pct(25));
}

void toModelUpdateType(ToModel& m, const Observable& obs, const GameState& st, Fixed eta) {
    // 似然：从可观测行为推断类型
    Fixed coop = Fixed(0), exploit = Fixed(0), retaliate = Fixed(0), myopic = Fixed(0);
    // 合作：高信任、少违约、有条约
    int treaties = 0;
    for (const auto& t : st.treaties)
        if (t.a == kPlayerId || t.b == kPlayerId) ++treaties;
    coop = Fixed(treaties) * Fixed::pct(10);
    if (obs.rollbackCount == 0) coop += Fixed::pct(8);
    // 剥削：读档多、黑市活跃、操纵记录
    exploit = Fixed(static_cast<i64>(obs.rollbackCount)) * Fixed::pct(12);
    for (const auto& rec : st.market.manipulations)
        if (rec.actor == kPlayerId) exploit += Fixed::pct(10);
    if (obs.chronicleBurned) exploit += Fixed::pct(20);
    // 报复：曾对伤害过自己的主体采取行动
    if (st.empire(kPlayerId) != nullptr) {
        retaliate = Fixed(static_cast<i64>(st.empire(kPlayerId)->betrayalsSuffered)) * Fixed::pct(8);
    }
    // 短视：国库低、库存低、无长期工程
    if (obs.treasury.rawValue() < Fixed::raw(20000).rawValue()) myopic += Fixed::pct(25);
    if (obs.megastructureIds.empty()) myopic += Fixed::pct(10);

    Fixed total = coop + exploit + retaliate + myopic;
    if (total.rawValue() <= 0) total = Fixed(1);
    std::array<Fixed, kTypeCount> likelihood = {coop / total, exploit / total, retaliate / total, myopic / total};
    for (std::size_t i = 0; i < m.typeBelief.size(); ++i) {
        Fixed posterior = m.typeBelief[i] * (Fixed::raw(200) + likelihood[i] * Fixed(3));
        m.typeBelief[i] = posterior;
    }
    Fixed sum = Fixed(0);
    for (auto& b : m.typeBelief) sum += b;
    if (sum.rawValue() <= 0) sum = Fixed(1);
    for (auto& b : m.typeBelief) b = b / sum;
    (void)eta;
}

Fixed toModelInferRisk(const GameState& st, const Observable& obs) {
    // 杠杆 + 持仓集中度 → 风险偏好
    Fixed leverage = Fixed(0);
    int n = 0;
    for (const auto& p : st.market.futuresPositions) {
        if (p.owner != kPlayerId || p.qty <= 0) continue;
        leverage += Fixed(p.leverage);
        ++n;
    }
    if (n > 0) leverage = Fixed::raw(leverage.rawValue() / n);
    Fixed concentration = Fixed(0);
    Fixed total = Fixed(0);
    for (const auto& r : obs.resources) total += r;
    if (total.rawValue() > 0) {
        for (const auto& r : obs.resources) {
            Fixed share = r / total;
            concentration += share * share;
        }
    }
    Fixed risk = fxClamp(leverage / Fixed(10) + concentration * Fixed(2), Fixed(0), Fixed(1));
    return risk;
}

Fixed toModelInferDiscount(const GameState& st, const Observable& obs) {
    // 有长期工程 + 高储蓄 → 高 δ
    Fixed savings = obs.treasury / (obs.treasury + Fixed(60000));
    Fixed longTerm = Fixed(static_cast<i64>(obs.megastructureIds.size())) * Fixed::pct(20);
    Fixed delta = Fixed::raw(700) + savings * Fixed::raw(150) + longTerm;
    // 读档多 → 低 δ（短视）
    delta -= Fixed(static_cast<i64>(obs.rollbackCount)) * Fixed::raw(60);
    (void)st;
    return fxClamp(delta, Fixed::raw(500), Fixed::raw(995));
}

void toModelUpdateHabits(ToModel& m, const Observable& obs, const GameState& st) {
    (void)st;
    std::array<Fixed, kHabitDim> obsHabits{};
    for (auto& h : obsHabits) h = Fixed(0);
    // 0 囤积 1 杠杆 2 外交 3 军事 4 情报 5 工程 6 舆论 7 读档
    Fixed stock = Fixed(0);
    for (const auto& r : obs.resources) stock += r;
    obsHabits[0] = fxClamp(stock / Fixed(60000), Fixed(0), Fixed(1));
    i64 positions = 0;
    for (const auto& p : st.market.futuresPositions) if (p.owner == kPlayerId && p.qty > 0) ++positions;
    obsHabits[1] = fxClamp(Fixed(positions) / Fixed(4), Fixed(0), Fixed(1));
    int treaties = 0;
    for (const auto& t : st.treaties)
        if (t.a == kPlayerId || t.b == kPlayerId) ++treaties;
    obsHabits[2] = fxClamp(Fixed(treaties) / Fixed(6), Fixed(0), Fixed(1));
    obsHabits[3] = fxClamp(obs.military / Fixed(1200), Fixed(0), Fixed(1));
    obsHabits[4] = fxClamp(Fixed(static_cast<i64>(obs.clues.size())) / Fixed(12), Fixed(0), Fixed(1));
    obsHabits[5] = fxClamp(Fixed(static_cast<i64>(obs.megastructureIds.size() + obs.colonizing.size())) / Fixed(4),
                           Fixed(0), Fixed(1));
    obsHabits[6] = fxClamp(obs.influence / Fixed::raw(800), Fixed(0), Fixed(1));
    obsHabits[7] = fxClamp(Fixed(static_cast<i64>(obs.rollbackCount)) / Fixed(3), Fixed(0), Fixed(1));
    for (std::size_t i = 0; i < m.habits.size(); ++i) {
        m.habits[i] = fxLerp(m.habits[i], obsHabits[i], Fixed::pct(30));
    }
    for (std::size_t i = 0; i < m.habits.size(); ++i) {
        if (st.empire(kPlayerId) != nullptr) {
            // 同步到"已识别的套路"（AI 之间共享的玩家画像）
        }
    }
    for (auto& e : const_cast<GameState&>(st).empires) {
        if (e.isPlayer) continue;
        for (std::size_t i = 0; i < e.mind.playerPattern.size(); ++i) {
            e.mind.playerPattern[i] = fxLerp(e.mind.playerPattern[i], obsHabits[i], Fixed::pct(10));
        }
    }
}

void toModelInjectNoise(ToModel& m, Fixed noise, Fixed contamination) {
    m.lastGradientNoise = noise;
    m.contamination = fxClamp(m.contamination + contamination, Fixed(0), Fixed(1));
    // 噪声直接削弱置信度
    m.modelConfidence = fxClamp(m.modelConfidence - noise, Fixed(0), Fixed(1));
    m.deceptions += 1;
}

ActorType toModelDominantType(const ToModel& m) {
    ActorType best = ActorType::Cooperate;
    Fixed bv = m.typeBelief[0];
    for (int i = 1; i < kTypeCount; ++i) {
        if (m.typeBelief[static_cast<std::size_t>(i)].rawValue() > bv.rawValue()) {
            bv = m.typeBelief[static_cast<std::size_t>(i)];
            best = static_cast<ActorType>(i);
        }
    }
    return best;
}

std::string toModelReport(const GameState& st, u32 observer, u32 subject) {
    const Empire* o = st.empire(observer);
    if (o == nullptr) return "非法观测者";
    // 当前实现只报告 observer 持有的心智模型；subject 保留以便将来区分
    // "它对你的模型" 与 "它对他人的模型"。
    (void)subject;
    const ToModel& m = o->mind.playerModel;
    std::string out;
    out += "═══ 镜像博弈：它对你的心智模型 ═══\n";
    out += "观测者：" + o->name + "\n\n";
    out += "效用权重 wGoal（它认为你想要什么）：\n";
    Fixed sum = Fixed(0);
    for (const auto& w : m.wGoal) sum += w;
    for (int i = 0; i < kGoalDim; ++i) {
        Fixed share = sum.rawValue() > 0 ? m.wGoal[static_cast<std::size_t>(i)] / sum : Fixed(0);
        out += "  " + padRight(std::string(goalDimName(static_cast<GoalDim>(i))), 8) + " " +
               fixedStrPlain(share * Fixed(100), 1) + "%\n";
    }
    out += "\n风险偏好 riskAversion：" + fixedStrPlain(m.riskAversion, 2) + "\n";
    out += "贴现因子 δ：" + fixedStrPlain(m.discount, 3) + "\n";
    out += "类型后验：";
    for (int i = 0; i < kTypeCount; ++i) {
        out += std::string(actorTypeName(static_cast<ActorType>(i))) + " " +
               fixedStrPlain(m.typeBelief[static_cast<std::size_t>(i)] * Fixed(100), 1) + "%  ";
    }
    out += "\n判定类型：" + std::string(actorTypeName(toModelDominantType(m))) + "\n";
    out += "模型置信度 modelConfidence：" + fixedStrPlain(m.modelConfidence, 2) + "（越高 → 越敢提前布防/勒索）\n";
    out += "观测样本：" + std::to_string(m.observations) + "    检测到的欺骗：" + std::to_string(m.deceptions) +
           "\n";
    if (m.contamination.rawValue() > 0) {
        out += "已被你的伪造数据污染：" + fixedStrPlain(m.contamination * Fixed(100), 1) + "%\n";
    }
    out += "\n已识别的惯性套路：\n";
    static const char* kHabitNames[] = {"囤积", "杠杆", "外交", "军事", "情报", "工程", "舆论", "读档"};
    for (int i = 0; i < kHabitDim; ++i) {
        out += "  " + padRight(kHabitNames[i], 6) + " " +
               bar(m.habits[static_cast<std::size_t>(i)], 20) + " " +
               fixedStrPlain(m.habits[static_cast<std::size_t>(i)] * Fixed(100), 0) + "%\n";
    }
    out += "\n（你可以用 decoy 布置数据、用 forge-prove 污染它的模型来改变上面的数字）\n";
    return out;
}

}  // namespace gf
