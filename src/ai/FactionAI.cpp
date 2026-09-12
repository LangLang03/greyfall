#include "ai/FactionAI.h"

#include <algorithm>

#include "domain/Parliament.h"
#include "domain/Policy.h"
#include "gen/EmpireGen.h"
#include "util/Fmt.h"
#include "items/ItemDef.h"
#include "rng/Streams.h"
#include "util/Str.h"

namespace gf {

Fixed coupRiskOf(const GameState& st, u32 empire) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return Fixed(0);
    Fixed dissatisfaction = Fixed(0);
    Fixed influence = Fixed(0);
    for (const auto& f : e->domestic.factions) {
        Fixed d = Fixed(1) - f.satisfaction;
        dissatisfaction += d * f.influence;
        influence += f.influence;
    }
    if (influence.rawValue() <= 0) return Fixed(0);
    Fixed weighted = dissatisfaction / influence;
    // 民怨对政变风险的贡献取 35%：留出安全余量，使治理正常的帝国不会被
    // 外部渗透（patronRisk 最高 +30%）一推就过 65% 的政变线。
    Fixed risk = weighted * (Fixed(1) - e->domestic.legitimacy) + e->domestic.unrest * Fixed::pct(35);
    // 被外部资助的派系提高政变风险。
    // 注意量纲：patronFunding 以信用点计，必须除以一个与「信用点」同量级的分母，
    // 否则 (9000 raw) / Fixed(2000) 会算出 4.5 的风险值，直接把风险顶到 1.0。
    Fixed patronRisk = Fixed(0);
    for (const auto& f : e->domestic.factions) {
        if (f.patron == 0xFFFFFFFFu) continue;
        patronRisk += f.patronFunding / Fixed(200000);
    }
    risk += fxClamp(patronRisk, Fixed(0), Fixed::pct(30));
    return fxClamp(risk, Fixed(0), Fixed(1));
}

