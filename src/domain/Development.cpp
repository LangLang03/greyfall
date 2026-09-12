#include "domain/Development.h"

#include <algorithm>
#include "cli/TextTable.h"
#include "core/GameState.h"
#include "domain/Construction.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {

std::string_view projectKindName(ProjectKind kind) {
    static constexpr const char* names[] = {"殖民", "巨构阶段", "恒星基地", "飞升", "招募训练", "基因改造", "罐头加工", "首都迁徙"};
    const auto i = static_cast<std::size_t>(kind);
    return i < static_cast<std::size_t>(ProjectKind::Count) ? names[i] : "未知";
}

std::string_view ascensionName(int path) {
    static constexpr const char* names[] = {"心灵", "机械", "基因", "虚空", "超脱"};
    return path >= 0 && path < 5 ? names[path] : "未知";
}

bool hasDevelopment(const Empire& e, ProjectKind kind, u32 target) {
    return std::any_of(e.developmentProjects.begin(), e.developmentProjects.end(), [&](const auto& p) {
        return p.kind == kind && (target == kNoSystem || p.target == target);
    });
}

void refreshColonizing(Empire& e) {
    e.colonizing.clear();
    for (const auto& p : e.developmentProjects)
        if (p.kind == ProjectKind::Colony) e.colonizing.push_back(p.target);
}

bool payProject(GameState& st, Empire& e, const ProjectCost& cost, std::string* message) {
    auto reject = [&](const std::string& reason) { if (message) *message = reason; return false; };
    if (!e.alive || cost.credits < 0 || cost.unity < 0) return reject("非法项目费用或帝国已覆灭");
    const Fixed available = e.isPlayer ? fxMin(e.treasury, st.market.margin.cash) : e.treasury;
    if (available < Fixed(cost.credits)) return reject("国库不足：需要 " + std::to_string(cost.credits) + " cr");
    if (e.unity < Fixed(cost.unity)) return reject("凝聚力不足：需要 " + std::to_string(cost.unity));
    for (int i = 0; i < kCommodityCount; ++i) {
        if (cost.resources[i] < 0) return reject("非法资源成本");
        if (e.stock[i] < Fixed(cost.resources[i]))
            return reject(std::string(commodityName(i)) + " 不足：需要 " + std::to_string(cost.resources[i]));
    }
    e.treasury = available - Fixed(cost.credits);
    e.unity -= Fixed(cost.unity);
    for (int i = 0; i < kCommodityCount; ++i) e.stock[i] -= Fixed(cost.resources[i]);
    if (e.isPlayer) st.market.margin.cash = e.treasury;
    return true;
}

void addDevelopment(GameState& st, Empire& e, DevelopmentProject p, const ProjectCost& cost, std::string* message) {
    p.paidCredits = cost.credits;
    p.ticksLeft = p.totalTicks;
    p.startedTick = st.tick;
    e.developmentProjects.push_back(p);
    refreshColonizing(e);
    const std::string text = "已启动" + std::string(projectKindName(p.kind)) + "项目：工期 " +
        std::to_string(p.totalTicks) + " 季，启动资金 " + std::to_string(cost.credits) +
        " cr，维护 " + std::to_string(p.upkeep) + " cr/季（另需补给）。";
    if (message) *message = text;
    st.logEvent(LogPhase::Economy, "project.start", e.name + "：" + text, e.id);
}

bool cancelDevelopment(GameState& st, u32 empire, std::size_t index, std::string* message) {
    Empire* e = st.empire(empire);
    if (!e || index >= e->developmentProjects.size()) { if (message) *message = "项目序号越界"; return false; }
    const auto& p = e->developmentProjects[index];
    const i64 refund = p.paidCredits * 40 / 100;
    e->treasury += Fixed(refund);
    if (e->isPlayer) st.market.margin.cash = e->treasury;
    if (message) *message = "已取消" + std::string(projectKindName(p.kind)) + "，退还启动资金的 40%（" +
        std::to_string(refund) + " cr）；物资、凝聚力、影响力和维护费不退。";
    if (p.kind == ProjectKind::Mega)
        std::erase_if(e->megas, [&](const auto& m) { return m.system == p.target && m.defId == p.definition && m.stage == 0 && !m.complete; });
    e->developmentProjects.erase(e->developmentProjects.begin() + static_cast<std::ptrdiff_t>(index));
    refreshColonizing(*e);
    st.logEvent(LogPhase::Economy, "project.cancel", e->name + " 取消工程", empire);
    return true;
}

