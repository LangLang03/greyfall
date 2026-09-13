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
    const bool wasAtWar = rab.atWar || rba.atWar;
    rab.atWar = atWar;
    rba.atWar = atWar;
    rab.warScore = 0;
    rba.warScore = 0;
    if (atWar) {
        // 只在「新开战」时记录起始 tick。
        //
        // 重复宣战**不得**重置计时器 —— AI 会在战争期间反复发出宣战动作
        //（declareWar 是幂等的，调用方并不总是先检查 atWarWith），
        // 旧写法 `!wasAtWar || warStartTick == 0` 里的第二个条件是多余的，
        // 它让每季重复宣战都能刷新 warStartTick，于是
        // Peace.cpp 的「战争疲劳上限」永远不触发，战争重新变成无限期。
        // 实测该缺陷会让玩家在 200 季后仍被 2 场战争拖着、领土归零。
        if (!wasAtWar) {
            // 存 tick+1，因为 0 是「未处于战争」的哨兵值。
            // 世界生成阶段就会爆发战争（AI 之间或对玩家），而那时 st.tick == 0 ——
            // 直接存 st.tick 会让 warStartTick 与哨兵撞车，
            // 于是 Peace.cpp 的「战争疲劳上限」永不生效，战争重回无限期。
            rab.warStartTick = st.tick + 1;
            rba.warStartTick = st.tick + 1;
        }
    } else {
        rab.warStartTick = 0;
        rba.warStartTick = 0;
        rab.lastWar = st.tick;
        rba.lastWar = st.tick;
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