void domesticPhase(GameState& st) {
    factionAiPhase(st);
    for (auto& e : st.empires) {
        if (!e.alive) continue;
        Domestic& d = e.domestic;

        // 派系满意度：政策决定均衡值，处境修正微调，满意度向均衡**平滑收敛**。
        //
        // 早期实现用 `drift = +2% + 财富项` 做**线性累加**，而财富项在国库
        // 10 万时会贡献 +33%/季 —— 结果是所有派系满意度必然在数十季内涨到 100%，
        // 派系、议会与政变机制随之全部失效。均值回归才是正确模型。
        Fixed treasury = e.isPlayer ? st.market.margin.cash : e.treasury;
        // 财富加成：上限 +10%，避免国库规模直接决定满意度
        Fixed wealthBonus = fxClamp(treasury / Fixed(1000000), Fixed(0), Fixed::pct(10));
        // 繁荣度：由低民怨、高稳定与行星开发度决定，**对所有派系同等生效**。
        //
        // 关键设计：政策对派系是**零和**的（受益方 +N%，受损方 −N%），
        // 因此仅靠政策的平均满意度恒为 50%，胜利条件「满意度 ≥60%」永远不可达。
        // 繁荣度提供非零和的共同利益来源，让「把国家治理好」能整体抬升满意度。
        Fixed avgDev = Fixed(0);
        {
            int np = 0;
            for (u32 sys : e.systems) {
                const SystemNode* sn = st.system(sys);
                if (sn == nullptr) continue;
                for (u32 pid : sn->planets) {
                    const Planet* pl = st.planet(pid);
                    if (pl == nullptr || pl->owner != e.id) continue;
                    avgDev += pl->development;
                    ++np;
                }
            }
            if (np > 0) avgDev = avgDev / Fixed(np);
        }
        // 以「中庸国家」为 0 点：民怨 45%、稳定 45%、开发度 1.0 时繁荣度为 0
        Fixed prosperity = (Fixed(1) - d.unrest) * Fixed::pct(20) + e.stability * Fixed::pct(15) -
                           Fixed::pct(17) + fxClamp(avgDev / Fixed(40), Fixed(0), Fixed::pct(6));
        // 开发度红利：把行星建设起来能实质提升稳定度与合法性的上限
        //（形成「建设 → 稳定 → 繁荣 → 满意度」的成长循环）
        Fixed devBonus = fxClamp(avgDev / Fixed(50), Fixed(0), Fixed::pct(12));

        for (auto& f : d.factions) {
            Fixed target = policyFactionTarget(st, e.id, f.kind) + wealthBonus + prosperity - wearinessSatisfactionPenalty(st, e.id);
            Fixed drift = Fixed(0);
            // 军部因战争上升，商会因贸易上升
            switch (f.kind) {
                case FactionKind::Military: {
                    int wars = 0;
                    for (const auto& r : st.relations) {
                        std::size_t idx = static_cast<std::size_t>(&r - st.relations.data());
                        if (r.atWar && idx / kMaxEmpires == e.id) ++wars;
                    }
                    drift += Fixed::pct(3) * Fixed(wars);
                    break;
                }
                case FactionKind::Merchant:
                    drift += empireModifier(e, ModKind::TradeMargin) * Fixed::pct(20);
                    break;
                case FactionKind::Fundamentalist:
                    drift += empireModifier(e, ModKind::Stability) * Fixed::pct(10);
                    break;
                case FactionKind::Labor:
                    drift -= d.unrest * Fixed::pct(10);
                    break;
                case FactionKind::Syndicate:
                    drift += Fixed::pct(2) * Fixed(static_cast<i64>(st.market.manipulations.size() > 4 ? 1 : 0));
                    break;
                default:
                    break;
            }
            // 处境修正作用在**均衡值**上，而非无限累加
            target = fxClamp(target + drift, Fixed::pct(5), Fixed::pct(95));
            f.satisfaction = fxLerp(f.satisfaction, target, Fixed::pct(5));
            // 未满足的诉求累积压力
            if (f.satisfaction.rawValue() < Fixed::pct(40).rawValue()) {
                f.demandPressure += Fixed::pct(4);
            } else {
                f.demandPressure = f.demandPressure * Fixed::pct(90);
            }
            // 压力过高 → 提出诉求
            if (f.demandPressure.rawValue() > Fixed(1).rawValue() && st.tick >= f.lastDemandTick + 6) {
                f.lastDemandTick = static_cast<u32>(st.tick);
                f.lastDemand = factionDemandText(f.kind);
                st.logEvent(LogPhase::Domestic, kLogDomestic,
                            e.name + " 的【" + f.name + "】提出诉求：" + f.lastDemand +
                                "（影响力 " + fixedStrPlain(f.influence, 2) + "）",
                            e.id, f.demandPressure);
                if (e.isPlayer) {
                    PendingChoice c;
                    c.kind = ChoiceKind::Faction;
                    c.subject = static_cast<u8>(f.kind);
                    c.eventId = static_cast<u8>(55 + (static_cast<int>(f.kind) % 20));
                    c.scopeTarget = e.id;
                    c.createdTick = st.tick;
                    c.options = {"满足诉求（消耗资源，满意度 +25%）", "压制派系（民怨 +，满意度 -）",
                                 "拖延（压力继续累积）"};
                    c.hints = {"代价：(6000 + 影响力 × 8000) × (1 + 非首都星系数 × 10%) cr；同派系援助间隔 8 季", "代价：自由派反弹", "代价：后续代价更高"};
                    st.pending.push(c);
                }
            }
        }

        // 民怨与合法性。
        //
        // 两个量纲缺陷曾让民怨在 13 季内必然饱和到 100%：
        //   a) `empireModifier(Unrest)` 是**百分比修正**（8% = 0.08），
        //      却被当作每季绝对增量累加；
        //   b) `factionPressure` 多除了 100，使派系不满这一正当压力项形同虚设。
        // 正确做法：由派系压力与合法性算出**民怨目标值**，民怨向目标收敛。
        Fixed factionPressure = Fixed(0);
        Fixed totalInfluence = Fixed(0);
        for (const auto& f : d.factions) {
            factionPressure += (Fixed(1) - f.satisfaction) * f.influence;
            totalInfluence += f.influence;
        }
        if (totalInfluence.rawValue() > 0) factionPressure = factionPressure / totalInfluence;
        factionPressure = fxClamp(factionPressure, Fixed(0), Fixed(1));

        // 结构性民怨：由伦理冲突、财政赤字、物资短缺等**结构性**因素决定。
        //
        // 这些因素原先各自以「每季 +N%」的方式直接累加到民怨上，
        // 在长局中必然饱和（种族张力 3%/季 ⇒ 33 季即到 100%）。
        // 它们本质上描述的是一个**均衡水平**，因此必须作用在目标值上。
        Fixed structural = Fixed(0);
        {
            // 伦理对立：每对冲突伦理 +6% 结构性民怨
            const int pairs[][2] = {{1, 5}, {6, 5}, {9, 10}, {11, 0}, {11, 8}, {0, 7}};
            int opposedPairs = 0;
            for (std::size_t i = 0; i < e.ethics.size(); ++i) {
                for (std::size_t j = i + 1; j < e.ethics.size(); ++j) {
                    for (const auto& pr : pairs) {
                        if ((e.ethics[i] == pr[0] && e.ethics[j] == pr[1]) ||
                            (e.ethics[i] == pr[1] && e.ethics[j] == pr[0]))
                            ++opposedPairs;
                    }
                }
            }
            structural += Fixed::pct(6) * Fixed(opposedPairs);
            // 财政赤字：国库为负则民怨高企（按缺口比例，上限 +20%）
            Fixed cash = e.isPlayer ? st.market.margin.cash : e.treasury;
            if (cash.rawValue() < 0) {
                Fixed shortfall = fxClamp(-cash / Fixed(50000), Fixed(0), Fixed::pct(20));
                structural += shortfall;
            }
            // 物资短缺：每种短缺物资 +3%
            int shortages = 0;
            for (int c2 = 0; c2 < kCommodityCount; ++c2) {
                Fixed need = resourceDemand(st, e, c2);
                if (need.rawValue() <= 0) continue;
                if (e.stock[static_cast<std::size_t>(c2)].rawValue() < need.rawValue()) ++shortages;
            }
            // 每种短缺 +3%，但总影响封顶 +18%（6 种）：
            // 短缺是压力而非判决，否则长期短缺会让民怨单向顶到上限。
            structural += fxClamp(Fixed::pct(2) * Fixed(shortages), Fixed(0), Fixed::pct(8));
        }

        // 民怨目标 = 派系压力 + 合法性缺口 + 结构性民怨 + 政策/科技修正 + 已通过法案的影响
        Fixed unrestTarget = factionPressure * Fixed::pct(40) + (Fixed(1) - d.legitimacy) * Fixed::pct(20) +
                             structural + empireModifier(e, ModKind::Unrest) +
                             parliamentUnrestTarget(st, e.id);
        unrestTarget = fxClamp(unrestTarget, Fixed(0), Fixed(1));
        // 政体差异：选举制对民怨更敏感（选民会算账），
        // 威权制的怨恨则持续推高民怨基数
        Fixed govUnrest = Fixed(0);
        // 奴役制度推高民怨目标（蓄奴制 +20%）
        govUnrest += laborUnrestTarget(e.labor);
        if (isElective(e.government)) govUnrest += d.unrest * Fixed::pct(10);
        govUnrest += e.gov.resentment * Fixed::pct(20);
        d.unrest = fxLerp(d.unrest, unrestTarget + wearinessUnrestPenalty(st, e.id) + govUnrest,
                          Fixed::pct(12));
        // 合法性：由**合法性来源**决定，而不是所有政体共用一条公式。
        // 选举制怕民怨、威权制可镇压、绩效制看经济、信仰制不敏感 ——
        // 这是「民主制与独裁制玩法不同」的核心。
        Fixed legitTarget = legitimacyFromSource(st, e.id) + empireModifier(e, ModKind::Stability) +
                            devBonus;
        d.legitimacy = fxClamp(fxLerp(d.legitimacy, legitTarget, Fixed::pct(15)), Fixed(0), Fixed(1));
        // 稳定度：与民怨一样，应当收敛到一个**均衡值**，而不是每季单向累加。
        //
        // 早期实现是 `+ 稳定修正*5% − 派系压力*1%`，而压力项（~0.5×1%=0.005/季）
        // 恒大于正项（~0.09×0.5%=0.0005/季），且没有任何恢复机制 ——
        // 结果是**所有帝国的稳定度在 40 季内必然崩到 0 并永久钉死**，
        // 使胜利条件「稳定 ≥70%」根本不可能达成。
        // 战争疲劳：打到民怨沸腾时稳定度会被持续拉低（停战后自动消除）
        Fixed warPenalty = wearinessStabilityPenalty(st, e.id);
        // 游牧政体：疆域过大难以维持（舰队才是它的国土）
        warPenalty += nomadicStabilityPenalty(st, e.id);
        Fixed stabTarget = Fixed::pct(55) + governmentInfo(e.government).unrestBias +
                           empireModifier(e, ModKind::Stability) - d.unrest * Fixed::pct(45) -
                           structural * Fixed::pct(50) + devBonus + warPenalty;
        stabTarget = fxClamp(stabTarget, Fixed(0), Fixed(1));
        e.stability = fxLerp(e.stability, stabTarget, Fixed::pct(10));

        // 政变
        d.coupRisk = coupRiskOf(st, e.id);
        if (d.coupRisk.rawValue() > Fixed::pct(65).rawValue()) {
            if (d.coupCountdown == 0) {
                d.coupCountdown = 3;
                st.logEvent(LogPhase::Domestic, kLogCoup,
                            e.name + " 政变风险 " + fixedStrPlain(d.coupRisk, 2) + "：进入 3 季倒计时", e.id,
                            d.coupRisk);
            } else {
                --d.coupCountdown;
                if (d.coupCountdown == 0) {
                    // 政变后必须有冷却期，否则一次高风险会变成每 4 季一次的无限政变
                    if (st.tick < static_cast<u64>(d.lastCoupTick) + 40) {
                        d.coupCountdown = 0;
                        d.coupRisk = d.coupRisk * Fixed::pct(50);
                        continue;
                    }
                    d.lastCoupTick = static_cast<u32>(st.tick);
                    // 政变爆发
                    Fixed severity = d.coupRisk;
                    e.stability -= severity * Fixed::pct(40);
                    // 政变的国库损失加绝对上限：按比例扣减在国库很大时
                    // 会一次性造成巨额缺口，使新政权一上台就破产。
                    Fixed coupLoss = e.treasury * severity * Fixed::pct(25);
                    Fixed coupCap = fxMax(Fixed(8000), fxAbs(e.lastIncome) * Fixed(20));
                    if (coupLoss.rawValue() > coupCap.rawValue()) coupLoss = coupCap;
                    if (coupLoss.rawValue() < 0) coupLoss = Fixed(0);
                    e.treasury -= coupLoss;
                    if (e.isPlayer) st.market.margin.cash = e.treasury;
                    e.military -= e.military * severity * Fixed::pct(30);
                    // 政变的作用是**解决**危机，而不是加剧它。
                    //
                    // 早期实现把全体派系满意度重置到 30% 并进一步降低合法性，
                    // 这必然让政变风险立刻回到高位 —— 形成「政变 → 更不满 → 再政变」
                    // 的死亡螺旋（实测政变风险长期钉在 100%）。
                    // 新政权的上台应当带来政治秩序的重置：满意度回到中性，
                    // 合法性由新政权重新建立（保留一段时间的折损）。
                    for (auto& f : d.factions) {
                        // 参与夺权的派系受益，其余回到中性
                        f.satisfaction = (f.influence.rawValue() > Fixed::pct(15).rawValue())
                                             ? Fixed::pct(60)
                                             : Fixed::pct(50);
                    }
                    d.unrest = d.unrest * Fixed::pct(35);
                    d.legitimacy = governmentInfo(e.government).legitimacy * Fixed::pct(85);
                    st.logEvent(LogPhase::Domestic, kLogCoup,
                                "【政变】" + e.name + " 的派系联盟夺权：国库 -" +
                                    fixedStrPlain(severity * Fixed(25), 0) + "%，军力重创，稳定度骤降",
                                e.id, severity);
                    if (e.domestic.coupCountdown == 0) d.coupRisk = d.coupRisk * Fixed::pct(50);
                }
            }
        } else {
            d.coupCountdown = 0;
        }
    }
}

