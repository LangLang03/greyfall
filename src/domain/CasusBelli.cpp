#include "domain/CasusBelli.h"

#include <algorithm>
#include <string>

#include "cli/TextTable.h"
#include "core/GameState.h"
#include "domain/Empire.h"
#include "domain/Treaty.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {
namespace {

/// 伪造宣称的影响力成本与有效期
constexpr i64 kFabricateCost = 400;
constexpr u32 kFabricateLifetime = 30;

/// 战争疲劳的每季增长：基础 + 战况修正。
/// 早期取值（基础 2% + 战况 3% + 民主 1% = 6%/季）使疲劳 15 季即达 90%，
/// 战争几乎不可持续 —— 玩家会因此完全不敢开战。下调到约 2.5%/季，
/// 一场势均力敌的战争可以打 30~40 季才触及反战阈值。
constexpr i64 kWearinessBasePct = 1;

/// 反战事件的触发间隔（季）
constexpr u32 kEventCooldown = 10;

WarWeariness& wearinessOf(Empire& e, u32 enemy) {
    for (auto& w : e.weariness)
        if (w.enemy == enemy) return w;
    WarWeariness w;
    w.enemy = enemy;
    e.weariness.push_back(w);
    return e.weariness.back();
}

}  // namespace

std::string_view casusBelliName(CasusBelliKind k) {
    switch (k) {
        case CasusBelliKind::TerritorialDispute: return "领土争端";
        case CasusBelliKind::BorderIncident: return "边界摩擦";
        case CasusBelliKind::Retaliation: return "反制制裁";
        case CasusBelliKind::AllyDefense: return "盟友受侵";
        case CasusBelliKind::FabricatedClaim: return "伪造宣称";
        case CasusBelliKind::BrokenTreaty: return "条约被撕毁";
        case CasusBelliKind::None:
        case CasusBelliKind::Count: break;
    }
    return "无";
}

std::string casusBelliDesc(CasusBelliKind k) {
    switch (k) {
        case CasusBelliKind::TerritorialDispute:
            return "对方持有与你接壤的星系。这是最常见的开战理由，也是最经得起推敲的。";
        case CasusBelliKind::BorderIncident:
            return "边境发生武装摩擦。理由不算充分，国内反战派会质疑。";
        case CasusBelliKind::Retaliation:
            return "对方对你实施了制裁或禁运。以反制为名开战，国际观感相对较好。";
        case CasusBelliKind::AllyDefense:
            return "你的盟友遭到对方攻击。履行同盟义务是公认的正当理由。";
        case CasusBelliKind::FabricatedClaim:
            return "**伪造的宣称**。它让你能合法开战，但经不起检验："
                   "其他国家看得穿，且宣称会随时间失效。";
        case CasusBelliKind::BrokenTreaty:
            return "对方单方面撕毁了与你签订的条约。";
        default: break;
    }
    return "";
}

const CasusBelli* findCasusBelli(const GameState& st, u32 empire, u32 target) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return nullptr;
    for (const auto& c : e->casusBelli) {
        if (c.target != target) continue;
        if (c.expireTick >= 0 && st.tick > static_cast<u64>(c.expireTick)) continue;   // 已过期
        return &c;
    }
    return nullptr;
}

bool hasCasusBelli(const GameState& st, u32 empire, u32 target) {
    return findCasusBelli(st, empire, target) != nullptr;
}

void clearCasusBelli(GameState& st, u32 empire, u32 target) {
    Empire* e = st.empire(empire);
    if (e == nullptr) return;
    e->casusBelli.erase(std::remove_if(e->casusBelli.begin(), e->casusBelli.end(),
                                       [&](const CasusBelli& c) { return c.target == target; }),
                        e->casusBelli.end());
}

