#include "domain/Treaty.h"

#include "core/GameState.h"

#include <algorithm>

namespace gf {

std::string_view treatyKindName(TreatyKind k) {
    switch (k) {
        case TreatyKind::TradePact: return "贸易协定";
        case TreatyKind::NonAggression: return "互不侵犯";
        case TreatyKind::DefensivePact: return "防御同盟";
        case TreatyKind::ResearchPact: return "研究协定";
        case TreatyKind::JointIntel: return "联合情报";
        case TreatyKind::Federation: return "联邦成员";
        case TreatyKind::Vassalage: return "宗藩关系";
        case TreatyKind::War: return "战争状态";
        case TreatyKind::Sanction: return "制裁";
        case TreatyKind::Embargo: return "封锁";
        case TreatyKind::Ceasefire: return "停火";
        case TreatyKind::TributeDemand: return "索贡";
        case TreatyKind::Count: break;
    }
    return "?";
}

TreatyKind treatyKindFromName(std::string_view s) {
    for (int i = 0; i < static_cast<int>(TreatyKind::Count); ++i)
        if (treatyKindName(static_cast<TreatyKind>(i)) == s) return static_cast<TreatyKind>(i);
    if (s == "propose-trade" || s == "trade") return TreatyKind::TradePact;
    if (s == "propose-pact" || s == "pact") return TreatyKind::NonAggression;
    if (s == "breach") return TreatyKind::War;
    if (s == "sanction") return TreatyKind::Sanction;
    if (s == "declare-war") return TreatyKind::War;
    if (s == "vassalize") return TreatyKind::Vassalage;
    if (s == "federate-join") return TreatyKind::Federation;
    return TreatyKind::TradePact;
}

void upsertTreaty(std::vector<Treaty>& list, const Treaty& t) {
    for (auto& existing : list) {
        if (existing.kind == t.kind && existing.a == t.a && existing.b == t.b) {
            existing = t;
            return;
        }
    }
    list.push_back(t);
}

void removeTreaty(std::vector<Treaty>& list, TreatyKind kind, u32 a, u32 b) {
    list.erase(std::remove_if(list.begin(), list.end(),
                              [kind, a, b](const Treaty& t) {
                                  return t.kind == kind && ((t.a == a && t.b == b) || (t.a == b && t.b == a));
                              }),
               list.end());
}

void declareWar(GameState& st, u32 a, u32 b, bool atWar) {
    if (a == b) return;
    Relation& rab = st.relation(a, b);
    Relation& rba = st.relation(b, a);
    rab.atWar = atWar;
    rba.atWar = atWar;
    if (atWar) {
        rab.warScore = 0;
        rba.warScore = 0;
    } else {
        rab.warScore = 0;
        rba.warScore = 0;
    }
}

bool atWarWith(const GameState& st, u32 a, u32 b) {
    return st.relation(a, b).atWar || st.relation(b, a).atWar;
}

bool atWarWithAnyone(const GameState& st, u32 a) {
    for (const auto& e : st.empires) {
        if (e.id == a) continue;
        if (atWarWith(st, a, e.id)) return true;
    }
    return false;
}

bool hasTreaty(const std::vector<Treaty>& list, TreatyKind kind, u32 a, u32 b) {
    for (const auto& t : list) {
        if (t.kind != kind) continue;
        if ((t.a == a && t.b == b) || (t.a == b && t.b == a)) return true;
    }
    return false;
}

}  // namespace gf