void factionAiPhase(GameState& st) {
    // AI 直接向玩家的国内派系输送资金/情报（代理人战）
    for (auto& e : st.empires) {
        if (e.isPlayer || !e.alive) continue;
        if (!st.rng.chance(RngStream::Ai, Fixed::pct(12))) continue;
        Empire& p = st.empires[kPlayerId];
        if (p.domestic.factions.empty()) continue;
        // 选择满意度最低、影响力最高的派系扶植
        std::size_t best = 0;
        Fixed bestScore = Fixed(-1);
        for (std::size_t i = 0; i < p.domestic.factions.size(); ++i) {
            Faction& f = p.domestic.factions[i];
            Fixed score = f.influence * (Fixed(1) - f.satisfaction);
            if (score.rawValue() > bestScore.rawValue()) {
                bestScore = score;
                best = i;
            }
        }
        Faction& target = p.domestic.factions[best];
        Fixed funding = Fixed(static_cast<i64>(st.rng.range(RngStream::Ai, 2000, 9000)));
        // 资助上限与自身收入挂钩：代理人战是长期投入，
        // 若无上限会持续抽干国库（实测该阶段每季流失约 1,955/国，
        // 而其净收入仅约 1,300/季 —— 支出超过收入）。
        Fixed cap = fxMax(e.lastIncome * Fixed::pct(20), Fixed(500));
        if (funding.rawValue() > cap.rawValue()) funding = cap;
        if (e.treasury.rawValue() < funding.rawValue()) continue;
        e.treasury -= funding;
        target.patron = e.id;
        target.patronFunding += funding;
        target.influence += Fixed::pct(3);
        target.satisfaction -= Fixed::pct(4);
        st.logEvent(LogPhase::Domestic, "domestic.patron",
                    e.name + " 向你的【" + target.name + "】输送 " + fixedStr(funding, 0) +
                        " cr 与情报（代理人战：影响力 +3%，满意度 -4%）",
                    e.id, funding);
    }
}

