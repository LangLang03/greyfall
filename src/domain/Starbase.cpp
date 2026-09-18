#include "domain/Starbase.h"

#include <algorithm>
#include <string>

#include "util/TextTable.h"
#include "core/GameState.h"
#include "domain/Empire.h"
#include "domain/Fleet.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {
namespace {

struct TierRow {
    StarbaseTier tier;
    i64 credits;
    i64 alloys;
    i64 defense;
    i64 supplyPct;
};

constexpr TierRow kTiers[] = {
    {StarbaseTier::Outpost, 8000, 60, 120, 10},
    {StarbaseTier::Starport, 22000, 180, 400, 25},
    {StarbaseTier::Fortress, 60000, 520, 1100, 45},
    {StarbaseTier::Citadel, 150000, 1400, 2600, 70},
    {StarbaseTier::StarFortress, 380000, 3600, 6000, 100},
};
static_assert(sizeof(kTiers) / sizeof(kTiers[0]) == static_cast<std::size_t>(StarbaseTier::Count));

const TierRow& rowOf(StarbaseTier t) {
    int i = static_cast<int>(t);
    if (i < 0) i = 0;
    if (i >= static_cast<int>(StarbaseTier::Count)) i = static_cast<int>(StarbaseTier::Count) - 1;
    return kTiers[i];
}

}  // namespace

std::string_view starbaseTierName(StarbaseTier t) {
    switch (t) {
        case StarbaseTier::Outpost: return "前哨站";
        case StarbaseTier::Starport: return "星港";
        case StarbaseTier::Fortress: return "堡垒";
        case StarbaseTier::Citadel: return "要塞";
        case StarbaseTier::StarFortress: return "星际要塞";
        case StarbaseTier::Count: break;
    }
    return "?";
}

std::string starbaseTierDesc(StarbaseTier t) {
    switch (t) {
        case StarbaseTier::Outpost:
            return "最低限度的存在：宣示主权，无实质防御。但它是后续升级的基础。";
        case StarbaseTier::Starport:
            return "可为本国舰队提供补给与维修，敌舰队进入时需要先突破它。";
        case StarbaseTier::Fortress:
            return "具备实质防御工事，能独立抵挡小规模袭扰。";
        case StarbaseTier::Citadel:
            return "区域防御核心：敌军必须投入主力才能攻克。";
        case StarbaseTier::StarFortress:
            return "几乎不可攻克。建造周期与成本都极为高昂，但它是真正的国门。";
        default: break;
    }
    return "";
}

i64 starbaseUpgradeCredits(StarbaseTier t) { return rowOf(t).credits; }
i64 starbaseUpgradeAlloys(StarbaseTier t) { return rowOf(t).alloys; }
Fixed starbaseDefense(StarbaseTier t) { return Fixed(rowOf(t).defense); }
Fixed starbaseSupplyBonus(StarbaseTier t) { return Fixed::pct(rowOf(t).supplyPct); }

const Starbase* starbaseAt(const GameState& st, u32 system) {
    for (const auto& b : st.starbases)
        if (b.system == system) return &b;
    return nullptr;
}

Fixed starbaseDefenseOf(const GameState& st, u32 empire) {
    Fixed acc = Fixed(0);
    for (const auto& b : st.starbases)
        if (b.owner == empire) acc += starbaseDefense(b.tier);
    return acc;
}

Fixed starbaseSupplyOf(const GameState& st, u32 empire) {
    Fixed best = Fixed(0);
    for (const auto& b : st.starbases)
        if (b.owner == empire) {
            Fixed v = starbaseSupplyBonus(b.tier);
            if (v.rawValue() > best.rawValue()) best = v;
        }
    return best;
}

bool starbaseFound(GameState& st, u32 empire, u32 system, std::string* msg) {
    return startStarbaseProject(st, empire, system, false, msg);
}

bool starbaseUpgrade(GameState& st, u32 empire, u32 system, std::string* msg) {
    return startStarbaseProject(st, empire, system, true, msg);
}

bool starbaseDismantle(GameState& st, u32 empire, u32 system, std::string* msg) {
    const auto* e = st.empire(empire);
    if (e && hasDevelopment(*e, ProjectKind::Starbase, system)) {
        if (msg) *msg = "请先通过 queue 取消基地施工项目";
        return false;
    }
    for (auto it = st.starbases.begin(); it != st.starbases.end(); ++it) {
        if (it->system != system || it->owner != empire) continue;
        st.starbases.erase(it);
        if (msg) *msg = "已拆除该星系的恒星基地（不返还资源）";
        return true;
    }
    if (msg) *msg = "该星系没有你的恒星基地";
    return false;
}

void starbasePhase(GameState& st) {
    for (auto& b : st.starbases) {
        const SystemNode* sys = st.system(b.system);
        if (sys == nullptr) continue;
        if (sys->owner == b.owner) continue;
        // 星系易主：基地随之易主并降一级（被攻占的工事需要修复/降级）。
        // 已被摧毁的基地（星系无主）直接移除。
        if (sys->owner == kNoEmpire) {
            b.owner = kNoEmpire;
            b.tier = StarbaseTier::Outpost;
            continue;
        }
        Empire* newOwner = st.empire(sys->owner);
        if (newOwner == nullptr || !newOwner->alive) continue;
        if (b.owner != kNoEmpire) b.siegesSurvived += 1;
        b.owner = sys->owner;
        if (static_cast<int>(b.tier) > 0)
            b.tier = static_cast<StarbaseTier>(static_cast<int>(b.tier) - 1);
        st.logEvent(LogPhase::Combat, kLogWar,
                    (sys ? sys->name : std::string("?")) + " 的恒星基地随星系易主，降级为" +
                        std::string(starbaseTierName(b.tier)),
                    sys->owner);
    }
    // 清理无主基地
    st.starbases.erase(std::remove_if(st.starbases.begin(), st.starbases.end(),
                                      [](const Starbase& b) { return b.owner == kNoEmpire; }),
                       st.starbases.end());
}

std::string starbaseReport(const GameState& st, u32 empire) {
    std::string out;
    std::vector<const Starbase*> mine;
    for (const auto& b : st.starbases)
        if (b.owner == empire) mine.push_back(&b);
    if (mine.empty()) {
        return "  （尚无恒星基地。用 `greyfall base --found <星系>` 建立前哨站）\n";
    }
    TextTable t;
    t.header({"星系", "等级", "防御", "补给加成", "建立于", "被围攻"});
    for (const Starbase* b : mine) {
        const SystemNode* sys = st.system(b->system);
        t.row({(sys ? sys->name : std::string("?")) + " (#" + std::to_string(b->system) + ")",
               std::string(starbaseTierName(b->tier)), fixedStr(starbaseDefense(b->tier), 0),
               "+" + fixedStrPlain(starbaseSupplyBonus(b->tier) * Fixed(100), 0) + "%",
               std::to_string(b->builtTick), std::to_string(b->siegesSurvived)});
    }
    out += t.render();
    out += "  合计防御 " + fixedStr(starbaseDefenseOf(st, empire), 0) + "（加入防守方战力）\n";
    return out;
}

std::string starbaseBrief(const GameState& st, u32 system) {
    const Starbase* b = starbaseAt(st, system);
    if (b == nullptr) return "无";
    const Empire* e = st.empire(b->owner);
    return std::string(starbaseTierName(b->tier)) + "（" + (e ? e->name : std::string("无主")) +
           "，防御 " + fixedStr(starbaseDefense(b->tier), 0) + "）";
}

}  // namespace gf