bool fabricateClaim(GameState& st, u32 empire, u32 target, std::string* msg) {
    Empire* e = st.empire(empire);
    Empire* t = st.empire(target);
    if (e == nullptr || t == nullptr) {
        if (msg) *msg = "非法主体";
        return false;
    }
    if (hasCasusBelli(st, empire, target)) {
        if (msg)
            *msg = std::string("你已持有对 ") + t->name + " 的正当理由【" +
                   std::string(casusBelliName(findCasusBelli(st, empire, target)->kind)) + "】";
        return false;
    }
    if (e->influence.rawValue() < Fixed(kFabricateCost).rawValue()) {
        if (msg) *msg = "影响力不足：伪造宣称需要 " + std::to_string(kFabricateCost);
        return false;
    }
    e->influence -= Fixed(kFabricateCost);
    CasusBelli c;
    c.kind = CasusBelliKind::FabricatedClaim;
    c.target = target;
    c.gainedTick = st.tick;
    c.expireTick = static_cast<i64>(st.tick) + kFabricateLifetime;
    c.note = "由 " + e->name + " 的情报机构伪造";
    e->casusBelli.push_back(c);

    // 伪造会被察觉：目标与第三方观感下降
    st.relation(target, empire).opinion =
        fxClamp(st.relation(target, empire).opinion - Fixed::pct(15), Fixed(-1), Fixed(1));
    for (auto& o : st.empires) {
        if (o.id == empire || o.id == target || !o.alive) continue;
        o.addOpinion(empire, Fixed::pct(-4));
    }
    st.logEvent(LogPhase::Model, "diplo.casus",
                e->name + " 伪造了对 " + t->name + " 的宣战理由（有效期 " +
                    std::to_string(kFabricateLifetime) + " 季）",
                empire);
    if (msg)
        *msg = "已伪造对 " + t->name + " 的宣称（影响力 -" + std::to_string(kFabricateCost) +
               "，有效期 " + std::to_string(kFabricateLifetime) + " 季；对方观感 -15%）";
    return true;
}

void casusBelliPhase(GameState& st) {
    // 自然产生的正当理由。节奏放慢（每 5 季评估一次），避免理由烂大街。
    if (st.tick % 5 != 0) return;
    for (auto& e : st.empires) {
        if (!e.alive) continue;
        // 清理过期
        e.casusBelli.erase(
            std::remove_if(e.casusBelli.begin(), e.casusBelli.end(),
                           [&](const CasusBelli& c) {
                               return c.expireTick >= 0 && st.tick > static_cast<u64>(c.expireTick);
                           }),
            e.casusBelli.end());

        for (const auto& o : st.empires) {
            if (o.id == e.id || !o.alive) continue;
            if (hasCasusBelli(st, e.id, o.id)) continue;
            const Relation& rel = st.relation(e.id, o.id);
            if (rel.atWar) continue;

            CasusBelliKind kind = CasusBelliKind::None;
            std::string note;

            // 1) 反制制裁：对方正在制裁或禁运我
            if (rel.embargo || hasTreaty(st.treaties, TreatyKind::Sanction, o.id, e.id)) {
                kind = CasusBelliKind::Retaliation;
                note = o.name + " 的制裁与禁运";
            } else {
                // 2) 领土争端：对方持有与我接壤的星系
                for (u32 sid : o.systems) {
                    const SystemNode* s = st.system(sid);
                    if (s == nullptr) continue;
                    bool adjacentToMe = false;
                    for (u32 nx : s->links) {
                        const SystemNode* m = st.system(nx);
                        if (m != nullptr && m->owner == e.id) adjacentToMe = true;
                    }
                    if (!adjacentToMe) continue;
                    kind = CasusBelliKind::TerritorialDispute;
                    note = "接壤星系 " + s->name;
                    break;
                }
            }
            // 3) 盟友受侵：与我结盟的国家正与对方交战
            if (kind == CasusBelliKind::None) {
                for (const auto& ally : st.empires) {
                    if (ally.id == e.id || ally.id == o.id || !ally.alive) continue;
                    if (!hasTreaty(st.treaties, TreatyKind::DefensivePact, e.id, ally.id)) continue;
                    if (!atWarWith(st, ally.id, o.id)) continue;
                    kind = CasusBelliKind::AllyDefense;
                    note = "盟友 " + ally.name + " 正与 " + o.name + " 交战";
                    break;
                }
            }
            // 4) 边界摩擦：概率事件，只有接壤才可能
            if (kind == CasusBelliKind::None) {
                bool border = false;
                for (u32 sid : o.systems) {
                    const SystemNode* s = st.system(sid);
                    if (s == nullptr) continue;
                    for (u32 nx : s->links) {
                        const SystemNode* m = st.system(nx);
                        if (m != nullptr && m->owner == e.id) border = true;
                    }
                }
                if (border && st.rng.chance(RngStream::Diplo, Fixed::pct(8))) {
                    kind = CasusBelliKind::BorderIncident;
                    note = "边境武装摩擦";
                }
            }
            if (kind == CasusBelliKind::None) continue;

            CasusBelli c;
            c.kind = kind;
            c.target = o.id;
            c.gainedTick = st.tick;
            // 领土争端长期有效；摩擦是短期的
            c.expireTick = (kind == CasusBelliKind::BorderIncident)
                               ? static_cast<i64>(st.tick) + 20
                               : -1;
            c.note = note;
            e.casusBelli.push_back(c);
            if (e.isPlayer) {
                st.logEvent(LogPhase::Model, "diplo.casus",
                            "获得对 " + o.name + " 的正当战争理由【" +
                                std::string(casusBelliName(kind)) + "】：" + note,
                            e.id);
            }
        }
    }
}