bool satisfyFaction(GameState& st, u32 empire, FactionKind kind, std::string* err) {
    Empire* e = st.empire(empire);
    if (e == nullptr) {
        if (err) *err = "非法主体";
        return false;
    }
    for (auto& f : e->domestic.factions) {
        if (f.kind != kind) continue;
        if (st.tick < f.nextSatisfyTick) {
            if (err) *err = "该派系援助仍在落实，需再等待 " + std::to_string(f.nextSatisfyTick - st.tick) + " 季";
            return false;
        }
        Fixed cost = (Fixed(6000) + f.influence * Fixed(8000)) *
                     (Fixed(1) + Fixed::pct(10) * Fixed(colonialCapacity(st, empire).owned));
        Fixed available = e->isPlayer ? st.market.margin.cash : e->treasury;
        if (available.rawValue() < cost.rawValue()) {
            if (err) *err = "国库不足以满足诉求（需要 " + fixedStr(cost, 0) + "）";
            return false;
        }
        e->treasury -= cost;
        if (e->isPlayer) st.market.margin.cash = e->treasury;
        f.satisfaction = fxClamp(f.satisfaction + Fixed::pct(25), Fixed(0), Fixed(1));
        f.demandPressure = Fixed(0);
        f.nextSatisfyTick = st.tick + 8;
        st.logEvent(LogPhase::Domestic, "domestic.satisfy",
                    "满足了【" + f.name + "】的诉求：" + f.lastDemand + "（花费 " + fixedStr(cost, 0) + "）",
                    empire, cost);
        return true;
    }
    if (err) *err = "找不到该派系";
    return false;
}

