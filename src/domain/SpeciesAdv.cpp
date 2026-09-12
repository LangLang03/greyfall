#include "domain/SpeciesAdv.h"

#include <algorithm>
#include <string>

#include "cli/TextTable.h"
#include "core/GameState.h"
#include "domain/Empire.h"
#include "domain/Planet.h"
#include "domain/Species.h"
#include "gen/EmpireGen.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {
namespace {

/// 太空生物的基础种群与每季增长率
constexpr i64 kFaunaBasePop = 120;
constexpr i64 kFaunaGrowthPct = 4;

/// 施加一次压力的成本与累积量
constexpr i64 kPressureCost = 8000;
constexpr i64 kPressureGainPct = 18;
/// 压力每季衰减
constexpr i64 kPressureDecayPct = 5;

struct FaunaRow {
    FaunaKind kind;
    i64 threat;
    int commodity;
    i64 amount;
};

constexpr FaunaRow kFaunaRows[] = {
    {FaunaKind::StarJelly, 300, static_cast<int>(Commodity::Food), 400},
    {FaunaKind::VoidWhale, 1400, static_cast<int>(Commodity::Luxury), 180},
    {FaunaKind::CrystalSwarm, 900, static_cast<int>(Commodity::Supermaterials), 120},
    {FaunaKind::RiftStalker, 2200, static_cast<int>(Commodity::Relics), 60},
};
static_assert(sizeof(kFaunaRows) / sizeof(kFaunaRows[0]) == static_cast<std::size_t>(FaunaKind::Count));

const FaunaRow& faunaRow(FaunaKind k) {
    int i = static_cast<int>(k);
    if (i < 0) i = 0;
    if (i >= static_cast<int>(FaunaKind::Count)) i = static_cast<int>(FaunaKind::Count) - 1;
    return kFaunaRows[i];
}

}  // namespace

// ---------------------------------------------------------------------------
// 1) 太空生物
// ---------------------------------------------------------------------------

std::string_view faunaName(FaunaKind k) {
    switch (k) {
        case FaunaKind::StarJelly: return "海星";
        case FaunaKind::VoidWhale: return "虚空鲸";
        case FaunaKind::CrystalSwarm: return "晶簇虫群";
        case FaunaKind::RiftStalker: return "裂隙潜行者";
        case FaunaKind::Count: break;
    }
    return "?";
}

std::string faunaDesc(FaunaKind k) {
    switch (k) {
        case FaunaKind::StarJelly:
            return "群居的胶质生物，随恒星风漂移。性情温顺、繁殖极快 —— "
                   "肉与胶质可加工成**海星罐头**，是廉价而稳定的食品来源。";
        case FaunaKind::VoidWhale:
            return "在星系间迁徙的巨兽。油脂与骨材是奢侈品市场的抢手货，"
                   "但它们皮糙肉厚，需要成规模的舰队才能猎杀。";
        case FaunaKind::CrystalSwarm:
            return "硅基虫群，以陨石为食。虫壳可提炼超材料，"
                   "但虫群会主动攻击靠近的采矿设施。";
        case FaunaKind::RiftStalker:
            return "潜伏在裂隙中的掠食者。极难猎杀，"
                   "但遗骸是珍贵文物级材料 —— 且它本身会袭击过往舰队。";
        default: break;
    }
    return "";
}

Fixed faunaThreat(FaunaKind k) { return Fixed(faunaRow(k).threat); }
bool faunaLoot(FaunaKind k, int* commodity, i64* amount) {
    if (commodity != nullptr) *commodity = faunaRow(k).commodity;
    if (amount != nullptr) *amount = faunaRow(k).amount;
    return true;
}

const FaunaHerd* faunaAt(const GameState& st, u32 system) {
    for (const auto& f : st.fauna)
        if (f.system == system) return &f;
    return nullptr;
}

void faunaPhase(GameState& st) {
    if (st.tick % 4 != 0) return;
    for (auto& f : st.fauna) {
        // 种群自然增长（有上限，避免无限膨胀）
        i64 growth = f.population * kFaunaGrowthPct / 100;
        if (growth < 1) growth = 1;
        f.population += growth;
        if (f.population > 2000) f.population = 2000;
    }
    // 已灭绝的群移除
    st.fauna.erase(std::remove_if(st.fauna.begin(), st.fauna.end(),
                                  [](const FaunaHerd& f) { return f.population <= 0; }),
                   st.fauna.end());
}

