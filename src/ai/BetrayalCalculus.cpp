#include "ai/ToModel.h"
#include "util/Fmt.h"
#include "domain/ModifierUtil.h"
#include "ai/BetrayalCalculus.h"

#include <algorithm>

#include "ai/Payoff.h"
#include "ai/Reputation.h"
#include "combat/Resolver.h"
#include "domain/Treaty.h"
#include "gen/EmpireGen.h"
#include "util/Str.h"

namespace gf {

BetrayalEV betrayalCalculus(const GameState& st, u32 actor, u32 target) {
    BetrayalEV ev;
    const Empire* a = st.empire(actor);
    const Empire* t = st.empire(target);
    if (a == nullptr || t == nullptr) return ev;
    const EmpireMind& mind = a->mind;
    Fixed delta = mind.playerModel.discount;

    // 所有分量统一到「千信用点(kcr)」量纲，便于与阈值比较、也便于复盘。

    // ---- PV(背叛收益, T=8)：夺取 12% 库存 + 影响力 + 航线 ----
    Fixed stockValue = Fixed(0);
    for (int c = 0; c < kCommodityCount; ++c) {
        Fixed px = st.market.spotIndex[static_cast<std::size_t>(c)];
        if (px.rawValue() <= 0) px = commodityInfo(c).basePrice;
        stockValue += Fixed::raw(mulDivSat(t->stock[static_cast<std::size_t>(c)].rawValue(), px.rawValue(), FIX));
    }
    Fixed grabCr = stockValue * Fixed::pct(12) + t->influence * Fixed(200) +
                   Fixed(static_cast<i64>(t->systems.size())) * Fixed(20000);
    Fixed rawGainKcr = grabCr / Fixed(1000);
    Fixed pvGain = Fixed(0);
    Fixed pow = Fixed(1);
    for (int i = 0; i < 8; ++i) {
        pvGain += (rawGainKcr / Fixed(8)) * pow;
        pow = pow * delta;
    }
    ev.gain = pvGain;

    // ---- PV(履约价值)：继续合作能拿到的贸易与援助 ----
    Fixed perTickCr = Fixed(0);
    if (hasTreaty(st.treaties, TreatyKind::TradePact, actor, target)) perTickCr += Fixed(1500);
    if (hasTreaty(st.treaties, TreatyKind::ResearchPact, actor, target)) perTickCr += Fixed(900);
    if (hasTreaty(st.treaties, TreatyKind::DefensivePact, actor, target)) perTickCr += Fixed(1800);
    perTickCr += mind.reputation[target] * Fixed(8000);
    Fixed pvCompliance = Fixed(0);
    pow = Fixed(1);
    for (int i = 0; i < 8; ++i) {
        pvCompliance += perTickCr * pow;
        pow = pow * delta;
    }
    ev.complianceValue = pvCompliance / Fixed(1000);

    // ---- 信誉现值损失：全体观感 → 未来贸易与援助折损 ----
    Fixed repLossPoints = Fixed(0);
    for (const auto& other : st.empires) {
        if (other.id == actor || other.id == target || !other.alive) continue;
        Fixed sympathy = fxMax(other.opinionOf(target), Fixed(0));
        repLossPoints += Fixed::pct(10) + sympathy * Fixed::pct(25);
    }
    if (t->federation != 0xFFFFFFFFu && t->federation < st.federations.size()) {
        repLossPoints += Fixed::pct(15) * Fixed(static_cast<i64>(st.federations[t->federation].members.size()));
    }
    repLossPoints = repLossPoints *
                    (Fixed(1) + mind.playerModel.typeBelief[static_cast<std::size_t>(ActorType::Retaliate)]);
    // 每个信誉点的现值折损 ≈ 25 kcr
    ev.reputationCost = repLossPoints * Fixed(25);

    // ---- E[warCost] ----
    Fixed odds = combatOdds(st, target, actor);      // 目标反击我的胜率
    Fixed myLoss = a->military * odds * Fixed::pct(60);
    Fixed rebuildCr = myLoss * Fixed(2000) + Fixed(40000) * (Fixed(1) - odds);
    ev.warCost = rebuildCr / Fixed(1000);
    ev.warCost = ev.warCost * (Fixed(1) + Fixed(static_cast<i64>(st.rollbackCount)) * Fixed::pct(20));
    if (hasModifier(st.modifierBits, kModWarEcho)) ev.warCost = ev.warCost * Fixed::pct(80);

    // ---- ExploitGain：if playerModel.可欺 ----
    bool exploitable = mind.playerModel.modelConfidence.rawValue() > Fixed::pct(45).rawValue() &&
                       mind.playerModel.typeBelief[static_cast<std::size_t>(ActorType::Cooperate)].rawValue() >
                           Fixed::pct(30).rawValue();
    if (exploitable) ev.exploitGain = mind.playerModel.modelConfidence * Fixed(60);
    ev.exploitGain += mind.playerModel.contamination * Fixed(40);

    ev.total = ev.gain - ev.complianceValue - ev.reputationCost - ev.warCost + ev.exploitGain;

    // 阈值：随自身体量与智能等级提高（强者机会成本更高，不轻易背约）
    ev.threshold = a->powerIndex() / Fixed(6) + Fixed(2) * Fixed(static_cast<i64>(mind.foresight)) + Fixed(2);
    if (st.difficulty >= 4) ev.threshold += Fixed(3);
    // 军事劣势足以翻盘时不动手（不可逆打击也要打得赢）
    bool militarilyCapable = a->military.rawValue() > (t->military * Fixed::pct(70)).rawValue();
    ev.shouldBetray = ev.total.rawValue() > ev.threshold.rawValue() && militarilyCapable;

    std::string verdict;
    if (ev.shouldBetray) verdict = "决定背约";
    else if (ev.total.rawValue() <= ev.threshold.rawValue()) verdict = "暂不背约（EV 未过阈值）";
    else verdict = "暂不背约（军力不足以打赢）";
    ev.decomposition =
        "PV(背叛收益)=" + fixedStr(ev.gain, 2) + " − PV(履约)=" + fixedStr(ev.complianceValue, 2) +
        " − 信誉损失=" + fixedStr(ev.reputationCost, 2) + " − E[战争成本]=" + fixedStr(ev.warCost, 2) +
        " + 可欺收益=" + fixedStr(ev.exploitGain, 2) + "  ⇒ EV=" + fixedStrSigned(ev.total, 2) +
        "（阈值 " + fixedStr(ev.threshold, 2) + "）→ " + verdict;
    return ev;
}

void betrayalLog(GameState& st, u32 actor, u32 target, const BetrayalEV& ev) {
    Empire* a = st.empire(actor);
    if (a == nullptr) return;
    // 只在「决定背约」或 EV 相对上次发生显著变化时才写日志。
    // 每 tick 对每个帝国都记录一次 EV 分解会把日志刷爆，
    // 把真正的战报与外交事件挤出环形缓冲。
    Fixed prev = a->mind.lastBetrayalEV;
    bool decisive = ev.shouldBetray;
    Fixed delta = fxAbs(ev.total - prev);
    Fixed threshold = fxMax(Fixed(20), fxAbs(prev) / Fixed(10));
    bool material = prev.rawValue() == 0 || delta.rawValue() >= threshold.rawValue();

    a->mind.lastBetrayalEV = ev.total;
    a->mind.lastBetrayalRepCost = ev.reputationCost;
    a->mind.lastBetrayalWarCost = ev.warCost;
    a->mind.lastBetrayalGain = ev.gain;
    a->mind.lastBetrayalReason = ev.decomposition;
    if (!decisive && !material) return;
    st.logEvent(LogPhase::Ai, kLogBetrayal,
                std::string(decisive ? "【决定背约】" : "【背叛 EV 变化】") + " 对 " +
                    (st.empire(target) ? st.empire(target)->name : std::string("?")) + "：" + ev.decomposition,
                actor, ev.total);
}

std::string betrayalReport(const GameState& st, u32 actor, u32 target) {
    const Empire* a = st.empire(actor);
    if (a == nullptr) return "非法主体";
    BetrayalEV ev = betrayalCalculus(st, actor, target);
    std::string out;
    out += "═══ 背叛 EV 分解：" + a->name + " → " +
           std::string(st.empire(target) ? st.empire(target)->name : "?") + " ═══\n";
    out += "  PV(背叛收益, T=8)        " + fixedStrSigned(ev.gain, 2) + "\n";
    out += "  − PV(履约价值)           " + fixedStr(-ev.complianceValue, 2) + "\n";
    out += "  − RepCost(全体观感损失)  " + fixedStr(-ev.reputationCost, 2) + "\n";
    out += "  − E[warCost]             " + fixedStr(-ev.warCost, 2) + "\n";
    out += "  + ExploitGain(可欺)      " + fixedStrSigned(ev.exploitGain, 2) + "\n";
    out += "  ─────────────────────────────\n";
    out += "  EV = " + fixedStrSigned(ev.total, 2) + "   阈值 = " + fixedStr(ev.threshold, 2) + "\n";
    out += std::string("  结论：") + (ev.shouldBetray ? style("它会动手", Style::Bad) : style("暂不动手", Style::Good)) + "\n";
    out += "\n为什么（可复盘的动机）：\n";
    out += "  · 你的国力指数 " + fixedStr(st.empires[kPlayerId].powerIndex(), 2) + "，它的 " +
           fixedStr(a->powerIndex(), 2) + "\n";
    out += "  · 它对你心智模型的置信度 " + fixedStrPlain(a->mind.playerModel.modelConfidence, 2) +
           "（越高越敢勒索）\n";
    out += "  · 它认为你是【" +
           std::string(actorTypeName(toModelDominantType(a->mind.playerModel))) + "】\n";
    out += "  · 你的读档次数 " + std::to_string(st.rollbackCount) + "：每多一次，战争成本预估被抬高 20%\n";
    if (a->mind.playerModel.contamination.rawValue() > 0) {
        out += "  · 它已被你的伪造数据污染 " + fixedStrPlain(a->mind.playerModel.contamination * Fixed(100), 1) +
               "%（这既是机会也是风险）\n";
    }
    return out;
}

}  // namespace gf
