#include "domain/Revolt.h"

#include <algorithm>
#include <string>

#include "cli/TextTable.h"
#include "core/GameState.h"
#include "domain/Empire.h"
#include "domain/Planet.h"
#include "domain/Treaty.h"
#include "gen/EmpireGen.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {
namespace {

/// 进入各阶段的风险阈值。
/// 必须低于「极端条件下的风险上限」，否则高阶段永远不可达 ——
/// 旧权重下上限只有约 73%，刚好卡在叛乱阈值之下，实测起义从不发生。
constexpr i64 kUnrestThreshold = 50;
constexpr i64 kRevoltThreshold = 65;
constexpr i64 kSecessionThreshold = 82;

/// 割据持续多少季仍未平息，就真的脱离
constexpr u32 kSecessionDelay = 8;

/// 派系通牒的影响力与满意度门槛
constexpr i64 kUltimatumInfluencePct = 30;
constexpr i64 kUltimatumSatisfactionPct = 22;

FactionKind strongestFaction(const Empire& e) {
    FactionKind best = FactionKind::Populist;
    Fixed bestScore = Fixed(-1);
    for (const auto& f : e.domestic.factions) {
        // 不满 × 影响力 = 闹事的资本
        Fixed score = f.influence * (Fixed(1) - f.satisfaction);
        if (score.rawValue() > bestScore.rawValue()) {
            bestScore = score;
            best = f.kind;
        }
    }
    return best;
}

Faction* factionOf(Empire& e, FactionKind k) {
    for (auto& f : e.domestic.factions)
        if (f.kind == k) return &f;
    return nullptr;
}

/// 找一个可以接收割据星系的邻国（接壤、非交战、伦理接近）
u32 findReceiver(const GameState& st, u32 system, u32 from) {
    const SystemNode* sys = st.system(system);
    if (sys == nullptr) return kNoEmpire;
    u32 best = kNoEmpire;
    Fixed bestOpinion = Fixed(-2);
    for (u32 nx : sys->links) {
        const SystemNode* n = st.system(nx);
        if (n == nullptr || n->owner == kNoEmpire || n->owner == from) continue;
        if (atWarWith(st, n->owner, from)) continue;
        const Relation& rel = st.relation(from, n->owner);
        if (rel.opinion.rawValue() > bestOpinion.rawValue()) {
            bestOpinion = rel.opinion;
            best = n->owner;
        }
    }
    return best;
}

}  // namespace

std::string_view revoltStageName(RevoltStage s) {
    switch (s) {
        case RevoltStage::Calm: return "平静";
        case RevoltStage::Unrest: return "不安";
        case RevoltStage::Revolt: return "叛乱";
        case RevoltStage::Secession: return "割据";
        case RevoltStage::Count: break;
    }
    return "?";
}

std::string revoltStageDesc(RevoltStage s) {
    switch (s) {
        case RevoltStage::Calm:
            return "秩序正常。";
        case RevoltStage::Unrest:
            return "民怨高企：产出下降、驻军增加，但尚未公开对抗。此时安抚成本最低。";
        case RevoltStage::Revolt:
            return "公开叛乱：该星系停止纳贡，驻军只维持最低秩序。必须派兵镇压或做出让步。";
        case RevoltStage::Secession:
            return "割据：若持续未平息，该星系将脱离控制 —— 或自立为国，或倒向邻国。";
        default: break;
    }
    return "";
}