bool huntFauna(GameState& st, u32 empire, u32 system, std::string* msg) {
    Empire* e = st.empire(empire);
    if (e == nullptr) {
        if (msg) *msg = "非法主体";
        return false;
    }
    FaunaHerd* herd = nullptr;
    for (auto& f : st.fauna)
        if (f.system == system) herd = &f;
    if (herd == nullptr) {
        if (msg) *msg = "该星系没有太空生物";
        return false;
    }
    const Fixed threat = faunaThreat(herd->kind);
    if (e->military.rawValue() < threat.rawValue()) {
        if (msg)
            *msg = std::string(faunaName(herd->kind)) + " 需要至少 " + fixedStr(threat, 0) +
                   " 军力才能猎杀（当前 " + fixedStr(e->military, 0) + "）";
        return false;
    }
    // 猎杀代价：军力损耗与经费
    const Fixed cost = Fixed(6000);
    const Fixed troops = threat * Fixed::pct(15);
    if (e->treasury.rawValue() < cost.rawValue()) {
        if (msg) *msg = "国库不足：狩猎行动需要 " + fixedStr(cost, 0) + " cr";
        return false;
    }
    e->treasury -= cost;
    e->military = fxMax(Fixed(0), e->military - troops);
    if (e->isPlayer) st.market.margin.cash = e->treasury;

    int commodity = 0;
    i64 amount = 0;
    (void)faunaLoot(herd->kind, &commodity, &amount);
    // 捕获量随种群规模缩放
    i64 take = amount * std::min<i64>(herd->population, 400) / 400;
    if (take < 1) take = 1;
    e->stock[static_cast<std::size_t>(commodity)] += Fixed(take);
    herd->population -= take / 4;
    if (herd->population < 0) herd->population = 0;
    herd->lastHunted = st.tick;

    if (msg)
        *msg = std::string("猎杀 ") + std::string(faunaName(herd->kind)) + "：获得 " +
               std::to_string(take) + " 单位" + std::string(commodityName(commodity)) +
               "（军力 -" + fixedStr(troops, 0) + "，经费 -" + fixedStr(cost, 0) + " cr）";
    st.logEvent(LogPhase::Economy, "fauna.hunt",
                e->name + " 猎杀 " + std::string(faunaName(herd->kind)), empire);
    return true;
}

bool canStarJelly(GameState& st, u32 empire, i64 batches, std::string* msg) {
    return startProcessing(st, empire, batches, msg);
}

std::string faunaReport(const GameState& st, u32 empire) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return "非法主体\n";
    // 只显示本国领土及其相邻星系内的生物
    std::vector<const FaunaHerd*> visible;
    for (const auto& f : st.fauna) {
        const SystemNode* sys = st.system(f.system);
        if (sys == nullptr) continue;
        bool near = sys->owner == empire;
        if (!near)
            for (u32 nx : sys->links) {
                const SystemNode* n = st.system(nx);
                if (n != nullptr && n->owner == empire) near = true;
            }
        if (near) visible.push_back(&f);
    }
    if (visible.empty()) return "  （附近没有发现太空生物）\n";
    std::string out;
    TextTable t;
    t.header({"星系", "生物", "种群", "所需军力", "可获"});
    for (const FaunaHerd* f : visible) {
        const SystemNode* sys = st.system(f->system);
        int c = 0;
        i64 a = 0;
        (void)faunaLoot(f->kind, &c, &a);
        t.row({(sys ? sys->name : std::string("?")) + " (#" + std::to_string(f->system) + ")",
               std::string(faunaName(f->kind)), std::to_string(f->population),
               fixedStr(faunaThreat(f->kind), 0),
               std::to_string(a) + " " + std::string(commodityName(c))});
    }
    out += t.render();
    out += "  用 `greyfall species --hunt <星系>` 猎杀；海星可用 `--can <批数>` 加工成罐头。\n";
    return out;
}

