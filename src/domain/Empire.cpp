#include "domain/Empire.h"

#include "domain/Federation.h"
#include "core/GameState.h"
#include "domain/Building.h"
#include "gen/EmpireGen.h"

namespace gf {

void refreshEmpireBonuses(GameState& st) {
    for (auto& e : st.empires) {
        refreshColonizing(e);
        e.tradeBonus = Fixed(0);
        e.storageBonus = Fixed(0);
        e.clueBonus = Fixed(0);
        e.tech.rateBonus = empireModifier(e, ModKind::ResearchRate);
    }
    for (const auto& p : st.planets) {
        Empire* e = st.empire(p.owner);
        if (e == nullptr || !e->alive) continue;
        for (u32 bid : p.buildings) {
            const BuildingInfo& bi = buildingInfo(static_cast<int>(bid & 0xFFu));
            switch (bi.effect) {
                case BuildingEffect::Trading: e->tradeBonus += bi.effectValue; break;
                case BuildingEffect::Storage: e->storageBonus += bi.effectValue; break;
                case BuildingEffect::ClueDiscovery: e->clueBonus += bi.effectValue; break;
                case BuildingEffect::ProdResearch: e->tech.rateBonus += bi.effectValue / Fixed(200); break;
                default: break;
            }
        }
    }
    for (const auto& sys : st.map.systems) {
        auto* e = st.empire(sys.owner);
        if (!e || !e->alive || !sys.megastructure || sys.megastructureId >= kMegastructureCount) continue;
        const auto& info = megastructureInfo(sys.megastructureId);
        if (info.effect == BuildingEffect::ProdResearch) e->tech.rateBonus += info.effectValue / Fixed(200);
        if (info.effect == BuildingEffect::ClueDiscovery) e->clueBonus += info.effectValue / Fixed(100);
    }
    for (auto& e : st.empires) {
        e.tradeBonus = fxClamp(e.tradeBonus, Fixed(0), Fixed(2));
        e.clueBonus = fxClamp(e.clueBonus, Fixed(0), Fixed(2));
    }
}

Fixed Empire::powerIndex() const {
    // 国力 = 0.35·军事 + 0.30·经济 + 0.20·科技 + 0.15·凝聚力
    Fixed t = tech.progress[0] + tech.progress[1] + tech.progress[2] + tech.progress[3] + tech.progress[4] +
              tech.progress[5];
    Fixed techScore = Fixed::raw(t.rawValue() / 60);
    Fixed sc = Fixed::raw(static_cast<i64>(tech.completed.size()) * FIX / 24);
    Fixed techTotal = (techScore + sc) / Fixed(2);
    return military * Fixed::bp(3500) + economy * Fixed::bp(3000) + techTotal * Fixed::bp(2000) +
           unity * Fixed::bp(1500);
}

Fixed Empire::effectiveCapacity(Commodity c) const {
    return capacity[static_cast<std::size_t>(c)];
}

Fixed Empire::availableStock(Commodity c) const { return stock[static_cast<std::size_t>(c)]; }

std::string_view stanceName(EmpireStance s) {
    switch (s) {
        case EmpireStance::Expansionist: return "扩张主义";
        case EmpireStance::Defensive: return "防御主义";
        case EmpireStance::Mercantile: return "重商主义";
        case EmpireStance::Scholarly: return "求知主义";
        case EmpireStance::Zealot: return "狂信主义";
        case EmpireStance::Isolationist: return "孤立主义";
        case EmpireStance::Opportunist: return "机会主义";
        case EmpireStance::Count: break;
    }
    return "?";
}

EmpireStance stanceFromName(std::string_view s) {
    for (int i = 0; i < static_cast<int>(EmpireStance::Count); ++i)
        if (stanceName(static_cast<EmpireStance>(i)) == s) return static_cast<EmpireStance>(i);
    return EmpireStance::Mercantile;
}

std::string_view goalDimName(GoalDim d) {
    switch (d) {
        case GoalDim::Territory: return "领土";
        case GoalDim::Wealth: return "财富";
        case GoalDim::Science: return "科技";
        case GoalDim::Military: return "军事";
        case GoalDim::Prestige: return "声望";
        case GoalDim::Knowledge: return "知识";
        case GoalDim::Stability: return "稳定";
        case GoalDim::Faith: return "信仰";
        case GoalDim::Count: break;
    }
    return "?";
}

std::string_view actorTypeName(ActorType t) {
    switch (t) {
        case ActorType::Cooperate: return "合作型";
        case ActorType::Exploit: return "剥削型";
        case ActorType::Retaliate: return "报复型";
        case ActorType::Myopic: return "短视型";
        case ActorType::Count: break;
    }
    return "?";
}

}  // namespace gf