Fixed revoltRiskOf(const GameState& st, u32 system) {
    const SystemNode* sys = st.system(system);
    if (sys == nullptr || sys->owner == kNoEmpire) return Fixed(0);
    const Empire* e = st.empire(sys->owner);
    if (e == nullptr || !e->alive) return Fixed(0);

    Fixed risk = Fixed(0);
    // 1) 帝国整体民怨（主因）
    risk += e->domestic.unrest * Fixed::pct(55);
    // 2) 稳定度不足
    risk += (Fixed::pct(50) - e->stability) * Fixed::pct(70);
    // 3) 首都所在星系更稳定（中央直接控制）
    if (sys->capital) risk -= Fixed::pct(25);
    // 4) 行星上的局部民怨
    for (u32 pid : sys->planets) {
        const Planet* p = st.planet(pid);
        if (p == nullptr || p->owner != e->id) continue;
        risk += p->unrest * Fixed::pct(35);
    }
    // 5) 驻军压制（每 200  garrison 抵消 1% 风险）
    Fixed garrison = Fixed(0);
    for (u32 pid : sys->planets) {
        const Planet* p = st.planet(pid);
        if (p != nullptr) garrison += p->garrison;
    }
    risk -= garrison / Fixed(200);
    // 6) 战争疲劳雪上加霜
    risk += fxAbs(wearinessUnrestPenalty(st, e->id)) * Fixed(2);
    // 7) 派系不满：闹事的派系会动员地方
    Fixed factionPush = Fixed(0);
    for (const auto& f : e->domestic.factions) {
        Fixed push = f.influence * (Fixed(1) - f.satisfaction);
        if (push.rawValue() > factionPush.rawValue()) factionPush = push;
    }
    risk += factionPush * Fixed::pct(25);
    return fxClamp(risk, Fixed(0), Fixed(1));
}

const Revolt* revoltAt(const GameState& st, u32 system) {
    for (const auto& r : st.revolts)
        if (r.system == system) return &r;
    return nullptr;
}

RevoltStage revoltStageOf(const GameState& st, u32 system) {
    const Revolt* r = revoltAt(st, system);
    return r == nullptr ? RevoltStage::Calm : r->stage;
}