// ---------------------------------------------------------------------------
// 2) 奴役
// ---------------------------------------------------------------------------

std::string_view laborPolicyName(LaborPolicy p) {
    switch (p) {
        case LaborPolicy::Free: return "自由民";
        case LaborPolicy::CasteSystem: return "种姓制";
        case LaborPolicy::Chattel: return "蓄奴制";
        case LaborPolicy::Count: break;
    }
    return "?";
}

std::string laborPolicyDesc(LaborPolicy p) {
    switch (p) {
        case LaborPolicy::Free:
            return "所有人口享有同等权利。无产出加成，但也没有额外的民怨与外交代价。";
        case LaborPolicy::CasteSystem:
            return "以出身划分阶层。产出 +15%，民怨目标 +8%，合法性 -5%。"
                   "第三方的观感会缓慢恶化。";
        case LaborPolicy::Chattel:
            return "蓄奴制。产出 +30%，民怨目标 +20%，合法性 -12%。"
                   "**全体第三方观感持续下降**，人道派系会激烈反对。";
        default: break;
    }
    return "";
}

Fixed laborOutputMultiplier(LaborPolicy p) {
    switch (p) {
        case LaborPolicy::Free: return Fixed(1);
        case LaborPolicy::CasteSystem: return Fixed(1) + Fixed::pct(15);
        case LaborPolicy::Chattel: return Fixed(1) + Fixed::pct(30);
        case LaborPolicy::Count: break;
    }
    return Fixed(1);
}

Fixed laborUnrestTarget(LaborPolicy p) {
    switch (p) {
        case LaborPolicy::Free: return Fixed(0);
        case LaborPolicy::CasteSystem: return Fixed::pct(8);
        case LaborPolicy::Chattel: return Fixed::pct(20);
        case LaborPolicy::Count: break;
    }
    return Fixed(0);
}

bool setLaborPolicy(GameState& st, u32 empire, LaborPolicy p, std::string* msg) {
    Empire* e = st.empire(empire);
    if (e == nullptr) {
        if (msg) *msg = "非法主体";
        return false;
    }
    if (e->labor == p) {
        if (msg) *msg = std::string("当前已是【") + std::string(laborPolicyName(p)) + "】";
        return false;
    }
    const Fixed cost = Fixed(20000);
    if (e->treasury.rawValue() < cost.rawValue()) {
        if (msg) *msg = "国库不足：制度改革需要 " + fixedStr(cost, 0) + " cr";
        return false;
    }
    e->treasury -= cost;
    if (e->isPlayer) st.market.margin.cash = e->treasury;
    const LaborPolicy old = e->labor;
    e->labor = p;
    // 制度切换立即引发反应
    if (p != LaborPolicy::Free) {
        e->domestic.unrest = fxClamp(e->domestic.unrest + Fixed::pct(6), Fixed(0), Fixed(1));
        for (auto& f : e->domestic.factions) {
            if (f.kind == FactionKind::Labor || f.kind == FactionKind::Populist)
                f.satisfaction = fxClamp(f.satisfaction - Fixed::pct(12), Fixed(0), Fixed(1));
            if (f.kind == FactionKind::Nobility || f.kind == FactionKind::Merchant)
                f.satisfaction = fxClamp(f.satisfaction + Fixed::pct(8), Fixed(0), Fixed(1));
        }
    }
    if (p == LaborPolicy::Chattel) {
        // 蓄奴会招致全体第三方谴责
        for (auto& o : st.empires) {
            if (o.id == empire || !o.alive) continue;
            o.addOpinion(empire, Fixed::pct(-15));
        }
    }
    if (msg)
        *msg = std::string("劳役制度由【") + std::string(laborPolicyName(old)) + "】改为【" +
               std::string(laborPolicyName(p)) + "】（产出 ×" +
               fixedStrPlain(laborOutputMultiplier(p), 2) + "）";
    st.logEvent(LogPhase::Domestic, "labor.change", e->name + " 劳役制度变更为" +
                                                         std::string(laborPolicyName(p)),
                empire);
    return true;
}

