#include "mkt/Insider.h"

#include <algorithm>

#include "domain/ModifierUtil.h"
#include "rng/Streams.h"
#include "util/Str.h"

namespace gf {

void insiderEmitSignals(GameState& st) {
    MarketState& m = st.market;
    // 数据泛滥词缀：信号更多但更不可信
    const bool glut = hasModifier(st.modifierBits, kModDataGlut);
    const Fixed chance = glut ? Fixed::pct(35) : Fixed::pct(18);
    for (std::size_t i = 0; i < st.empires.size(); ++i) {
        const Empire& e = st.empires[i];
        if (!e.alive || e.isPlayer) continue;   // 玩家自己不能"被内幕"
        if (!st.rng.chance(RngStream::Market, chance)) continue;
        InsiderSignal sig;
        sig.res = static_cast<u8>(st.rng.pick(RngStream::Market, kCommodityCount));
        if (!commodityInfo(sig.res).tradable) continue;
        sig.magnitude = Fixed::raw(static_cast<i64>(st.rng.range(RngStream::Market, 400, 950)));
        if (glut) sig.magnitude = sig.magnitude * Fixed::pct(75);
        sig.fireTick = st.tick + 1 + static_cast<u64>(st.rng.range(RngStream::Market, 1, 4));
        sig.knower = e.id;
        sig.consumed = false;
        m.insiderSignals.push_back(sig);
    }
    if (m.insiderSignals.size() > 256) {
        m.insiderSignals.erase(m.insiderSignals.begin(), m.insiderSignals.begin() + 64);
    }
}

Fixed insiderTrace(const GameState& st, u32 actor, u8 res) {
    Fixed trace = Fixed(0);
    for (const auto& s : st.market.insiderSignals) {
        if (s.knower != actor || s.res != res) continue;
        trace = fxMax(trace, s.magnitude);
    }
    return trace;
}

std::string insiderReport(const GameState& st, u32 target) {
    const Empire* e = st.empire(target);
    std::string out = "内幕侦察：" + (e ? e->name : std::string("未知主体")) + "\n";
    int found = 0;
    for (const auto& s : st.market.insiderSignals) {
        if (s.knower != target || s.consumed) continue;
        out += "  · 在【" + std::string(commodityName(s.res)) + "】上存在知情痕迹，评分 " +
               fixedStrPlain(s.magnitude, 2) + "，预计 " + std::to_string(s.fireTick) + " tick 内公布相关事件\n";
        ++found;
    }
    if (found == 0) out += "  · 未发现活跃的知情痕迹\n";
    for (const auto& rec : st.market.manipulations) {
        if (rec.actor != target || rec.kind != ManipKind::Insider) continue;
        out += "  · 历史记录 tick " + std::to_string(rec.tick) + "：" + rec.detail +
               (rec.penalized ? "（已被裁罚）" : "（未被裁罚）") + "\n";
    }
    return out;
}

void insiderConsume(GameState& st) {
    for (auto& s : st.market.insiderSignals) {
        if (!s.consumed && st.tick >= s.fireTick) s.consumed = true;
    }
}

}  // namespace gf