namespace {
std::string blockedProject(const GameState& st, const Empire& e, const DevelopmentProject& p) {
    if (p.kind == ProjectKind::Colony || p.kind == ProjectKind::Mega || p.kind == ProjectKind::Starbase ||
        p.kind == ProjectKind::Processing || p.kind == ProjectKind::Migration) {
        const auto* sys = st.system(p.target);
        if (!sys) return "目标星系不存在";
        if (p.kind != ProjectKind::Colony && sys->owner != e.id) return "目标星系已失守";
        if (p.kind == ProjectKind::Colony) {
            if (sys->owner != kNoEmpire && sys->owner != e.id) return "殖民目标被其他帝国占领";
            bool accessible = sys->owner == e.id;
            for (u32 id : sys->links) {
                const auto* from = st.system(id);
                if (from && from->owner == e.id && from->blockade < Fixed::pct(50)) accessible = true;
            }
            if (!accessible) return "殖民补给线中断或邻接星系遭封锁";
            const auto capacity = colonialCapacity(st, e.id);
            if (sys->owner == kNoEmpire && capacity.owned >= capacity.territories) return "殖民管理容量不足";
            const auto* planet = st.planet(p.auxiliary);
            if (!planet || planet->colonized || planet->owner != kNoEmpire) return "据点行星已被占用";
        }
        if (p.kind == ProjectKind::Starbase) {
            const auto* base = starbaseAt(st, p.target);
            if ((p.definition == 0 && base) || (p.definition > 0 &&
                (!base || base->owner != e.id || static_cast<u32>(base->tier) + 1 != p.definition)))
                return "基地状态改变，需要取消后重新规划";
        }
        if (sys->blockade >= Fixed::pct(50)) return "目标星系遭封锁";
    }
    return {};
}

void completeProject(GameState& st, Empire& e, const DevelopmentProject& p) {
    if (p.kind == ProjectKind::Colony) {
        auto* sys = st.system(p.target);
        auto* planet = st.planet(p.auxiliary);
        if (!sys || !planet) return;
        sys->owner = e.id;
        sys->colonized = true;
        if (std::find(e.systems.begin(), e.systems.end(), sys->id) == e.systems.end()) e.systems.push_back(sys->id);
        planet->owner = e.id;
        planet->colonized = true;
        planet->pops = 200;
        planet->development = Fixed(0);
        planet->settlementTicksLeft = 12;
        planet->settlementStartedTick = st.tick;
        ++e.coloniesFounded;
    } else if (p.kind == ProjectKind::Mega) {
        for (auto& m : e.megas) {
            if (m.system != p.target || m.defId != p.definition || m.complete) continue;
            ++m.stage;
            m.progress = Fixed(0);
            if (m.stage >= megastructureInfo(m.defId).stages) {
                m.complete = true;
                auto* sys = st.system(p.target);
                sys->megastructure = true;
                sys->megastructureId = m.defId;
            }
            break;
        }
    } else if (p.kind == ProjectKind::Starbase) {
        Starbase* found = nullptr;
        for (auto& b : st.starbases) if (b.system == p.target && b.owner == e.id) found = &b;
        if (found) found->tier = static_cast<StarbaseTier>(p.definition);
        else {
            Starbase b;
            b.system = p.target; b.owner = e.id; b.tier = static_cast<StarbaseTier>(p.definition); b.builtTick = st.tick;
            st.starbases.push_back(b);
        }
    } else if (p.kind == ProjectKind::Ascension) {
        e.ascensions |= 1u << p.definition;
        if (e.isPlayer) {
            const int dimensions[] = {5, 4, 3, 6, 7};
            st.plot.endingVector[dimensions[p.definition]] += Fixed(p.definition == 4 ? 3 : 1);
            if (p.definition == 4) st.plot.hiddenActUnlocked = true;
        }
    } else if (p.kind == ProjectKind::Recruitment) {
        for (u32 id : p.fleets) {
            Fleet* f = st.fleet(id);
            if (!f || f->owner != e.id || f->battle != kNoEmpire) continue;
            const auto* system = st.system(f->system);
            if (!system || system->owner != e.id) continue;
            f->morale = fxMin(Fixed(1), f->morale + Fixed::pct(10));
            f->supply = fxMin(Fixed(1), f->supply + Fixed::pct(15));
        }
    } else if (p.kind == ProjectKind::Processing) {
        e.stock[static_cast<int>(Commodity::Luxury)] += Fixed(p.definition * 30);
    } else if (p.kind == ProjectKind::Migration) {
        if (auto* previous = st.system(e.capital)) {
            previous->capital = false;
            for (u32 id : previous->planets) if (auto* planet = st.planet(id)) planet->capital = false;
        }
        e.capital = p.target;
        if (auto* capital = st.system(e.capital)) {
            capital->capital = true;
            for (u32 id : capital->planets) {
                auto* planet = st.planet(id);
                if (planet && planet->colonized && planet->owner == e.id) { planet->capital = true; break; }
            }
        }
    } else if (p.kind == ProjectKind::GeneMod) {
        e.geneMods[p.definition] = true;
        for (auto& f : e.domestic.factions) {
            if (f.kind == FactionKind::Fundamentalist) f.satisfaction = fxMax(Fixed(0), f.satisfaction - Fixed::pct(10));
            if (f.kind == FactionKind::Technocrat) f.satisfaction = fxMin(Fixed(1), f.satisfaction + Fixed::pct(8));
        }
    }
    st.logEvent(LogPhase::Economy, "project.done", e.name + " 完成" + std::string(projectKindName(p.kind)) +
                "项目（目标 #" + std::to_string(p.target) + "）", e.id);
}
}  // namespace

