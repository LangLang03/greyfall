#include "ai/PowerBalancing.h"

#include <algorithm>

#include "ai/BetrayalCalculus.h"
#include "ai/Reputation.h"
#include "domain/Federation.h"
#include "domain/Treaty.h"
#include "gen/EmpireGen.h"
#include "util/Str.h"

namespace gf {

Fixed powerIndexRank(const GameState& st, u32 actor) {
    if (st.empires.size() <= 1) return Fixed::pct(50);
    const Empire* me = st.empire(actor);
    if (me == nullptr) return Fixed::pct(50);
    Fixed my = me->powerIndex();
    i64 below = 0;
    i64 total = 0;
    for (const auto& e : st.empires) {
        if (!e.alive || e.id == actor) continue;
        ++total;
        if (e.powerIndex().rawValue() < my.rawValue()) ++below;
    }
    if (total == 0) return Fixed::pct(50);
    return Fixed::raw(mulDivSat(below, FIX, total));
}

Coalition evaluateCoalition(const GameState& st, u32 target) {
    Coalition c;
    c.target = target;
    Fixed rank = powerIndexRank(st, target);
    c.threshold = Fixed::pct(55);
    if (rank.rawValue() <= c.threshold.rawValue()) {
        c.reason = "目标国力分位 " + fixedStrPlain(rank, 2) + " ≤ 0.55，无需制衡";
        return c;
    }
    // 找出所有"受到威胁"的主体
    const Empire* t = st.empire(target);
    if (t == nullptr) return c;
    u32 leader = 0xFFFFFFFFu;
    Fixed bestFear = Fixed(0);
    for (const auto& e : st.empires) {
        if (e.id == target || !e.alive) continue;
        Fixed fear = e.mind.threat[target] + st.relation(e.id, target).fear;
        if (fear.rawValue() > bestFear.rawValue()) {
            bestFear = fear;
            leader = e.id;
        }
    }
    if (leader == 0xFFFFFFFFu) {
        // 默认由国力第二强者牵头
        Fixed second = Fixed(0);
        for (const auto& e : st.empires) {
            if (e.id == target || !e.alive) continue;
            if (e.powerIndex().rawValue() > second.rawValue()) {
                second = e.powerIndex();
                leader = e.id;
            }
        }
    }
    if (leader == 0xFFFFFFFFu) return c;
    c.leader = leader;
    c.members.push_back(leader);
    Fixed coalitionPower = st.empires[leader].powerIndex();
    for (const auto& e : st.empires) {
        if (e.id == target || e.id == leader || !e.alive) continue;
        // 与自己观感为负、或与目标的观感为正者更愿加入
        Fixed willingness = Fixed::pct(50) - st.relation(e.id, target).opinion * Fixed::pct(30) +
                            (e.federation == st.empires[leader].federation ? Fixed::pct(25) : Fixed(0));
        if (willingness.rawValue() < Fixed::pct(35).rawValue()) continue;
        c.members.push_back(e.id);
        coalitionPower += e.powerIndex();
    }
    c.strength = coalitionPower / (t->powerIndex() + Fixed(1));
    // 联盟实力达到霸权者的 60% 即构成有效反制
    c.formed = c.strength.rawValue() > Fixed::pct(60).rawValue();
    c.reason = "玩家国力分位 " + fixedStrPlain(rank, 2) + " > 0.55 ⇒ 联盟实力比 " +
               fixedStrPlain(c.strength, 2) + (c.formed ? "（已形成遏制）" : "（尚未形成）");
    return c;
}

void powerBalancingPhase(GameState& st) {
    Coalition c = evaluateCoalition(st, kPlayerId);
    // 国力分位未超阈值（或找不到牵头者）⇒ 恐惧自然衰减
    if (c.members.empty() || c.reason.find("无需制衡") != std::string::npos) {
        for (auto& e : st.empires)
            if (!e.isPlayer) e.mind.threat[kPlayerId] = e.mind.threat[kPlayerId] * Fixed::pct(95);
        return;
    }
    // 只要玩家构成霸权（国力分位 > 0.55），所有非玩家主体都会抬高威胁感知 ——
    // 这就是威慑信号本身；只有联盟实力足够时才升级为军备与封锁。
    for (auto& e : st.empires) {
        if (e.isPlayer || !e.alive) continue;
        bool member = std::find(c.members.begin(), c.members.end(), e.id) != c.members.end();
        Fixed bump = member ? Fixed::pct(4) : Fixed::pct(2);
        e.mind.threat[kPlayerId] = fxClamp(e.mind.threat[kPlayerId] + bump, Fixed(0), Fixed(1));
        if (!c.formed) continue;
        e.military += e.military * Fixed::pct(3);   // 军备
        e.addOpinion(kPlayerId, Fixed::pct(-2));
        // 串联：联盟成员彼此之间观感上升
        for (u32 other : c.members) {
            if (other == e.id) continue;
            e.addOpinion(other, Fixed::pct(2));
        }
        // 威胁足够高时实施封锁（削减该主体对玩家的市场参与）
        if (e.mind.threat[kPlayerId].rawValue() > Fixed::pct(40).rawValue()) {
            Relation& rel = st.relation(e.id, kPlayerId);
            rel.embargo = true;
        }
    }
    st.logEvent(LogPhase::Ai, kLogBalance,
                std::string(c.formed ? "遏制联盟形成" : "遏制意图成形（尚未升级为联盟行动）") + "：牵头 " +
                    st.empires[c.leader].name + "，成员 " + std::to_string(c.members.size()) + " 国，实力比 " +
                    fixedStrPlain(c.strength, 2) + "；" + c.reason,
                c.leader, c.strength);
}

std::string coalitionReport(const GameState& st, u32 target) {
    Coalition c = evaluateCoalition(st, target);
    std::string out;
    out += "═══ 制衡评估 ═══\n";
    out += "目标：" + std::string(st.empire(target) ? st.empire(target)->name : "?") + "  国力分位 " +
           fixedStrPlain(powerIndexRank(st, target), 2) + "（阈值 0.55）\n";
    out += c.reason + "\n";
    if (!c.members.empty()) {
        out += "联盟成员：" + std::to_string(c.members.size()) + " 国\n";
        for (u32 m : c.members) {
            const Empire* e = st.empire(m);
            if (e == nullptr) continue;
            out += "  · " + e->name + "  国力 " + fixedStr(e->powerIndex(), 2) + "  对目标恐惧 " +
                   fixedStrPlain(e->mind.threat[target], 2) + "\n";
        }
    }
    return out;
}

}  // namespace gf