void laborPhase(GameState& st) {
    if (st.tick % 8 != 0) return;
    for (auto& e : st.empires) {
        if (!e.alive || e.labor == LaborPolicy::Free) continue;
        // 蓄奴制的持续代价：第三方观感缓慢下降
        const Fixed drift = (e.labor == LaborPolicy::Chattel) ? Fixed::pct(-2) : Fixed::pct(-1);
        for (auto& o : st.empires) {
            if (o.id == e.id || !o.alive) continue;
            o.addOpinion(e.id, drift);
        }
    }
}

// ---------------------------------------------------------------------------
// 3) 外交施压
// ---------------------------------------------------------------------------

std::string_view pressureKindName(PressureKind k) {
    switch (k) {
        case PressureKind::Economic: return "经济施压";
        case PressureKind::Military: return "军事施压";
        case PressureKind::Diplomatic: return "外交孤立";
        case PressureKind::Count: break;
    }
    return "?";
}

Fixed pressureOn(const GameState& st, u32 target, u32 source) {
    const Empire* t = st.empire(target);
    if (t == nullptr || source >= kMaxEmpires) return Fixed(0);
    return t->pressure[source];
}

bool applyPressure(GameState& st, u32 empire, u32 target, PressureKind k, std::string* msg) {
    Empire* e = st.empire(empire);
    Empire* t = st.empire(target);
    if (e == nullptr || t == nullptr || empire == target) {
        if (msg) *msg = "非法主体";
        return false;
    }
    const Fixed cost = Fixed(kPressureCost);
    const Fixed infl = Fixed(150);
    if (e->treasury.rawValue() < cost.rawValue()) {
        if (msg) *msg = "国库不足：施压需要 " + fixedStr(cost, 0) + " cr";
        return false;
    }
    if (e->influence.rawValue() < infl.rawValue()) {
        if (msg) *msg = "影响力不足：施压需要 " + fixedStr(infl, 0);
        return false;
    }
    e->treasury -= cost;
    e->influence -= infl;
    if (e->isPlayer) st.market.margin.cash = e->treasury;

    Fixed gain = Fixed::pct(kPressureGainPct);
    // 军力优势让施压更有效
    if (e->military.rawValue() > t->military.rawValue()) {
        Fixed edge = (e->military - t->military) / fxMax(t->military, Fixed(1));
        gain += fxMin(edge, Fixed(1)) * Fixed::pct(10);
    }
    t->pressure[empire] = fxClamp(t->pressure[empire] + gain, Fixed(0), Fixed(1));
    // 施压必然损害关系
    t->addOpinion(empire, Fixed::pct(-8));
    e->addOpinion(target, Fixed::pct(-3));
    if (msg)
        *msg = std::string("对 ") + t->name + " 实施【" + std::string(pressureKindName(k)) +
               "】：压力 +" + fixedStrPlain(gain * Fixed(100), 0) + "%（现 " +
               fixedStrPlain(t->pressure[empire] * Fixed(100), 0) + "%），对方观感 -8%";
    st.logEvent(LogPhase::Model, "diplo.pressure",
                e->name + " 对 " + t->name + " 实施" + std::string(pressureKindName(k)), empire);
    return true;
}