void warWearinessPhase(GameState& st) {
    for (auto& e : st.empires) {
        if (!e.alive) continue;
        // 清理已不存在的战争
        e.weariness.erase(std::remove_if(e.weariness.begin(), e.weariness.end(),
                                         [&](const WarWeariness& w) {
                                             return !atWarWith(st, e.id, w.enemy) ||
                                                    st.empire(w.enemy) == nullptr ||
                                                    !st.empire(w.enemy)->alive;
                                         }),
                          e.weariness.end());

        for (const auto& o : st.empires) {
            if (o.id == e.id || !o.alive) continue;
            if (!atWarWith(st, e.id, o.id)) continue;

            WarWeariness& w = wearinessOf(e, o.id);
            if (w.startTick == 0) w.startTick = st.tick;

            // 疲劳增长：基础 2%/季，战况不利（战果为负）再加 3%
            Fixed growth = Fixed::pct(kWearinessBasePct);
            const Relation& rel = st.relation(e.id, o.id);
            if (rel.warScore <= 0) growth += Fixed::pct(1);   // 战况不利的额外压力
            // 民主政体对战争更敏感（选民会施压）。
            // government 是编号而非枚举：按议会已用的约定，
            // 2=寡头 3=独裁 5=神权 8=军事委员会 9=蜂群 11=无政府 属非民主，
            // 其余（含共和/民主）视为需要面对选民。
            switch (e.government) {
                case 2: case 3: case 5: case 8: case 9: case 11: break;
                default: growth += Fixed::pct(1); break;
            }
            w.value = fxClamp(w.value + growth, Fixed(0), Fixed(1));

            // 反战事件：疲劳 >= 50% 后每 kEventCooldown 季触发一次
            if (w.value.rawValue() >= Fixed::pct(50).rawValue() &&
                st.tick >= static_cast<u64>(w.lastEventTick) + kEventCooldown) {
                w.lastEventTick = static_cast<u32>(st.tick);
                if (w.value.rawValue() >= Fixed::pct(80).rawValue()) {
                    w.peaceDemanded = true;
                    if (e.isPlayer) {
                        st.logEvent(LogPhase::Domestic, "war.demand",
                                    "反战浪潮：民众与派系公开要求结束与 " + o.name +
                                        " 的战争（战争疲劳 " +
                                        fixedStrPlain(w.value * Fixed(100), 0) + "%）",
                                    e.id);
                    }
                } else if (e.isPlayer) {
                    st.logEvent(LogPhase::Domestic, "war.unrest",
                                "国内反战情绪上升（对 " + o.name + " 战争疲劳 " +
                                    fixedStrPlain(w.value * Fixed(100), 0) + "%）",
                                e.id);
                }
            }
        }
    }
}

