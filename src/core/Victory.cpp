#include "core/Victory.h"

#include <algorithm>

#include "core/GameState.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {
namespace {

constexpr std::array<Commodity, 3> kBasicResources = {
    Commodity::Energy, Commodity::Food, Commodity::Medicines};

bool ownsTerritory(const GameState& st, u32 empire) {
    return std::any_of(st.map.systems.begin(), st.map.systems.end(),
                      [empire](const SystemNode& s) { return s.owner == empire; }) ||
           std::any_of(st.planets.begin(), st.planets.end(),
                       [empire](const Planet& p) { return p.owner == empire; });
}

std::string percent(Fixed value) { return fixedStrPlain(value * Fixed(100), 1) + "%"; }

}  // namespace

VictoryRules victoryRules(int difficulty) {
    VictoryRules rules;
    rules.requiredQuarters = static_cast<u32>(12 + (std::clamp(difficulty, 1, 5) - 1) * 3);
    return rules;
}

VictoryStatus checkVictory(const GameState& st) {
    VictoryStatus v;
    v.rules = victoryRules(st.difficulty);
    v.won = st.victory.achieved;
    if (st.victory.achieved || st.victory.lastEvaluatedTick == st.tick)
        v.sustainedQuarters = std::min(st.victory.consecutiveQuarters, v.rules.requiredQuarters);
    const Empire* player = st.empire(kPlayerId);
    if (player == nullptr || !player->alive || !ownsTerritory(st, kPlayerId)) {
        v.unmet.push_back("玩家必须存活并拥有领土");
        return v;
    }
    for (const auto& e : st.empires) {
        if (e.id == kPlayerId || !e.alive) continue;
        // 无领土的残余舰队不算存活国家；未交战的盟友也仍是对手。
        if (!e.systems.empty() || ownsTerritory(st, e.id)) ++v.aliveRivals;
    }
    v.avgUnrest = player->domestic.unrest;
    v.minStability = player->stability;
    v.minLegitimacy = player->domestic.legitimacy;
    if (!player->domestic.factions.empty()) {
        v.minFactionSatisfaction = Fixed(1);
        for (const auto& f : player->domestic.factions) {
            v.avgFactionSatisfaction += f.satisfaction;
            v.minFactionSatisfaction = fxMin(v.minFactionSatisfaction, f.satisfaction);
        }
        v.avgFactionSatisfaction /= Fixed(static_cast<i64>(player->domestic.factions.size()));
    }

    if (v.aliveRivals > 0)
        v.unmet.push_back("仍有 " + std::to_string(v.aliveRivals) + " 个对手未被击败");
    auto minimum = [&](const char* label, Fixed value, int threshold) {
        if (value < Fixed::pct(threshold))
            v.unmet.push_back(std::string(label) + " " + percent(value) + " < " +
                              std::to_string(threshold) + "%");
    };
    if (v.avgUnrest > Fixed::pct(v.rules.maxUnrestPct))
        v.unmet.push_back("民怨 " + percent(v.avgUnrest) + " > " +
                          std::to_string(v.rules.maxUnrestPct) + "%");
    minimum("稳定度", v.minStability, v.rules.minStabilityPct);
    minimum("合法性", v.minLegitimacy, v.rules.minLegitimacyPct);
    minimum("派系平均满意度", v.avgFactionSatisfaction, v.rules.minAverageSatisfactionPct);
    minimum("最低派系满意度", v.minFactionSatisfaction, v.rules.minFactionSatisfactionPct);

    // 现金与当季经营净收入都必须非负，临时借款不能掩盖经营亏损。
    v.cash = fxMin(player->treasury, st.market.margin.cash);
    v.netIncome = player->lastIncome;
    if (v.cash < Fixed(0)) v.unmet.push_back("国库为负：" + fixedStr(v.cash, 1) + " cr");
    if (v.netIncome < Fixed(0))
        v.unmet.push_back("本季经营亏损：" + fixedStr(v.netIncome, 1) + " cr");
    for (std::size_t i = 0; i < kBasicResources.size(); ++i) {
        const auto res = static_cast<std::size_t>(kBasicResources[i]);
        v.reserves[i] = player->stock[res];
        v.reserveNeeds[i] = fxMax(Fixed(0), resourceDemand(st, *player, static_cast<int>(res))) * Fixed(v.rules.reserveQuarters);
        if (v.reserves[i] < v.reserveNeeds[i])
            v.unmet.push_back(std::string(commodityInfo(kBasicResources[i]).nameZh) + " 储备 " +
                              fixedStr(v.reserves[i], 1) + " < " + fixedStr(v.reserveNeeds[i], 1) +
                              "（" + std::to_string(v.rules.reserveQuarters) + " 季需求）");
    }
    for (const auto& r : st.revolts) {
        const SystemNode* system = st.system(r.system);
        if (system != nullptr && system->owner == kPlayerId && r.stage != RevoltStage::Calm)
            ++v.unsettledSystems;
    }
    if (v.unsettledSystems > 0)
        v.unmet.push_back("仍有 " + std::to_string(v.unsettledSystems) + " 个星系处于不安、叛乱或割据");
    if (player->domestic.coupCountdown > 0) v.unmet.push_back("政变倒计时尚未解除");
    const bool factionDemand = std::any_of(st.pending.items.begin(), st.pending.items.end(),
        [](const PendingChoice& c) { return c.kind == ChoiceKind::Faction && c.scopeTarget == kPlayerId; });
    if (factionDemand) v.unmet.push_back("仍有未处理的国内派系诉求（延后不算解决）");
    v.domesticPeace = v.unsettledSystems == 0 && player->domestic.coupCountdown == 0 && !factionDemand;
    v.currentCriteriaMet = v.unmet.empty();
    if (!v.won && v.sustainedQuarters < v.rules.requiredQuarters)
        v.unmet.push_back("连续治理达标 " + std::to_string(v.sustainedQuarters) + " / " +
                          std::to_string(v.rules.requiredQuarters) + " 季");
    return v;
}