void pressurePhase(GameState& st) {
    for (auto& t : st.empires) {
        if (!t.alive) continue;
        for (u32 src = 0; src < kMaxEmpires; ++src) {
            Fixed& p = t.pressure[src];
            if (p.rawValue() <= 0) continue;
            const Empire* s = st.empire(src);
            if (s == nullptr || !s->alive) {
                p = Fixed(0);
                continue;
            }
            // 压力 ≥ 60%：对方被迫让步 —— 一次性转移资源，压力回落
            if (p.rawValue() >= Fixed::pct(60).rawValue() && st.tick % 10 == 0) {
                Fixed tribute = fxMin(t.treasury * Fixed::pct(10), Fixed(50000));
                if (tribute.rawValue() > 0) {
                    t.treasury -= tribute;
                    Empire* se = st.empire(src);
                    if (se != nullptr) se->treasury += tribute;
                    if (t.isPlayer) st.market.margin.cash = t.treasury;
                    st.logEvent(LogPhase::Model, "diplo.concede",
                                t.name + " 在持续施压下让步，向 " + s->name + " 支付 " +
                                    fixedStr(tribute, 0) + " cr",
                                src);
                }
                // 让步后压力显著回落
                p = p * Fixed::pct(40);
            } else {
                p = fxClamp(p - Fixed::pct(kPressureDecayPct), Fixed(0), Fixed(1));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 4) 基因改造
// ---------------------------------------------------------------------------

std::string_view geneModName(GeneMod m) {
    switch (m) {
        case GeneMod::Hardy: return "强健";
        case GeneMod::Industrious: return "勤勉";
        case GeneMod::Erudite: return "睿智";
        case GeneMod::Resilient: return "坚韧";
        case GeneMod::Docile: return "温顺";
        case GeneMod::Count: break;
    }
    return "?";
}

std::string geneModDesc(GeneMod m) {
    switch (m) {
        case GeneMod::Hardy: return "强化代谢与免疫：人口增长 +20%。";
        case GeneMod::Industrious: return "肌肉与耐力改造：建造速率 +15%。";
        case GeneMod::Erudite: return "神经发育优化：研究速率 +15%。";
        case GeneMod::Resilient: return "应激耐受改造：稳定度 +12%。";
        case GeneMod::Docile: return "降低攻击性：民怨 -15%。";
        default: break;
    }
    return "";
}

i64 geneModCost(GeneMod m) {
    return 1500 + static_cast<i64>(m) * 300;
}

bool applyGeneMod(GameState& st, u32 empire, GeneMod m, std::string* msg) {
    return startGeneProject(st, empire, static_cast<int>(m), msg);
}

void genePhase(GameState& st) {
    // 基因改造的维护：每 12 季一笔基因库维护费
    if (st.tick % 12 != 0) return;
    for (auto& e : st.empires) {
        if (!e.alive) continue;
        int mods = 0;
        for (bool b : e.geneMods)
            if (b) ++mods;
        if (mods == 0) continue;
        const Fixed upkeep = Fixed(mods * 1200);
        e.treasury -= upkeep;
        if (e.isPlayer) st.market.margin.cash = e.treasury;
    }
}

std::string speciesReport(const GameState& st, u32 empire) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return "非法主体\n";
    std::string out;
    TextTable t;
    t.header({"项目", "值"});
    const SpeciesInfo& si = speciesInfo(static_cast<int>(e->species));
    t.row({"种族", std::string(si.nameZh)});
    t.row({"描述", std::string(si.desc)});
    std::string traits;
    for (u8 i = 0; i < si.modCount && i < si.mods.size(); ++i) {
        const Modifier& m = si.mods[i];
        if (!traits.empty()) traits += "、";
        traits += std::string(modKindName(m.kind)) + " " +
                  (m.value.rawValue() > 0 ? "+" : "") + fixedStrPlain(m.value * Fixed(100), 0) + "%";
    }
    t.row({"种族特质", traits.empty() ? "无" : traits});
    t.row({"劳役制度", std::string(laborPolicyName(e->labor))});
    t.row({"产出倍率", "×" + fixedStrPlain(laborOutputMultiplier(e->labor), 2)});
    t.row({"民怨目标增量", "+" + fixedStrPlain(laborUnrestTarget(e->labor) * Fixed(100), 0) + "%"});
    std::string mods;
    for (int i = 0; i < static_cast<int>(GeneMod::Count); ++i) {
        if (!e->geneMods[static_cast<std::size_t>(i)]) continue;
        if (!mods.empty()) mods += "、";
        mods += std::string(geneModName(static_cast<GeneMod>(i)));
    }
    t.row({"基因改造", mods.empty() ? "（无）" : mods});
    out += t.render();
    return out;
}


// ---------------------------------------------------------------------------
// 5) 游牧国家
// ---------------------------------------------------------------------------

bool isNomadic(const GameState& st, u32 empire) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return false;
    // 种族：游牧虫群
    if (e->species < kSpeciesCount) {
        const SpeciesInfo& si = speciesInfo(static_cast<int>(e->species));
        // 以 idName 判定，避免引入额外的枚举依赖
        if (si.idName == "swarm") return true;
    }
    // 公民：游牧（注意它是 **civics** 而非 ethics —— 伦理表只有 12 项，
    // 「游牧」是公民 id 12；查错表会让整个游牧机制永远不生效）
    for (u8 cv : e->civics) {
        if (cv >= kCivicsCount) continue;
        if (civicInfo(static_cast<int>(cv)).idName == "nomadic") return true;
    }
    return false;
}

Fixed hordeMomentum(const GameState& st, u32 empire) {
    const Empire* e = st.empire(empire);
    if (e == nullptr || !isNomadic(st, empire)) return Fixed(0);
    // 群势 = 舰队数 / (舰队数 + 星系数×2)
    // 星系越多分母越大 —— 定居会稀释游牧的优势。
    Fixed fleets = Fixed(static_cast<i64>(e->fleets.size()));
    Fixed settled = Fixed(static_cast<i64>(e->systems.size())) * Fixed(2);
    Fixed denom = fleets + settled;
    if (denom.rawValue() <= 0) return Fixed(0);
    return fxClamp(fleets / denom, Fixed(0), Fixed(1));
}

Fixed nomadicMilitaryBonus(const GameState& st, u32 empire) {
    if (!isNomadic(st, empire)) return Fixed(0);
    // 群势 0.5 为基准：高于它才有加成，最高 +35%
    Fixed m = hordeMomentum(st, empire);
    return (m - Fixed::pct(50)) * Fixed::pct(70);
}

Fixed nomadicStabilityPenalty(const GameState& st, u32 empire) {
    const Empire* e = st.empire(empire);
    if (e == nullptr || !isNomadic(st, empire)) return Fixed(0);
    // 每多一个星系，稳定度目标 -3%，上限 -18%
    Fixed pen = Fixed(static_cast<i64>(e->systems.size())) * Fixed::pct(3);
    return -fxMin(pen, Fixed::pct(18));
}

bool nomadicMigrate(GameState& st, u32 empire, u32 system, std::string* msg) {
    return startMigration(st, empire, system, msg);
}

void nomadicPhase(GameState& st) {
    if (st.tick % 10 != 0) return;
    for (auto& e : st.empires) {
        if (!e.alive || !isNomadic(st, e.id)) continue;
        // 游牧的军事修正由 nomadicMilitaryBonus 实时派生，此处只处理
        // 「定居惯性」：疆域过大时游牧民会不满（派系压力上升）。
        if (e.systems.size() > e.fleets.size()) {
            e.domestic.unrest = fxClamp(e.domestic.unrest + Fixed::pct(1), Fixed(0), Fixed(1));
            for (auto& f : e.domestic.factions)
                if (f.kind == FactionKind::Military)
                    f.satisfaction = fxClamp(f.satisfaction - Fixed::pct(2), Fixed(0), Fixed(1));
        }
    }
}

std::string nomadicReport(const GameState& st, u32 empire) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return "非法主体\n";
    if (!isNomadic(st, empire)) return "  （本帝国不是游牧政体）\n";
    std::string out;
    TextTable t;
    t.header({"项目", "值"});
    const SystemNode* cap = st.system(e->capital);
    t.row({"首都", cap ? cap->name : std::string("?")});
    t.row({"舰队 / 星系", std::to_string(e->fleets.size()) + " / " + std::to_string(e->systems.size())});
    t.row({"群势", fixedStrPlain(hordeMomentum(st, empire) * Fixed(100), 0) + "%"});
    t.row({"军事加成", (nomadicMilitaryBonus(st, empire).rawValue() >= 0 ? "+" : "") +
                            fixedStrPlain(nomadicMilitaryBonus(st, empire) * Fixed(100), 0) + "%"});
    t.row({"稳定度惩罚", fixedStrPlain(nomadicStabilityPenalty(st, empire) * Fixed(100), 0) + "%"});
    out += t.render();
    out += "  「舰队即是国土」：舰队越多、疆域越少，群势越高、军事越强。\n";
    out += "  疆域超过舰队数会引发不满（每 10 季民怨 +1%，军部满意度 -2%）。\n";
    out += "  用 `greyfall species --migrate <星系>` 启动迁徙（300 影响力、10000 cr、能源 100、部件 50；4 + 跳数季）。\n";
    return out;
}

}  // namespace gf