void revoltPhase(GameState& st) {
    // ---- 1) 评估所有有主星系的动乱风险 ----
    for (const auto& sys : st.map.systems) {
        if (sys.owner == kNoEmpire) continue;
        Fixed risk = revoltRiskOf(st, sys.id);
        int riskPct = static_cast<int>(risk.rawValue() / 10);

        RevoltStage want = RevoltStage::Calm;
        if (riskPct >= kSecessionThreshold) want = RevoltStage::Secession;
        else if (riskPct >= kRevoltThreshold) want = RevoltStage::Revolt;
        else if (riskPct >= kUnrestThreshold) want = RevoltStage::Unrest;

        Revolt* existing = nullptr;
        for (auto& r : st.revolts)
            if (r.system == sys.id) existing = &r;

        if (want == RevoltStage::Calm) {
            // 风险回落：阶段下降（需要连续平静才完全解除）
            if (existing != nullptr) {
                if (static_cast<int>(existing->stage) <= static_cast<int>(RevoltStage::Unrest)) {
                    // 已降到最低档：直接移除记录，避免残留「平静」条目
                    const u32 sid = sys.id;
                    st.revolts.erase(std::remove_if(st.revolts.begin(), st.revolts.end(),
                                                    [&](const Revolt& r) { return r.system == sid; }),
                                     st.revolts.end());
                } else {
                    existing->stage = static_cast<RevoltStage>(static_cast<int>(existing->stage) - 1);
                    existing->severity = existing->severity * Fixed::pct(70);
                }
            }
            continue;
        }
        if (existing == nullptr) {
            Revolt r;
            r.system = sys.id;
            r.owner = sys.owner;
            r.stage = want;
            r.severity = risk;
            r.sinceTick = st.tick;
            r.leader = strongestFaction(*st.empire(sys.owner));
            st.revolts.push_back(r);
            existing = &st.revolts.back();
            if (st.empire(sys.owner) != nullptr && st.empire(sys.owner)->isPlayer) {
                st.logEvent(LogPhase::Domestic, "revolt.stage",
                            sys.name + " 进入【" + std::string(revoltStageName(want)) + "】：" +
                                revoltStageDesc(want),
                            sys.owner, risk);
            }
        } else {
            // 阶段只升不降（除上面的平静回落路径）
            if (static_cast<int>(want) > static_cast<int>(existing->stage))
                existing->stage = want;
            existing->severity = fxMax(existing->severity, risk);
            existing->owner = sys.owner;
        }
    }

    // ---- 2) 割据：持续足够久就真的脱离 ----
    for (auto& r : st.revolts) {
        if (r.stage != RevoltStage::Secession) continue;
        if (st.tick < r.sinceTick + kSecessionDelay) continue;
        SystemNode* sys = st.system(r.system);
        Empire* owner = st.empire(r.owner);
        if (sys == nullptr || owner == nullptr || !owner->alive) continue;

        // 优先倒向接壤邻国；没有邻国则自立
        u32 receiver = findReceiver(st, r.system, r.owner);
        if (receiver != kNoEmpire) {
            Empire* rec = st.empire(receiver);
            if (rec != nullptr && rec->alive) {
                sys->owner = receiver;
                owner->systems.erase(std::remove(owner->systems.begin(), owner->systems.end(), r.system),
                                     owner->systems.end());
                rec->systems.push_back(r.system);
                for (u32 pid : sys->planets) {
                    Planet* p = st.planet(pid);
                    if (p != nullptr && p->owner == owner->id) p->owner = receiver;
                }
                st.logEvent(LogPhase::Combat, kLogWar,
                            sys->name + " 脱离 " + owner->name + "，倒向 " + rec->name, receiver);
                continue;
            }
        }
        // 自立：从原帝国分裂出一个新国家
        if (st.empires.size() >= kMaxEmpires) {
            // 帝国数已达上限：改为「无主」
            sys->owner = kNoEmpire;
            owner->systems.erase(std::remove(owner->systems.begin(), owner->systems.end(), r.system),
                                 owner->systems.end());
            for (u32 pid : sys->planets) {
                Planet* p = st.planet(pid);
                if (p != nullptr && p->owner == owner->id) p->owner = kNoEmpire;
            }
            st.logEvent(LogPhase::Combat, kLogWar,
                        sys->name + " 宣布独立，成为无主星系（帝国数已达上限）", r.owner);
            continue;
        }
        Empire rebel;
        rebel.id = static_cast<u32>(st.empires.size());
        rebel.name = owner->name.substr(0, 2) + "自由邦";
        rebel.species = owner->species;
        rebel.ethics = owner->ethics;
        rebel.civics = owner->civics;
        rebel.government = owner->government;
        rebel.alive = true;
        rebel.capital = r.system;
        rebel.treasury = Fixed(20000);
        rebel.military = owner->military * Fixed::pct(20);
        rebel.economy = owner->economy * Fixed::pct(20);
        rebel.stability = Fixed::pct(60);
        rebel.systems = {r.system};
        sys->owner = rebel.id;
        owner->systems.erase(std::remove(owner->systems.begin(), owner->systems.end(), r.system),
                             owner->systems.end());
        for (u32 pid : sys->planets) {
            Planet* p = st.planet(pid);
            if (p != nullptr && p->owner == owner->id) p->owner = rebel.id;
        }
        const u32 rebelId = rebel.id;
        st.empires.push_back(std::move(rebel));
        // 独立即开战
        declareWar(st, rebelId, r.owner, true);
        st.logEvent(LogPhase::Combat, kLogWar,
                    sys->name + " 宣布独立，成立【" + st.empires[rebelId].name + "】", rebelId);
    }

    // ---- 3) 清理不再有主的动乱记录 ----
    st.revolts.erase(std::remove_if(st.revolts.begin(), st.revolts.end(),
                                    [&](const Revolt& r) {
                                        const SystemNode* sys = st.system(r.system);
                                        return sys == nullptr || sys->owner == kNoEmpire ||
                                               sys->owner != r.owner;
                                    }),
                     st.revolts.end());
}

