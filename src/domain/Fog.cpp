#include "domain/Fog.h"

#include <algorithm>
#include <string>

#include "util/TextTable.h"
#include "core/GameState.h"
#include "domain/Empire.h"
#include "domain/SpyNetwork.h"
#include "domain/Treaty.h"
#include "gen/EmpireGen.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {

std::string_view intelFieldName(IntelField f) {
    switch (f) {
        case IntelField::Military: return "军力";
        case IntelField::Treasury: return "国库";
        case IntelField::Tech: return "科技";
        case IntelField::Policies: return "政策与决议";
        case IntelField::Relations: return "外交关系";
        case IntelField::FleetPositions: return "舰队位置";
        case IntelField::Count: break;
    }
    return "?";
}

Fixed intelThreshold(IntelField f) {
    switch (f) {
        case IntelField::Military: return Fixed::pct(20);
        case IntelField::FleetPositions: return Fixed::pct(30);
        case IntelField::Treasury: return Fixed::pct(40);
        case IntelField::Tech: return Fixed::pct(55);
        case IntelField::Relations: return Fixed::pct(45);
        case IntelField::Policies: return Fixed::pct(65);
        case IntelField::Count: break;
    }
    return Fixed::pct(50);
}

Fixed intelLevel(const GameState& st, u32 observer, u32 target) {
    if (observer == target) return Fixed(1);
    const Empire* o = st.empire(observer);
    const Empire* t = st.empire(target);
    if (o == nullptr || t == nullptr || !t->alive) return Fixed(0);

    Fixed level = Fixed(0);

    // 1) 间谍网络渗透度：最主要的来源
    for (const auto& n : o->spy.networks) {
        if (n.target != target || n.burned) continue;
        level += n.infiltration * Fixed::pct(70);
    }

    // 2) 外交渠道：条约国之间信息更透明
    if (hasTreaty(st.treaties, TreatyKind::ResearchPact, observer, target))
        level += Fixed::pct(12);
    if (hasTreaty(st.treaties, TreatyKind::JointIntel, observer, target))
        level += Fixed::pct(22);
    if (hasTreaty(st.treaties, TreatyKind::DefensivePact, observer, target))
        level += Fixed::pct(10);
    if (hasTreaty(st.treaties, TreatyKind::NonAggression, observer, target))
        level += Fixed::pct(5);

    // 3) 贸易往来：商业渠道透露经济状况
    for (const auto& r : st.market.trade.routes) {
        if (!r.active) continue;
        if ((r.exporter == observer && r.importer == target) ||
            (r.exporter == target && r.importer == observer))
            level += Fixed::pct(6);
    }

    // 4) 接壤：边境观察能看出军力调动
    const Relation& rel = st.relation(observer, target);
    if (rel.border > 0) level += Fixed::pct(8);

    // 5) 交战：战时情报大增（但也很粗）
    if (atWarWith(st, observer, target)) level += Fixed::pct(15);

    // 6) 对方的反间谍削减情报
    level -= t->counterIntel * Fixed::pct(40);
    level -= t->intelDefense * Fixed::pct(25);

    return fxClamp(level, Fixed(0), Fixed(1));
}

bool intelKnown(const GameState& st, u32 observer, u32 target, IntelField f) {
    if (observer == target) return true;
    return intelLevel(st, observer, target).rawValue() >= intelThreshold(f).rawValue();
}

std::string intelNumber(const GameState& st, u32 observer, u32 target, IntelField f, Fixed value,
                        int decimals) {
    if (intelKnown(st, observer, target, f)) return fixedStr(value, decimals);
    Fixed lvl = intelLevel(st, observer, target);
    // 未达门槛：给出一个**区间**，宽度随情报缺口放大。
    // 玩家因此只能判断量级，无法据精确数字做最优决策 —— 这正是迷雾的意义。
    Fixed gap = intelThreshold(f) - lvl;
    if (gap.rawValue() < 0) gap = Fixed(0);
    // 误差 ±(30% ~ 90%)
    Fixed err = Fixed::pct(30) + gap * Fixed::pct(90);
    Fixed lo = value * (Fixed(1) - err);
    Fixed hi = value * (Fixed(1) + err);
    if (lo.rawValue() < 0) lo = Fixed(0);
    return "约 " + fixedStr(lo, decimals) + " ~ " + fixedStr(hi, decimals);
}