void victoryPhase(GameState& st) {
    auto& progress = st.victory;
    if (progress.achieved || st.tick == 0 || progress.lastEvaluatedTick == st.tick) return;
    VictoryStatus v = checkVictory(st);
    const u32 previous = progress.consecutiveQuarters;
    if (st.tick != progress.lastEvaluatedTick + 1) progress.consecutiveQuarters = 0;
    progress.lastEvaluatedTick = st.tick;
    if (!v.currentCriteriaMet) {
        progress.consecutiveQuarters = 0;
        if (previous > 0)
            st.logEvent(LogPhase::Plot, "victory.interrupted",
                        "治理考验中断，连续季数归零：" + v.unmet.front(), kPlayerId);
        return;
    }
    ++progress.consecutiveQuarters;
    if (progress.consecutiveQuarters == 1)
        st.logEvent(LogPhase::Plot, "victory.started",
                    "征服与治理条件达标，开始连续 " + std::to_string(v.rules.requiredQuarters) +
                        " 季的治理考验", kPlayerId);
    if (progress.consecutiveQuarters < v.rules.requiredQuarters) return;
    progress.achieved = true;
    st.endingId = 12;
    st.ended = !st.endless;
    st.logEvent(LogPhase::Plot, kLogEnding,
                "【征服胜利】所有对手均已被击败，民心、派系、财政、物资储备与国内秩序连续 " +
                    std::to_string(v.rules.requiredQuarters) + " 季达标", kPlayerId);
}

std::string victoryReport(const GameState& st) {
    const VictoryStatus v = checkVictory(st);
    std::string out = style("═══ 征服胜利条件 ═══", Style::Heading) + std::string("\n");
    out += "  击败所有对手：剩余 " + std::to_string(v.aliveRivals) + " 个\n";
    auto metric = [&](const char* name, const char* sign, int threshold, Fixed value) {
        out += "  " + std::string(name) + " " + sign + " " + std::to_string(threshold) +
               "%    当前 " + percent(value) + "\n";
    };
    metric("民怨", "≤", v.rules.maxUnrestPct, v.avgUnrest);
    metric("稳定度", "≥", v.rules.minStabilityPct, v.minStability);
    metric("合法性", "≥", v.rules.minLegitimacyPct, v.minLegitimacy);
    metric("派系平均满意度", "≥", v.rules.minAverageSatisfactionPct, v.avgFactionSatisfaction);
    metric("最低派系满意度", "≥", v.rules.minFactionSatisfactionPct, v.minFactionSatisfaction);
    out += "  财政：国库 " + fixedStr(v.cash, 1) + " cr，本季经营净收入 " +
           fixedStr(v.netIncome, 1) + " cr（两项均须 ≥ 0）\n";
    out += "  基本物资：季末至少保留 " + std::to_string(v.rules.reserveQuarters) + " 季需求\n";
    for (std::size_t i = 0; i < kBasicResources.size(); ++i)
        out += "    " + std::string(commodityInfo(kBasicResources[i]).nameZh) + " " +
               fixedStr(v.reserves[i], 1) + " / " + fixedStr(v.reserveNeeds[i], 1) + "\n";
    out += "  国内秩序：" + std::string(v.domesticPeace ? "平静" : "尚未恢复") +
           "（无动乱星系、政变倒计时及未处理的派系诉求）\n";
    out += "  连续治理：" + std::to_string(v.sustainedQuarters) + " / " +
           std::to_string(v.rules.requiredQuarters) + " 季（难度 " + std::to_string(st.difficulty) + "）\n";
    if (v.won) return out + "\n" + style("★ 已达成征服胜利", Style::Good) + "\n";
    out += "\n  每季全部结算后检查；任一条件不达标，连续季数归零。\n";
    if (v.currentCriteriaMet) out += "  当前条件齐备，请继续推进并维持治理。\n";
    for (const auto& u : v.unmet) out += "    · " + u + "\n";
    return out;
}

}  // namespace gf
