#include "gen/EmpireGen.h"
#include "ai/Reputation.h"

#include <algorithm>

#include "domain/Treaty.h"
#include "mkt/OrderBook.h"

namespace gf {

ObservationChannel observationChannel(const GameState& st, u32 observer, u32 target) {
    ObservationChannel ch;
    ch.target = target;
    const Empire* o = st.empire(observer);
    const Empire* t = st.empire(target);
    if (o == nullptr || t == nullptr) return ch;

    // 距离噪声：首都跳数
    int hops = st.map.hops(o->capital, t->capital);
    if (hops < 0) hops = 12;
    ch.distanceNoise = Fixed::pct(3) * Fixed(hops);
    // 宣传偏移
    ch.propaganda = t->propaganda;
    // 反间谍
    ch.counterIntel = t->counterIntel + empireModifier(*t, ModKind::IntelDefense);
    // 联邦成员之间的观测更清晰
    ch.weight = Fixed(1);
    if (o->federation != 0xFFFFFFFFu && o->federation == t->federation) ch.weight = Fixed::raw(1400);
    return ch;
}

Fixed reputationOf(const GameState& st, u32 observer, u32 target) {
    const Empire* o = st.empire(observer);
    if (o == nullptr || target >= kMaxEmpires) return Fixed::pct(50);
    return o->mind.reputation[target];
}

void reputationUpdate(GameState& st, u32 observer, u32 target, Fixed eventValue, Fixed halfLife) {
    Empire* o = st.empire(observer);
    if (o == nullptr || target >= kMaxEmpires) return;
    ObservationChannel ch = observationChannel(st, observer, target);
    // 观测模型：真值与宣传先叠加，再被「保真度」（距离 + 反间谍）整体衰减，
    // 最后叠加零均值噪声。反间谍只模糊信号，绝不引入系统性负偏 ——
    // 否则信誉会在什么都没发生时也单调下滑。
    Fixed noise = st.rng.normal(RngStream::Diplo) * Fixed::raw(2);
    Fixed fidelity = fxClamp(Fixed(1) - ch.distanceNoise - ch.counterIntel, Fixed::pct(15), Fixed(1));
    Fixed observed = (eventValue + ch.propaganda) * fidelity + noise;
    Fixed scaled = observed * ch.weight;
    o->mind.reputation[target] += scaled;
    if (scaled.rawValue() < 0) {
        o->mind.grudge[target] += -scaled;
    }
    // 无事件时向中性缓慢回归
    o->mind.reputation[target] = fxLerp(o->mind.reputation[target], Fixed::pct(50), Fixed::pct(4));
    o->mind.reputation[target] = fxClamp(o->mind.reputation[target], Fixed(0), Fixed(1));
    (void)halfLife;
}

void reputationUpdateAll(GameState& st, u32 target, Fixed eventValue, u32 exclude) {
    for (const auto& e : st.empires) {
        if (e.id == target || e.id == exclude || !e.alive) continue;
        reputationUpdate(st, e.id, target, eventValue);
    }
}

void grudgeDecay(GameState& st) {
    for (auto& e : st.empires) {
        for (auto& g : e.mind.grudge) {
            // grudgeHalfLife ≈ 8 tick
            g = g * Fixed::raw(917);
            if (g.rawValue() < 1) g = Fixed(0);
        }
    }
}

void opinionPhase(GameState& st) {
    grudgeDecay(st);
    for (auto& e : st.empires) {
        if (!e.alive) continue;
        for (std::size_t i = 0; i < e.opinion.size(); ++i) {
            if (i == e.id) continue;
            const Empire* other = st.empire(static_cast<u32>(i));
            if (other == nullptr || !other->alive) continue;
            Fixed target = e.opinionOf(static_cast<u32>(i));
            // 同伦理/同政体互相靠拢
            int shared = 0;
            for (u8 x : e.ethics)
                for (u8 y : other->ethics)
                    if (x == y) ++shared;
            target += Fixed::pct(1) * Fixed(shared);
            // 怨恨拉低观感
            target -= e.mind.grudge[i] * Fixed::pct(15);
            // 战争状态
            const Relation& rel = st.relation(e.id, static_cast<u32>(i));
            if (rel.atWar) target -= Fixed::pct(4);
            // 贸易条约拉高
            if (hasTreaty(st.treaties, TreatyKind::TradePact, e.id, static_cast<u32>(i)))
                target += Fixed::pct(2);
            if (hasTreaty(st.treaties, TreatyKind::NonAggression, e.id, static_cast<u32>(i)))
                target += Fixed::pct(1);
            e.setOpinion(static_cast<u32>(i), fxClamp(fxLerp(e.opinionOf(static_cast<u32>(i)), target, Fixed::pct(20)), Fixed(-1), Fixed(1)));
            // 同步到 Relation
            Relation& r = st.relation(e.id, static_cast<u32>(i));
            r.opinion = e.opinionOf(static_cast<u32>(i));
            r.trust = fxClamp(r.trust + (e.mind.reputation[i] - Fixed::pct(50)) * Fixed::pct(10), Fixed(0), Fixed(1));
        }
    }
}

}  // namespace gf