bool suppressRevolt(GameState& st, u32 empire, u32 system, std::string* msg) {
    Empire* e = st.empire(empire);
    SystemNode* sys = st.system(system);
    if (e == nullptr || sys == nullptr || sys->owner != empire) {
        if (msg) *msg = "只能镇压自己的星系";
        return false;
    }
    Revolt* r = nullptr;
    for (auto& x : st.revolts)
        if (x.system == system) r = &x;
    if (r == nullptr || r->stage == RevoltStage::Calm) {
        if (msg) *msg = "该星系没有动乱";
        return false;
    }
    const Fixed cost = Fixed(20000);
    const Fixed troops = Fixed(300);
    if (e->treasury.rawValue() < cost.rawValue()) {
        if (msg) *msg = "国库不足：镇压需要 " + fixedStr(cost, 0) + " cr";
        return false;
    }
    if (e->military.rawValue() < troops.rawValue()) {
        if (msg) *msg = "军力不足：镇压需要 " + fixedStr(troops, 0) + " 军力";
        return false;
    }
    e->treasury -= cost;
    e->military -= troops;
    if (e->isPlayer) st.market.margin.cash = e->treasury;
    // 镇压立即降低阶段并压下严重度
    r->suppressed += cost;
    r->severity = r->severity * Fixed::pct(40);
    if (r->severity.rawValue() < Fixed::pct(40).rawValue() && r->stage == RevoltStage::Secession)
        r->stage = RevoltStage::Revolt;
    // 镇压有代价：民怨短期上升（镇压本身激化矛盾）
    e->domestic.unrest = fxClamp(e->domestic.unrest + Fixed::pct(3), Fixed(0), Fixed(1));
    for (auto& f : e->domestic.factions)
        if (f.kind == FactionKind::Populist || f.kind == FactionKind::Labor)
            f.satisfaction = fxClamp(f.satisfaction - Fixed::pct(6), Fixed(0), Fixed(1));
    if (msg)
        *msg = "已镇压 " + sys->name + " 的动乱（" + fixedStr(cost, 0) + " cr，军力 -" +
               fixedStr(troops, 0) + "；民怨 +3%，民粹/劳工满意度 -6%）";
    st.logEvent(LogPhase::Domestic, "revolt.suppress",
                e->name + " 镇压 " + sys->name + " 的动乱", empire);
    return true;
}

std::string factionStruggleReport(const GameState& st, u32 empire) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return "非法主体\n";
    if (e->domestic.factions.empty()) return "  （该政体没有派系）\n";
    std::string out;
    TextTable t;
    t.header({"派系", "影响力", "满意度", "诉求压力", "态势"});
    // 按影响力排序
    std::vector<const Faction*> fs;
    for (const auto& f : e->domestic.factions) fs.push_back(&f);
    std::sort(fs.begin(), fs.end(), [](const Faction* a, const Faction* b) {
        return a->influence.rawValue() > b->influence.rawValue();
    });
    for (const Faction* f : fs) {
        std::string state;
        if (f->satisfaction.rawValue() < Fixed::pct(25).rawValue() &&
            f->influence.rawValue() > Fixed::pct(25).rawValue())
            state = "**准备逼宫**";
        else if (f->satisfaction.rawValue() < Fixed::pct(35).rawValue()) state = "不满";
        else if (f->influence.rawValue() > Fixed::pct(35).rawValue()) state = "当权";
        else state = "平常";
        t.row({std::string(factionKindName(f->kind)),
               fixedStrPlain(f->influence * Fixed(100), 0) + "%",
               fixedStrPlain(f->satisfaction * Fixed(100), 0) + "%",
               fixedStrPlain(f->demandPressure, 1), state});
    }
    out += t.render();
    out += "  派系的**影响力 × 不满**决定它闹事的资本；影响力最高且满意度极低时会发出最后通牒。\n";
    return out;
}

bool factionUltimatumPending(const GameState& st, u32 empire, FactionKind* which) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return false;
    Fixed bestScore = Fixed(0);
    FactionKind best = FactionKind::Count;
    for (const auto& f : e->domestic.factions) {
        if (f.influence.rawValue() < Fixed::pct(kUltimatumInfluencePct).rawValue()) continue;
        if (f.satisfaction.rawValue() > Fixed::pct(kUltimatumSatisfactionPct).rawValue()) continue;
        Fixed score = f.influence * (Fixed(1) - f.satisfaction);
        if (score.rawValue() > bestScore.rawValue()) {
            bestScore = score;
            best = f.kind;
        }
    }
    if (best == FactionKind::Count) return false;
    if (which != nullptr) *which = best;
    return true;
}