bool suppressFaction(GameState& st, u32 empire, FactionKind kind, std::string* err) {
    Empire* e = st.empire(empire);
    if (e == nullptr) {
        if (err) *err = "非法主体";
        return false;
    }
    for (auto& f : e->domestic.factions) {
        if (f.kind != kind) continue;
        f.satisfaction = fxClamp(f.satisfaction - Fixed::pct(20), Fixed(0), Fixed(1));
        f.influence = fxClamp(f.influence - Fixed::pct(15), Fixed(0), Fixed(1));
        f.demandPressure = Fixed(0);
        e->domestic.repression += Fixed::pct(10);
        e->domestic.unrest = fxClamp(e->domestic.unrest + Fixed::pct(8), Fixed(0), Fixed(1));
        e->domestic.legitimacy = fxClamp(e->domestic.legitimacy - Fixed::pct(5), Fixed(0), Fixed(1));
        // 其他派系唇亡齿寒
        for (auto& other : e->domestic.factions) {
            if (other.kind == kind) continue;
            other.satisfaction = fxClamp(other.satisfaction - Fixed::pct(5), Fixed(0), Fixed(1));
        }
        st.logEvent(LogPhase::Domestic, "domestic.suppress", "压制了【" + f.name + "】：民怨 +8%，合法性 -5%",
                    empire);
        return true;
    }
    if (err) *err = "找不到该派系";
    return false;
}

