// 舰船设计器：新建舰体、装卸模块、改造现役舰队
#include <algorithm>
#include <string>

#include "core/GameState.h"
#include "domain/Empire.h"
#include "domain/Fleet.h"
#include "domain/Planet.h"
#include "util/Str.h"

namespace gf {
namespace {

constexpr u32 kNoDesignId = 0xFFFFFFFFu;
/// 改造一艘现役舰队的成本系数（相对新造）：模块全部更换相当于半价改装
constexpr i64 kRefitCostPct = 45;

}  // namespace

std::string_view hullRoleName(HullRole r) {
    switch (r) {
        case HullRole::Screen: return "屏卫";
        case HullRole::Striker: return "突击";
        case HullRole::Line: return "战列";
        case HullRole::Carrier: return "母舰";
        case HullRole::Stealth: return "隐匿";
        case HullRole::Siege: return "攻坚";
        case HullRole::Count: break;
    }
    return "?";
}

std::string hullRoleDesc(HullRole r) {
    switch (r) {
        case HullRole::Screen:
            return "廉价量大，用于吸收火力、掩护主力。单舰战力低，但成群时性价比最高。";
        case HullRole::Striker:
            return "火力突出、防御薄弱。适合集中突击，但扛不住持久战。";
        case HullRole::Line:
            return "攻防均衡的主力舰。编队的中坚，什么场合都能用。";
        case HullRole::Carrier:
            return "搭载舰载机，可远程投送火力。自身脆弱，需要屏卫保护。";
        case HullRole::Stealth:
            return "低可探测性，适合侦察与偷袭。正面对抗能力弱。";
        case HullRole::Siege:
            return "对恒星基地与巨构特化，攻城时无可替代，野战则笨重。";
        default: break;
    }
    return "";
}

void recomputeDesign(FleetDesign& d) {
    const HullInfo& hi = hullInfo(d.hull);
    d.firepower = hi.baseFirepower;
    d.defense = hi.baseDefense;
    d.speed = hi.baseSpeed;
    d.supplyUse = hi.baseSupply;
    d.creditCost = hi.baseCost;
    for (int c = 0; c < kCommodityCount; ++c) d.buildCost[static_cast<std::size_t>(c)] = 0;
    // 船体本身也要计价：只算模块会让「裸舰体」的设计成本为 0，
    // 于是改造到裸设计不花任何合金（实测改造成功却未扣费）。
    // 以造价的 1/5 折算为合金需求，量级与模块相当。
    d.buildCost[static_cast<std::size_t>(Commodity::Alloys)] = hi.baseCost / 5;
    for (u8 mod : d.modules) {
        const ModuleInfo& mi = moduleInfo(static_cast<int>(mod));
        switch (mi.effect) {
            case ModuleEffect::Firepower: d.firepower += mi.effectValue; break;
            case ModuleEffect::Defense: d.defense += mi.effectValue; break;
            case ModuleEffect::Speed: d.speed += mi.effectValue; break;
            case ModuleEffect::Supply: d.supplyUse += mi.effectValue; break;
            default: break;
        }
        for (int c = 0; c < kCommodityCount; ++c)
            // 注意：buildCost 是「单位数」而非定点原始值。
            // 早期这里用了 rawValue()（×1000），把 40 合金记成 40,000 ——
            // 于是造一艘护卫舰需要 80,000 合金，永远造不起。
            d.buildCost[static_cast<std::size_t>(c)] +=
                mi.buildCost[static_cast<std::size_t>(c)].rawValue() / FIX;
    }
}

u32 designCreate(Empire& e, HullClass hull, const std::string& name, std::string* err) {
    if (e.designs.size() >= 24) {
        if (err) *err = "设计库已满（最多 24 份）";
        return kNoDesignId;
    }
    FleetDesign d;
    d.id = e.nextDesignId++;
    const HullInfo& hi = hullInfo(hull);
    d.name = name.empty() ? (std::string(hi.nameZh) + "-" + std::to_string(d.id)) : name;
    // 重名检查
    for (const auto& x : e.designs) {
        if (x.name == d.name) {
            if (err) *err = "已存在同名设计【" + d.name + "】";
            return kNoDesignId;
        }
    }
    d.hull = hull;
    d.custom = true;
    recomputeDesign(d);
    const u32 id = d.id;
    e.designs.push_back(std::move(d));
    if (err) *err = "已新建设计【" + e.designs.back().name + "】(#" + std::to_string(id) + "，" +
                    std::string(hi.nameZh) + " / " + std::string(hullRoleName(hi.role)) + ")";
    return id;
}

bool designInstallModule(Empire& e, u32 designId, int moduleIdx, std::string* err) {
    if (moduleIdx < 0 || moduleIdx >= kModuleCount) {
        if (err) *err = "未知模块";
        return false;
    }
    for (auto& d : e.designs) {
        if (d.id != designId) continue;
        const ModuleInfo& mi = moduleInfo(moduleIdx);
        if (static_cast<int>(d.hull) < static_cast<int>(mi.minHull)) {
            if (err)
                *err = "舰体等级不足：需要 " + std::string(hullClassName(mi.minHull)) + " 或更高";
            return false;
        }
        if (static_cast<int>(d.modules.size()) >= hullInfo(d.hull).slots) {
            if (err)
                *err = "槽位已满（" + std::string(hullInfo(d.hull).nameZh) + " 只有 " +
                       std::to_string(hullInfo(d.hull).slots) + " 个槽）";
            return false;
        }
        d.modules.push_back(static_cast<u8>(moduleIdx));
        recomputeDesign(d);
        if (err)
            *err = "已为【" + d.name + "】安装 " + std::string(mi.nameZh) + "（" +
                   std::to_string(d.modules.size()) + "/" +
                   std::to_string(hullInfo(d.hull).slots) + " 槽）";
        return true;
    }
    if (err) *err = "找不到该设计";
    return false;
}

bool designRemoveModule(Empire& e, u32 designId, int slot, std::string* err) {
    for (auto& d : e.designs) {
        if (d.id != designId) continue;
        if (slot < 0 || slot >= static_cast<int>(d.modules.size())) {
            if (err) *err = "槽位编号越界（该设计已装 " + std::to_string(d.modules.size()) + " 个模块）";
            return false;
        }
        const ModuleInfo& mi = moduleInfo(static_cast<int>(d.modules[static_cast<std::size_t>(slot)]));
        std::string removed(mi.nameZh);
        d.modules.erase(d.modules.begin() + slot);
        recomputeDesign(d);
        if (err) *err = "已从【" + d.name + "】卸下 " + removed;
        return true;
    }
    if (err) *err = "找不到该设计";
    return false;
}

bool designClearModules(Empire& e, u32 designId, std::string* err) {
    for (auto& d : e.designs) {
        if (d.id != designId) continue;
        const std::size_t n = d.modules.size();
        d.modules.clear();
        recomputeDesign(d);
        if (err) *err = "已清空【" + d.name + "】的 " + std::to_string(n) + " 个模块";
        return true;
    }
    if (err) *err = "找不到该设计";
    return false;
}

bool fleetRefit(GameState& st, u32 empire, u32 fleetId, u32 designId, std::string* err) {
    Empire* e = st.empire(empire);
    Fleet* f = st.fleet(fleetId);
    if (e == nullptr || f == nullptr || f->owner != empire) {
        if (err) *err = "非法主体或舰队不属于你";
        return false;
    }
    const FleetDesign* target = nullptr;
    for (const auto& d : e->designs)
        if (d.id == designId) target = &d;
    if (target == nullptr) {
        if (err) *err = "找不到目标设计";
        return false;
    }
    if (f->design == designId) {
        if (err) *err = "该舰队已在使用这份设计";
        return false;
    }
    // 改造必须在己方星系进行（需要船坞）
    const SystemNode* sys = st.system(f->system);
    if (sys == nullptr || sys->owner != empire) {
        if (err) *err = "改造必须在自己的星系内进行（需要船坞支持）";
        return false;
    }
    // 成本：按目标设计的造价的 45% 计
    i64 alloyNeed = target->buildCost[static_cast<std::size_t>(Commodity::Alloys)] * kRefitCostPct / 100;
    if (alloyNeed < 0) alloyNeed = 0;
    Fixed alloyCost = Fixed::raw(alloyNeed);
    i64 creditNeed = target->creditCost * kRefitCostPct / 100;
    if (e->stock[static_cast<std::size_t>(Commodity::Alloys)].rawValue() < alloyCost.rawValue()) {
        if (err)
            *err = "合金不足：改造需要 " + fixedStr(alloyCost, 0);
        return false;
    }
    if (e->treasury.rawValue() < Fixed(creditNeed).rawValue()) {
        if (err) *err = "国库不足：改造需要 " + std::to_string(creditNeed) + " cr";
        return false;
    }
    e->stock[static_cast<std::size_t>(Commodity::Alloys)] -= alloyCost;
    e->treasury -= Fixed(creditNeed);
    if (e->isPlayer) st.market.margin.cash = e->treasury;
    // 改造后舰队强度按新设计的火力比重缩放
    const FleetDesign* old = nullptr;
    for (const auto& d : e->designs)
        if (d.id == f->design) old = &d;
    if (old != nullptr && old->firepower.rawValue() > 0) {
        Fixed ratio = target->firepower / old->firepower;
        f->strength = f->strength * ratio;
    }
    f->design = designId;
    // 改造期间组织度归零（需要重新整训）
    f->org = Fixed(0);
    if (err)
        *err = "【" + f->name + "】已改造为 " + target->name + "（合金 -" + fixedStr(alloyCost, 0) +
               "，组织度清零需重整训）";
    st.logEvent(LogPhase::Economy, "ship.refit",
                e->name + " 将【" + f->name + "】改造为 " + target->name, empire);
    return true;
}

}  // namespace gf