void developmentPhase(GameState& st) {
    for (auto& planet : st.planets) if (planet.settlementTicksLeft > 0 && st.tick >= planet.settlementStartedTick)
        planet.settlementTicksLeft = static_cast<u32>(12 - std::min<u64>(12, st.tick - planet.settlementStartedTick));
    for (auto& e : st.empires) {
        if (!e.alive) { e.developmentProjects.clear(); refreshColonizing(e); continue; }
        for (auto it = e.developmentProjects.begin(); it != e.developmentProjects.end();) {
            auto& p = *it;
            if (p.hasAdvanced && p.lastTick == st.tick) { ++it; continue; }
            p.hasAdvanced = true; p.lastTick = st.tick;
            p.pauseReason = blockedProject(st, e, p);
            if (p.pauseReason.empty() && e.treasury < Fixed(p.upkeep)) p.pauseReason = "维护资金不足";
            for (int i = 0; i < kCommodityCount && p.pauseReason.empty(); ++i)
                if (e.stock[i] < Fixed(p.supplies[i])) p.pauseReason = std::string(commodityName(i)) + " 补给不足";
            if (!p.pauseReason.empty()) {
                // 停工仍有留守费用；连续八季无法恢复则撤回项目，不能无限占位。
                const Fixed holding = fxMin(fxMax(e.treasury, Fixed(0)), Fixed(std::max<i64>(1, p.upkeep / 5)));
                e.treasury -= holding; e.lastIncome -= holding;
                if (e.isPlayer) st.market.margin.cash = e.treasury;
                ++p.stalledTicks;
                if (p.stalledTicks >= 8) {
                    st.logEvent(LogPhase::Economy, "project.failed", e.name + " 的" + std::string(projectKindName(p.kind)) +
                        "项目连续停工 8 季，已撤回且不退款：" + p.pauseReason, e.id);
                    if (p.kind == ProjectKind::Mega)
                        std::erase_if(e.megas, [&](const auto& m) { return m.system == p.target && m.defId == p.definition && m.stage == 0 && !m.complete; });
                    it = e.developmentProjects.erase(it);
                } else ++it;
                continue;
            }
            p.stalledTicks = 0;
            e.treasury -= Fixed(p.upkeep); e.lastIncome -= Fixed(p.upkeep);
            if (e.isPlayer) st.market.margin.cash = e.treasury;
            for (int i = 0; i < kCommodityCount; ++i) e.stock[i] -= Fixed(p.supplies[i]);
            if (p.ticksLeft > 0) --p.ticksLeft;
            if (p.ticksLeft == 0) { completeProject(st, e, p); it = e.developmentProjects.erase(it); }
            else ++it;
        }
        refreshColonizing(e);
    }
    refreshEmpireBonuses(st);
}