std::string domesticReport(const GameState& st, u32 empire) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return "非法主体";
    std::string out;
    out += "═══ 国内派系（双层博弈） ═══\n";
    out += "民怨 " + bar(e->domestic.unrest, 20) + " " + fixedStrPlain(e->domestic.unrest * Fixed(100), 1) +
           "%    合法性 " + bar(e->domestic.legitimacy, 20) + " " +
           fixedStrPlain(e->domestic.legitimacy * Fixed(100), 1) + "%\n";
    out += "政变风险 " + bar(e->domestic.coupRisk, 20) + " " + fixedStrPlain(e->domestic.coupRisk * Fixed(100), 1) +
           "%";
    if (e->domestic.coupCountdown > 0) out += "  ⚠ 倒计时 " + std::to_string(e->domestic.coupCountdown) + " 季";
    out += "\n\n";
    out += "派系                  影响力   满意度   压力   诉求\n";
    for (const auto& f : e->domestic.factions) {
        std::string line = "  " + padRight(f.name, 18) + padLeft(fixedStrPlain(f.influence, 2), 6) + "  " +
                           padLeft(fixedStrPlain(f.satisfaction, 2), 6) + "  " +
                           padLeft(fixedStrPlain(f.demandPressure, 2), 6) + "  ";
        line += f.lastDemand.empty() ? "—" : f.lastDemand;
        if (st.tick < f.nextSatisfyTick) line += "（再次援助需等 " + std::to_string(f.nextSatisfyTick - st.tick) + " 季）";
        if (f.patron != 0xFFFFFFFFu) {
            const Empire* p = st.empire(f.patron);
            line += "  【被 " + std::string(p ? p->name : "?") + " 资助】";
        }
        out += line + "\n";
    }
    return out;
}

}  // namespace gf