Fixed wearinessStabilityPenalty(const GameState& st, u32 empire) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return Fixed(0);
    Fixed worst = Fixed(0);
    for (const auto& w : e->weariness) {
        if (!atWarWith(st, empire, w.enemy)) continue;
        if (w.value.rawValue() > worst.rawValue()) worst = w.value;
    }
    // 疲劳 50% 起开始扣稳定度，满值扣 15%
    if (worst.rawValue() <= Fixed::pct(50).rawValue()) return Fixed(0);
    Fixed over = worst - Fixed::pct(50);
    return -(over / Fixed::pct(50)) * Fixed::pct(15);
}

Fixed wearinessUnrestPenalty(const GameState& st, u32 empire) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return Fixed(0);
    Fixed worst = Fixed(0);
    for (const auto& w : e->weariness) {
        if (!atWarWith(st, empire, w.enemy)) continue;
        if (w.value.rawValue() > worst.rawValue()) worst = w.value;
    }
    if (worst.rawValue() <= Fixed::pct(50).rawValue()) return Fixed(0);
    Fixed over = worst - Fixed::pct(50);
    return (over / Fixed::pct(50)) * Fixed::pct(12);
}

Fixed wearinessSatisfactionPenalty(const GameState& st, u32 empire) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return Fixed(0);
    bool demanded = false;
    for (const auto& w : e->weariness)
        if (w.peaceDemanded && atWarWith(st, empire, w.enemy)) demanded = true;
    // 派系公开要求停战却继续打：满意度 -18%
    return demanded ? Fixed::pct(18) : Fixed(0);
}

std::string casusBelliReport(const GameState& st, u32 empire) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return "非法主体\n";
    std::string out;
    // 清理过期后展示
    std::vector<const CasusBelli*> live;
    for (const auto& c : e->casusBelli) {
        if (c.expireTick >= 0 && st.tick > static_cast<u64>(c.expireTick)) continue;
        live.push_back(&c);
    }
    if (live.empty()) {
        out += "  （当前没有任何正当战争理由）\n";
        out += "  用 `greyfall envoy <emp> fabricate` 花影响力伪造一个宣称，\n";
        out += "  或等待领土争端 / 边界摩擦 / 盟友受侵 / 反制制裁自然产生。\n";
        return out;
    }
    TextTable t;
    t.header({"目标", "理由", "说明", "失效"});
    for (const CasusBelli* c : live) {
        const Empire* o = st.empire(c->target);
        t.row({o ? o->name.substr(0, 8) : std::string("?"), std::string(casusBelliName(c->kind)),
               c->note, c->expireTick < 0 ? std::string("长期有效")
                                          : (std::to_string(c->expireTick) + " 季")});
    }
    out += t.render();
    return out;
}

std::string wearinessReport(const GameState& st, u32 empire) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return "非法主体\n";
    std::string out;
    bool any = false;
    TextTable t;
    t.header({"对手", "战争疲劳", "状态", "减益"});
    for (const auto& w : e->weariness) {
        if (!atWarWith(st, empire, w.enemy)) continue;
        const Empire* o = st.empire(w.enemy);
        any = true;
        std::string state = w.peaceDemanded ? "派系要求停战"
                            : (w.value.rawValue() >= Fixed::pct(50).rawValue() ? "反战情绪上升"
                                                                              : "尚可承受");
        std::string deb;
        if (w.value.rawValue() > Fixed::pct(50).rawValue()) {
            deb = "稳定 " + fixedStrPlain(wearinessStabilityPenalty(st, empire) * Fixed(100), 0) +
                  "% / 民怨 +" + fixedStrPlain(wearinessUnrestPenalty(st, empire) * Fixed(100), 0) + "%";
        } else {
            deb = "—";
        }
        t.row({o ? o->name.substr(0, 8) : std::string("?"),
               fixedStrPlain(w.value * Fixed(100), 0) + "%", state, deb});
    }
    if (!any) return "  （当前没有进行中的战争）\n";
    out += t.render();
    if (wearinessSatisfactionPenalty(st, empire).rawValue() > 0) {
        out += "  派系已公开要求停战：继续作战将导致满意度 -18%。\n";
    }
    return out;
}

}  // namespace gf