std::string intelSourceBreakdown(const GameState& st, u32 observer, u32 target) {
    const Empire* o = st.empire(observer);
    const Empire* t = st.empire(target);
    if (o == nullptr || t == nullptr) return "  非法主体\n";
    std::string out;
    TextTable tab;
    tab.header({"来源", "贡献"});
    Fixed spy = Fixed(0);
    for (const auto& n : o->spy.networks) {
        if (n.target != target || n.burned) continue;
        spy += n.infiltration * Fixed::pct(70);
    }
    tab.row({"间谍网络渗透", "+" + fixedStrPlain(spy * Fixed(100), 1) + "%"});
    Fixed diplo = Fixed(0);
    if (hasTreaty(st.treaties, TreatyKind::JointIntel, observer, target)) diplo += Fixed::pct(22);
    if (hasTreaty(st.treaties, TreatyKind::ResearchPact, observer, target)) diplo += Fixed::pct(12);
    if (hasTreaty(st.treaties, TreatyKind::DefensivePact, observer, target)) diplo += Fixed::pct(10);
    if (hasTreaty(st.treaties, TreatyKind::NonAggression, observer, target)) diplo += Fixed::pct(5);
    tab.row({"外交条约", "+" + fixedStrPlain(diplo * Fixed(100), 1) + "%"});
    Fixed tradePct = Fixed(0);
    for (const auto& r : st.market.trade.routes) {
        if (!r.active) continue;
        if ((r.exporter == observer && r.importer == target) ||
            (r.exporter == target && r.importer == observer))
            tradePct += Fixed::pct(6);
    }
    tab.row({"贸易往来", "+" + fixedStrPlain(tradePct * Fixed(100), 1) + "%"});
    const Relation& rel = st.relation(observer, target);
    tab.row({"接壤", rel.border > 0 ? "+8.0%" : "+0.0%"});
    tab.row({"交战状态", atWarWith(st, observer, target) ? "+15.0%" : "+0.0%"});
    tab.row({"对方反间谍", "-" + fixedStrPlain(t->counterIntel * Fixed::pct(40) * Fixed(100), 1) + "%"});
    tab.row({"对方情报防御", "-" + fixedStrPlain(t->intelDefense * Fixed::pct(25) * Fixed(100), 1) + "%"});
    tab.row({"合计", fixedStrPlain(intelLevel(st, observer, target) * Fixed(100), 1) + "%"});
    out += tab.render();
    return out;
}

std::string intelPicture(const GameState& st, u32 observer, u32 target) {
    const Empire* t = st.empire(target);
    if (t == nullptr) return "  非法主体\n";
    Fixed lvl = intelLevel(st, observer, target);
    std::string out;
    TextTable tab;
    tab.header({"信息", "等级", "需要", "状态", "你看到的"});
    for (int i = 0; i < static_cast<int>(IntelField::Count); ++i) {
        IntelField f = static_cast<IntelField>(i);
        bool known = intelKnown(st, observer, target, f);
        Fixed need = intelThreshold(f);
        std::string seen;
        switch (f) {
            case IntelField::Military:
                seen = intelNumber(st, observer, target, f, t->military, 0);
                break;
            case IntelField::Treasury:
                seen = intelNumber(st, observer, target, f, t->treasury, 0) + " cr";
                break;
            case IntelField::Tech:
                seen = intelNumber(st, observer, target, f,
                                   Fixed(static_cast<i64>(t->tech.completed.size())), 0) + " 项";
                break;
            case IntelField::Relations:
                seen = intelNumber(st, observer, target, f, t->powerIndex(), 0);
                break;
            case IntelField::Policies:
                seen = intelNumber(st, observer, target, f, Fixed(static_cast<i64>(t->policies.enactCount)), 0) + " 次推行";
                break;
            case IntelField::FleetPositions: {
                if (known) {
                    seen = std::to_string(t->fleets.size()) + " 支舰队";
                } else {
                    seen = "约 " + std::to_string(t->fleets.size() / 2) + " ~ " +
                           std::to_string(t->fleets.size() + 2) + " 支";
                }
                break;
            }
            default: break;
        }
        tab.row({std::string(intelFieldName(f)),
                 fixedStrPlain(lvl * Fixed(100), 0) + "%",
                 fixedStrPlain(need * Fixed(100), 0) + "%",
                 known ? "已掌握" : "**模糊**", seen});
    }
    out += tab.render();
    return out;
}

void intelPhase(GameState& st) {
    // 情报等级是**派生量**（由渗透、条约、贸易实时算出），不需要额外状态。
    // 这里只做一件事：反间谍能力随时间自然回归，
    // 避免一次投入之后永久高位（那样迷雾就再也不会回来了）。
    if (st.tick % 8 != 0) return;
    for (auto& e : st.empires) {
        if (!e.alive) continue;
        e.counterIntel = fxClamp(e.counterIntel * Fixed::pct(98), Fixed(0), Fixed(1));
        // 基础情报防御由科技与政体决定，缓慢回归到该基线
        Fixed baseline = Fixed::pct(30) + empireModifier(e, ModKind::IntelDefense);
        e.intelDefense = fxLerp(e.intelDefense, fxClamp(baseline, Fixed(0), Fixed(1)), Fixed::pct(20));
    }
}

}  // namespace gf