bool answerUltimatum(GameState& st, u32 empire, bool concede, std::string* msg) {
    Empire* e = st.empire(empire);
    if (e == nullptr) {
        if (msg) *msg = "非法主体";
        return false;
    }
    FactionKind which = FactionKind::Count;
    if (!factionUltimatumPending(st, empire, &which)) {
        if (msg) *msg = "当前没有派系发出最后通牒";
        return false;
    }
    Faction* f = factionOf(*e, which);
    if (f == nullptr) {
        if (msg) *msg = "找不到该派系";
        return false;
    }
    if (concede) {
        // 让步：满足诉求，代价是资源与政策偏移
        const Fixed cost = Fixed(30000);
        if (e->treasury.rawValue() < cost.rawValue()) {
            if (msg) *msg = "国库不足以满足诉求（需要 " + fixedStr(cost, 0) + " cr）";
            return false;
        }
        e->treasury -= cost;
        if (e->isPlayer) st.market.margin.cash = e->treasury;
        f->satisfaction = fxClamp(f->satisfaction + Fixed::pct(35), Fixed(0), Fixed(1));
        f->demandPressure = Fixed(0);
        f->influence = fxClamp(f->influence + Fixed::pct(8), Fixed(0), Fixed(1));
        // 其他派系不满：资源是零和的
        for (auto& o : e->domestic.factions) {
            if (o.kind == which) continue;
            o.satisfaction = fxClamp(o.satisfaction - Fixed::pct(10), Fixed(0), Fixed(1));
        }
        if (msg)
            *msg = "已满足【" + std::string(factionKindName(which)) + "】的诉求（" +
                   fixedStr(cost, 0) + " cr；其满意度 +35%，但其他派系 -10%）";
    } else {
        // 拒绝：满意度暴跌，动乱风险上升，政变风险上升
        f->satisfaction = fxClamp(f->satisfaction - Fixed::pct(25), Fixed(0), Fixed(1));
        f->demandPressure += Fixed(1);
        e->domestic.unrest = fxClamp(e->domestic.unrest + Fixed::pct(8), Fixed(0), Fixed(1));
        e->stability = fxClamp(e->stability - Fixed::pct(6), Fixed(0), Fixed(1));
        e->domestic.coupRisk = fxClamp(e->domestic.coupRisk + Fixed::pct(12), Fixed(0), Fixed(1));
        if (msg)
            *msg = "已拒绝【" + std::string(factionKindName(which)) +
                   "】的最后通牒（其满意度 -25%，民怨 +8%，稳定 -6%，政变风险 +12%）";
    }
    st.logEvent(LogPhase::Domestic, "faction.ultimatum",
                e->name + (concede ? " 向 " : " 拒绝 ") + std::string(factionKindName(which)) +
                    (concede ? " 让步" : " 的诉求"),
                empire);
    return true;
}

std::string revoltReport(const GameState& st, u32 empire) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return "非法主体\n";
    std::string out;
    std::vector<const Revolt*> mine;
    for (const auto& r : st.revolts)
        if (r.owner == empire) mine.push_back(&r);
    if (mine.empty()) {
        out += "  （境内没有动乱星系）\n";
    } else {
        TextTable t;
        t.header({"星系", "阶段", "严重度", "主导派系", "持续", "说明"});
        for (const Revolt* r : mine) {
            const SystemNode* sys = st.system(r->system);
            t.row({(sys ? sys->name : std::string("?")) + " (#" + std::to_string(r->system) + ")",
                   std::string(revoltStageName(r->stage)),
                   fixedStrPlain(r->severity * Fixed(100), 0) + "%",
                   std::string(factionKindName(r->leader)),
                   std::to_string(st.tick - r->sinceTick) + " 季",
                   revoltStageDesc(r->stage)});
        }
        out += t.render();
        out += "  用 `greyfall revolt --suppress <星系>` 镇压（20,000 cr + 300 军力）。\n";
        out += "  割据状态持续 8 季未平息，该星系将脱离控制。\n";
    }
    // 全境风险最高的几个星系
    struct RiskRow {
        u32 sys;
        Fixed risk;
    };
    std::vector<RiskRow> rows;
    for (u32 sid : e->systems) {
        Fixed r = revoltRiskOf(st, sid);
        if (r.rawValue() < Fixed::pct(35).rawValue()) continue;
        rows.push_back({sid, r});
    }
    std::sort(rows.begin(), rows.end(),
              [](const RiskRow& a, const RiskRow& b) { return a.risk.rawValue() > b.risk.rawValue(); });
    if (!rows.empty()) {
        out += "\n  风险最高的星系：\n";
        for (std::size_t i = 0; i < rows.size() && i < 5; ++i) {
            const SystemNode* sys = st.system(rows[i].sys);
            out += "    · " + (sys ? sys->name : std::string("?")) + "  风险 " +
                   fixedStrPlain(rows[i].risk * Fixed(100), 0) + "%\n";
        }
    }
    return out;
}

}  // namespace gf
