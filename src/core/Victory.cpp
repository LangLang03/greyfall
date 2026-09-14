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
    // 连续性必须在覆盖 lastEvaluatedTick **之前**判断：
    // 先赋值会让 `st.tick != lastEvaluatedTick + 1` 恒为真，连续季数永远归零。
    const bool consecutive = (st.tick == progress.lastEvaluatedTick + 1);
    progress.lastEvaluatedTick = st.tick;

    // ---- 破产计时 ----
    // 必须每季都推进，不能被任何提前 return 跳过，否则"连续破产 8 季"
    // 会因为其他分支的短路而永远数不满。
    // 判定条件是**深度**为负（-50,000 cr 以下），正常的季度波动不会触发。
    {
        constexpr i64 kBankruptFloor = -50000;
        const Empire* pl = st.empire(kPlayerId);
        if (pl != nullptr && pl->treasury.rawValue() < Fixed(kBankruptFloor).rawValue())
            ++progress.consecutiveBankrupt;
        else
            progress.consecutiveBankrupt = 0;
    }

    // ---- 胜利判定优先于失败判定 ----
    // 满足治理条件就是胜利：一个已经达成全部治理目标的帝国，
    // 即便在领土上被蚕食殆尽，也已经赢下了这一局（测试
    // `mature_peaceful_economy_can_sustain_harder_governance` 明确要求这一点）。
    // 反过来的优先级会让"最后一季刚好同时满足胜利与失败"永远判负。
    VictoryStatus v = checkVictory(st);
    const u32 previous = progress.consecutiveQuarters;
    if (!consecutive) progress.consecutiveQuarters = 0;
    if (v.currentCriteriaMet) {
        ++progress.consecutiveQuarters;
        if (progress.consecutiveQuarters == 1)
            st.logEvent(LogPhase::Plot, "victory.started",
                        "征服与治理条件达标，开始连续 " + std::to_string(v.rules.requiredQuarters) +
                            " 季的治理考验", kPlayerId);
        if (progress.consecutiveQuarters >= v.rules.requiredQuarters) {
            progress.achieved = true;
            st.endingId = 12;
            st.ended = !st.endless;
            st.logEvent(LogPhase::Plot, kLogEnding,
                        "【征服胜利】所有对手均已被击败，民心、派系、财政、物资储备与国内秩序连续 " +
                            std::to_string(v.rules.requiredQuarters) + " 季达标", kPlayerId);
            return;
        }
    } else {
        progress.consecutiveQuarters = 0;
        if (previous > 0)
            st.logEvent(LogPhase::Plot, "victory.interrupted",
                        "治理考验中断，连续季数归零：" + v.unmet.front(), kPlayerId);
    }

    // ---- 失败判定 ----
    // 放在最后：只有在没有达成胜利时才可能判负。
    const DefeatStatus d = checkDefeat(st);
    if (d.defeated) {
        st.ended = !st.endless;
        // 只在**首次**判负时记日志。旧写法每 tick 都会写一条
        // 「【败亡】…」，日志被同一条消息刷屏（实测连续 4 季重复）。
        if (!st.defeated) {
            st.logEvent(LogPhase::Plot, kLogEnding, "【败亡】" + d.reason, kPlayerId);
        }
        st.defeated = true;
        st.defeatReason = d.reason;
    }
}

DefeatStatus checkDefeat(const GameState& st) {
    DefeatStatus d;
    const Empire* player = st.empire(kPlayerId);
    if (player == nullptr) {
        d.kind = DefeatKind::Conquered;
        d.defeated = true;
        d.reason = "你的帝国已不复存在。";
        return d;
    }
    if (st.defeated) {
        // 已经判负过：保持结论（--endless 可继续观望，但状态不再翻转）
        d.kind = DefeatKind::Conquered;
        d.defeated = true;
        d.reason = st.defeatReason.empty() ? "你的帝国已覆灭。" : st.defeatReason;
        return d;
    }
    if (!player->alive) {
        d.kind = DefeatKind::Conquered;
        d.defeated = true;
        d.reason = "你的帝国已覆灭。";
        return d;
    }
    // 领土归零 = 被征服。这是旧版本最严重的行为缺口：
    // 玩家在 154 季后 0 星系 / 0 行星，游戏却继续正常运行、不判负、不结束。
    const bool hasSystem = std::any_of(st.map.systems.begin(), st.map.systems.end(),
                                      [](const SystemNode& s) { return s.owner == kPlayerId; });
    const bool hasPlanet = std::any_of(st.planets.begin(), st.planets.end(),
                                       [](const Planet& p) { return p.owner == kPlayerId; });
    if (!hasSystem && !hasPlanet) {
        d.kind = DefeatKind::Conquered;
        d.defeated = true;
        d.reason = "你失去了最后一个星系与行星，帝国被彻底征服。";
        return d;
    }
    // 连续破产：国库深度为负且持续 8 季。给足缓冲，避免正常波动误判。
    //
    // 阈值必须与**收入规模**挂钩：一个季度净收入只有 200 cr 的小国，
    // 欠 5 万已经是 250 季的收入，等同于永久无法翻身；
    // 而对大国来说 5 万只是周转波动。固定阈值对两者都不公平。
    constexpr u32 kBankruptQuarters = 8;
    const Fixed floorByIncome = Fixed(20000) + fxMax(player->lastIncome, Fixed(0)) * Fixed(15);
    const Fixed bankruptFloor = fxMax(Fixed(50000), floorByIncome);
    if (player->treasury.rawValue() < (-bankruptFloor).rawValue() &&
        st.victory.consecutiveBankrupt + 1 >= kBankruptQuarters) {
        d.kind = DefeatKind::Bankrupt;
        d.defeated = true;
        d.reason = "连续 " + std::to_string(kBankruptQuarters) + " 季国库低于 -" +
                   fixedStrPlain(bankruptFloor, 0) + " cr，财政崩溃导致帝国解体。";
        return d;
    }
    if (player->treasury.rawValue() < (-bankruptFloor).rawValue()) d.kind = DefeatKind::Bankrupt;
    return d;
}

std::string defeatText(const GameState& st) {
    DefeatStatus d = checkDefeat(st);
    if (!d.defeated && d.kind == DefeatKind::None) return {};
    std::string out = style("═══ 败亡 ═══", Style::Heading) + "\n";
    out += "  " + (st.defeatReason.empty() ? d.reason : st.defeatReason) + "\n";
    if (!st.endless)
        out += "  纪元在此终结。用 `greyfall new` 开启新纪元，"
               "或 `greyfall epoch --next` 以遗产续行。\n";
    else
        out += "  （无尽模式：你可以继续观望这个世界的走向。）\n";
    return out;
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