Fixed megaProduction(const GameState& st, const Empire& e, int commodity) {
    Fixed total;
    for (const auto& sys : st.map.systems) {
        if (sys.owner != e.id || !sys.megastructure || sys.megastructureId >= kMegastructureCount) continue;
        const auto& info = megastructureInfo(sys.megastructureId);
        const int resource = info.effect == BuildingEffect::ProdEnergy ? static_cast<int>(Commodity::Energy) :
                             info.effect == BuildingEffect::ProdFood ? static_cast<int>(Commodity::Food) :
                             info.effect == BuildingEffect::ProdMinerals ? static_cast<int>(Commodity::Minerals) : -1;
        if (resource == commodity) total += info.effectValue;
    }
    return total;
}

Fixed developmentUpkeep(const GameState& st, u32 empire) {
    Fixed upkeep = colonialUpkeep(st, empire).credits;
    for (const auto& b : st.starbases) {
        const auto* sys = st.system(b.system);
        if (b.owner == empire && sys && sys->owner == empire) {
            const int tier = static_cast<int>(b.tier) + 1;
            upkeep += Fixed(50 * tier * tier);
        }
    }
    const auto* e = st.empire(empire);
    for (const auto& sys : st.map.systems)
        if (sys.owner == empire && sys.megastructure && sys.megastructureId < kMegastructureCount)
            upkeep += Fixed(megastructureInfo(sys.megastructureId).creditCost / 500);
    if (e) for (const auto& edict : e->nationalEdicts)
        if (st.tick < edict.expiresTick) upkeep += Fixed(nationalEdictUpkeep(edict.kind));
    return upkeep;
}

std::string developmentReport(const GameState& st, u32 empire) {
    const auto* e = st.empire(empire);
    if (!e) return "帝国不存在\n";
    TextTable table;
    table.header({"项目号", "类型", "目标", "剩余/总工期", "维护/季", "状态"});
    for (std::size_t i = 0; i < e->developmentProjects.size(); ++i) {
        const auto& p = e->developmentProjects[i];
        std::string target = "#" + std::to_string(p.target);
        if (p.kind == ProjectKind::Ascension) target = ascensionName(static_cast<int>(p.definition));
        table.row({std::to_string(i), std::string(projectKindName(p.kind)), target,
                   std::to_string(p.ticksLeft) + "/" + std::to_string(p.totalTicks), std::to_string(p.upkeep),
                   p.pauseReason.empty() ? "施工中" : p.pauseReason + "（" + std::to_string(p.stalledTicks) + "/8）"});
    }
    std::string out = e->developmentProjects.empty() ? "  （没有殖民或国家工程）\n" : table.render() + "\n";
    for (std::size_t i = 0; i < e->developmentProjects.size(); ++i) {
        out += "  #" + std::to_string(i) + " 每季补给：";
        for (int c = 0; c < kCommodityCount; ++c)
            if (e->developmentProjects[i].supplies[c]) out += std::string(commodityName(c)) + " " + std::to_string(e->developmentProjects[i].supplies[c]) + " ";
        out += "\n";
    }
    return out;
}
}  // namespace gf
