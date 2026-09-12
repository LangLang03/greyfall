#include "ai/FederationVote.h"

#include <algorithm>

#include "ai/PowerBalancing.h"
#include "domain/Treaty.h"
#include "gen/EmpireGen.h"
#include "rng/Streams.h"
#include "util/Str.h"

namespace gf {

Fixed federationVoteWeight(Fixed power, Fixed contribution, Fixed concession) {
    return power * Fixed(1) + contribution * Fixed::pct(50) + concession * Fixed(2);
}

Fixed voteStance(const GameState& st, u32 voter, const FederalMotion& motion) {
    const Empire* v = st.empire(voter);
    if (v == nullptr) return Fixed(0);
    switch (motion.subject) {
        case VoteSubject::AdmitMember: {
            const Empire* t = st.empire(motion.target);
            if (t == nullptr) return Fixed(0);
            Fixed stance = st.relation(voter, motion.target).opinion;
            // 若新成员过强，担心削弱自身话语权
            if (t->powerIndex().rawValue() > v->powerIndex().rawValue()) stance -= Fixed::pct(20);
            return fxClamp(stance, Fixed(-1), Fixed(1));
        }
        case VoteSubject::ExpelMember: {
            const Empire* t = st.empire(motion.target);
            if (t == nullptr) return Fixed(0);
            return fxClamp(-st.relation(voter, motion.target).opinion, Fixed(-1), Fixed(1));
        }
        case VoteSubject::CommonFleet:
            // 军费分摊：富裕者反对
            return fxClamp(Fixed::pct(30) - v->treasury / Fixed(6000), Fixed(-1), Fixed(1));
        case VoteSubject::TaxHarmonize:
            return fxClamp(Fixed::pct(20) - empireModifier(*v, ModKind::TradeMargin), Fixed(-1), Fixed(1));
        case VoteSubject::SanctionMember: {
            const Empire* t = st.empire(motion.target);
            if (t == nullptr) return Fixed(0);
            return fxClamp(-st.relation(voter, motion.target).opinion - Fixed::pct(20), Fixed(-1), Fixed(1));
        }
        case VoteSubject::WarDeclaration: {
            const Empire* t = st.empire(motion.target);
            if (t == nullptr) return Fixed(0);
            Fixed stance = -st.relation(voter, motion.target).opinion + empireModifier(*v, ModKind::MilitaryPower);
            // 距离越远越不支持
            int hops = st.map.hops(v->capital, t->capital);
            stance -= Fixed::pct(4) * Fixed(hops < 0 ? 10 : hops);
            return fxClamp(stance, Fixed(-1), Fixed(1));
        }
        case VoteSubject::LimitManipulation:
            // 操纵记录多者反对
            {
                int recs = 0;
                for (const auto& r : st.market.manipulations)
                    if (r.actor == voter) ++recs;
                return fxClamp(Fixed::pct(20) - Fixed(recs) * Fixed::pct(15), Fixed(-1), Fixed(1));
            }
        case VoteSubject::CrisisResponse:
            return fxClamp(Fixed::pct(40) - v->treasury / Fixed(8000), Fixed(-1), Fixed(1));
        case VoteSubject::Reform:
            return fxClamp(Fixed::pct(10) + empireModifier(*v, ModKind::ResearchRate), Fixed(-1), Fixed(1));
        case VoteSubject::Count:
            break;
    }
    return Fixed(0);
}

bool resolveMotion(GameState& st, u32 federationId, FederalMotion& motion) {
    if (federationId >= st.federations.size()) return false;
    Federation& fed = st.federations[federationId];
    Fixed yes = Fixed(0);
    Fixed no = Fixed(0);
    Fixed total = Fixed(0);
    for (u32 m : fed.members) {
        const Empire* e = st.empire(m);
        if (e == nullptr || !e->alive) continue;
        Fixed contribution = e->influence / Fixed(100);
        Fixed weight = federationVoteWeight(e->powerIndex(), contribution, fed.concession);
        total += weight;
        // logrolling：让利承诺把立场推向赞成
        Fixed stance = voteStance(st, m, motion) + fed.concession;
        if (stance.rawValue() > Fixed::pct(10).rawValue()) {
            yes += weight;
            motion.yes.push_back(m);
        } else if (stance.rawValue() < -Fixed::pct(10).rawValue()) {
            no += weight;
            motion.no.push_back(m);
        } else {
            motion.abstain.push_back(m);
            // 弃权按半数计入
            yes += weight / Fixed(2);
            total -= weight / Fixed(2);
        }
    }
    motion.yesWeight = yes;
    motion.noWeight = no;
    Fixed base = yes + no;
    Fixed ratio = base.rawValue() > 0 ? Fixed::raw(mulDivSat(yes.rawValue(), FIX, base.rawValue())) : Fixed(0);
    motion.passed = ratio.rawValue() >= motion.threshold.rawValue();
    motion.resolved = true;
    (void)total;

    st.logEvent(LogPhase::Federation, "fed.vote",
                "动议 #" + std::to_string(motion.id) + "（" + std::to_string(static_cast<int>(motion.subject)) +
                    "）表决：" + (motion.passed ? "通过" : "否决") + "，赞成权重 " + fixedStrPlain(ratio * Fixed(100), 1) +
                    "%（阈值 " + fixedStrPlain(motion.threshold * Fixed(100), 0) + "%）");

    if (motion.passed) {
        switch (motion.subject) {
            case VoteSubject::AdmitMember: {
                Empire* t = st.empire(motion.target);
                if (t != nullptr) {
                    t->federation = federationId;
                    fed.members.push_back(motion.target);
                }
                break;
            }
            case VoteSubject::ExpelMember: {
                Empire* t = st.empire(motion.target);
                if (t != nullptr) {
                    t->federation = 0xFFFFFFFFu;
                    fed.members.erase(std::remove(fed.members.begin(), fed.members.end(), motion.target),
                                      fed.members.end());
                }
                break;
            }
            case VoteSubject::WarDeclaration: {
                declareWar(st, fed.founder, motion.target, true);
                st.empires[fed.founder].lastWarTick = static_cast<u32>(st.tick);
                break;
            }
            case VoteSubject::TaxHarmonize:
                fed.treasury += Fixed(2000);
                break;
            default:
                break;
        }
    }
    return motion.passed;
}

FederalMotion proposeMotion(GameState& st, u32 federationId, VoteSubject subject, u32 target, u32 proposer) {
    FederalMotion m;
    // 动议 id 由状态推导，避免全局计数器破坏可复现性
    m.id = static_cast<u32>(st.tick * 16 + federationId * 4 + (proposer % 4) + 1);
    m.subject = subject;
    m.target = target;
    m.proposer = proposer;
    m.proposedTick = st.tick;
    m.threshold = (subject == VoteSubject::ExpelMember || subject == VoteSubject::WarDeclaration)
                      ? Fixed::pct(66)
                      : Fixed::pct(50);
    if (federationId < st.federations.size()) st.federations[federationId].motions.push_back(m);
    return m;
}

bool playerVote(GameState& st, u32 motionId, bool yes, std::string* err) {
    for (auto& fed : st.federations) {
        for (auto& m : fed.motions) {
            if (m.id != motionId) continue;
            if (m.resolved) {
                if (err) *err = "该动议已表决完毕";
                return false;
            }
            if (yes) {
                m.yes.push_back(kPlayerId);
                m.yesWeight += st.empires[kPlayerId].powerIndex();
            } else {
                m.no.push_back(kPlayerId);
                m.noWeight += st.empires[kPlayerId].powerIndex();
            }
            st.logEvent(LogPhase::Federation, "fed.player.vote",
                        "你在动议 #" + std::to_string(motionId) + " 上投了" + (yes ? "赞成" : "反对"), kPlayerId);
            return true;
        }
    }
    if (err) *err = "找不到该动议";
    return false;
}

bool offerConcession(GameState& st, u32 federationId, u32 toMember, Fixed concession, std::string* err) {
    if (federationId >= st.federations.size()) {
        if (err) *err = "非法联邦";
        return false;
    }
    Empire* me = st.empire(kPlayerId);
    if (me == nullptr) return false;
    Fixed cost = concession * Fixed(2000);
    if (me->treasury.rawValue() < cost.rawValue()) {
        if (err) *err = "国库不足以支撑该让利（需要 " + fixedStr(cost, 0) + "）";
        return false;
    }
    me->treasury -= cost;
    st.market.margin.cash = me->treasury;
    st.federations[federationId].concession += concession;
    Empire* other = st.empire(toMember);
    if (other != nullptr) other->treasury += cost;
    st.logEvent(LogPhase::Federation, "fed.logrolling",
                "向 " + std::string(other ? other->name : "?") + " 让利 " + fixedStr(cost, 0) + " cr 以换取投票支持",
                kPlayerId, cost);
    return true;
}

void federationPhase(GameState& st) {
    for (std::size_t f = 0; f < st.federations.size(); ++f) {
        Federation& fed = st.federations[f];
        if (fed.members.size() < 2) continue;
        // 凝聚力随成员实力差异下降
        Fixed maxP = Fixed(0), minP = Fixed(1000000);
        for (u32 m : fed.members) {
            const Empire* e = st.empire(m);
            if (e == nullptr) continue;
            maxP = fxMax(maxP, e->powerIndex());
            minP = fxMin(minP, e->powerIndex());
        }
        if (minP.rawValue() > 0 && maxP.rawValue() > 0) {
            Fixed disparity = Fixed(1) - minP / maxP;
            fed.cohesion = fxClamp(fed.cohesion - disparity * Fixed::pct(3), Fixed(0), Fixed(1));
        }
        // 每隔 6 tick 自动产生一个动议
        if (st.tick % 6 == 5) {
            VoteSubject subjects[] = {VoteSubject::CommonFleet, VoteSubject::TaxHarmonize, VoteSubject::Reform,
                                       VoteSubject::LimitManipulation, VoteSubject::CrisisResponse};
            u32 proposer = fed.members[st.rng.pick(RngStream::Diplo, fed.members.size())];
            u32 target = 0xFFFFFFFFu;
            VoteSubject subj = subjects[st.rng.pick(RngStream::Diplo, 5)];
            if (subj == VoteSubject::LimitManipulation) target = kPlayerId;
            if (subj == VoteSubject::CrisisResponse && !st.crises.empty()) {
                target = st.crises[st.rng.pick(RngStream::Diplo, st.crises.size())].id;
            }
            FederalMotion m = proposeMotion(st, static_cast<u32>(f), subj, target, proposer);
            (void)resolveMotion(st, static_cast<u32>(f), m);
            fed.motions.back() = m;
        }
        // 保留最近 24 条动议
        if (fed.motions.size() > 24) fed.motions.erase(fed.motions.begin(), fed.motions.begin() + 8);
    }
}

}  // namespace gf
