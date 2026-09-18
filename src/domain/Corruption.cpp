#include "domain/Corruption.h"

#include <algorithm>
#include <string>

#include "util/TextTable.h"
#include "core/GameState.h"
#include "domain/Empire.h"
#include "domain/Species.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {
namespace {

/// 每个行星贡献的基础腐败压力
constexpr i64 kPerPlanetPct = 2;
/// 每个星系贡献的基础腐败压力
constexpr i64 kPerSystemPct = 1;
/// 每季自然增长的腐败（在压力之上）
constexpr i64 kDriftPct = 1;
/// 反腐运动的压制幅度
constexpr i64 kPurgePct = 35;

}  // namespace

Fixed corruptionOf(const GameState& st, u32 empire) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return Fixed(0);
    // 存储的 corruption 是**已结算**的当前值；规模压力在 corruptionPhase 中演化
    return fxClamp(e->corruption, Fixed(0), Fixed(1));
}

Fixed corruptionIncomeLoss(const GameState& st, u32 empire) {
    // 腐败度直接按比例抽走收入：30% 腐败 = 收入少 30%
    return corruptionOf(st, empire);
}

Fixed corruptionUnrest(const GameState& st, u32 empire) {
    // 腐败推高民怨，但幅度小于其对收入的侵蚀
    return corruptionOf(st, empire) * Fixed::pct(15);
}

bool antiCorruption(GameState& st, u32 empire, std::string* msg) {
    Empire* e = st.empire(empire);
    if (e == nullptr) {
        if (msg) *msg = "非法主体";
        return false;
    }
    if (e->treasury.rawValue() < Fixed(kAntiCorruptionCost).rawValue()) {
        if (msg) *msg = "国库不足：反腐运动需要 " + groupDigits(kAntiCorruptionCost) + " cr";
        return false;
    }
    const Fixed before = e->corruption;
    if (before.rawValue() <= 0) {
        if (msg) *msg = "当前没有可查的腐败";
        return false;
    }
    e->treasury -= Fixed(kAntiCorruptionCost);
    if (e->isPlayer) st.market.margin.cash = e->treasury;
    e->corruption = fxClamp(e->corruption - Fixed::pct(kPurgePct), Fixed(0), Fixed(1));
    // 反腐触动既得利益者
    for (auto& f : e->domestic.factions) {
        if (f.kind == FactionKind::Nobility || f.kind == FactionKind::Syndicate ||
            f.kind == FactionKind::Merchant)
            f.satisfaction = fxClamp(f.satisfaction - Fixed::pct(8), Fixed(0), Fixed(1));
        if (f.kind == FactionKind::Labor || f.kind == FactionKind::Populist)
            f.satisfaction = fxClamp(f.satisfaction + Fixed::pct(6), Fixed(0), Fixed(1));
    }
    if (msg)
        *msg = "反腐运动完成：腐败 " + fixedStrPlain(before * Fixed(100), 0) + "% → " +
               fixedStrPlain(e->corruption * Fixed(100), 0) + "%（" +
               groupDigits(kAntiCorruptionCost) +
               " cr；旧贵族/辛迪加/商会 -8%，劳工/民粹 +6%）";
    st.logEvent(LogPhase::Domestic, "corruption.purge",
                e->name + " 发起反腐运动（腐败降至 " +
                    fixedStrPlain(e->corruption * Fixed(100), 0) + "%）",
                empire);
    return true;
}

void corruptionPhase(GameState& st) {
    if (st.tick % 4 != 0) return;
    for (auto& e : st.empires) {
        if (!e.alive) continue;
        const GovernmentInfo& gi = governmentInfo(e.government);
        // 规模压力：行星与星系越多，行政越难覆盖
        i64 ownedPlanets = 0;
        for (const auto& p : st.planets)
            if (p.owner == e.id) ++ownedPlanets;
        Fixed pressure = Fixed::pct(kPerPlanetPct) * Fixed(ownedPlanets);
        pressure += Fixed::pct(kPerSystemPct) * Fixed(static_cast<i64>(e.systems.size()));
        // 政体倾向（资本主义 +30%，民主主义 -12%……）
        pressure += gi.corruptionBias;
        // 目标腐败度：压力占 40% 权重，封顶 85%
        Fixed target = fxClamp(pressure * Fixed::pct(40), Fixed(0), Fixed::pct(85));
        // 每季自然漂移 + 向目标收敛
        Fixed drift = Fixed::pct(kDriftPct);
        e.corruption = fxClamp(e.corruption + drift + (target - e.corruption) * Fixed::pct(25),
                               Fixed(0), Fixed(1));
    }
}

std::string corruptionReport(const GameState& st, u32 empire) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return "非法主体\n";
    const GovernmentInfo& gi = governmentInfo(e->government);
    i64 ownedPlanets = 0;
    for (const auto& p : st.planets)
        if (p.owner == empire) ++ownedPlanets;
    std::string out;
    TextTable t;
    t.header({"项目", "值"});
    t.row({"当前腐败", fixedStrPlain(e->corruption * Fixed(100), 1) + "%"});
    t.row({"收入侵蚀", "-" + fixedStrPlain(corruptionIncomeLoss(st, empire) * Fixed(100), 1) + "%"});
    t.row({"民怨增量", "+" + fixedStrPlain(corruptionUnrest(st, empire) * Fixed(100), 1) + "%"});
    t.row({"政体倾向", (gi.corruptionBias.rawValue() >= 0 ? "+" : "") +
                            fixedStrPlain(gi.corruptionBias * Fixed(100), 0) + "%" +
                            "（" + std::string(gi.nameZh) + "）"});
    t.row({"规模", std::to_string(ownedPlanets) + " 行星 / " + std::to_string(e->systems.size()) +
                        " 星系"});
    out += t.render();
    out += "  腐败随疆域扩张而上升，直接抽走收入。用 `greyfall corruption --purge` 治理\n";
    out += "  （" + groupDigits(kAntiCorruptionCost) + " cr，腐败 -" + std::to_string(kPurgePct) +
           "%，但会触怒旧贵族/辛迪加/商会）。\n";
    return out;
}

}  // namespace gf
