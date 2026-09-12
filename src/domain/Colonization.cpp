#include "domain/Development.h"

#include <algorithm>
#include <queue>
#include "core/GameState.h"
#include "gen/EmpireGen.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {
namespace {
bool ethic(const Empire& e, EthicAxis axis) {
    return std::find(e.ethics.begin(), e.ethics.end(), static_cast<u8>(axis)) != e.ethics.end();
}

bool colonyPlan(const GameState& st, const Empire& e, u32 system, DevelopmentProject& project,
                ProjectCost& cost, std::string* message) {
    auto failWith = [&](const std::string& reason) { if (message) *message = reason; return false; };
    const auto* sys = st.system(system);
    if (!e.alive || sys == nullptr) return failWith("星系不存在或帝国已覆灭");
    if (sys->owner != kNoEmpire && sys->owner != e.id) return failWith("不能殖民其他帝国的星系");
    for (const auto& other : st.empires)
        if (hasDevelopment(other, ProjectKind::Colony, system)) return failWith("该星系已有殖民项目");
    bool adjacent = sys->owner == e.id;
    for (u32 id : sys->links) {
        const auto* neighbor = st.system(id);
        if (neighbor && neighbor->owner == e.id) adjacent = true;
    }
    if (!adjacent) return failWith("目标必须与已经拥有的领土相邻，在建殖民地不能作为跳板");
    const auto capacity = colonialCapacity(st, e.id);
    if (capacity.active >= capacity.parallel) return failWith("已达到同时殖民上限（" + std::to_string(capacity.parallel) + " 项）");
    int reserved = 0;
    for (const auto& p : e.developmentProjects) {
        const auto* target = st.system(p.target);
        if (p.kind == ProjectKind::Colony && target && target->owner == kNoEmpire) ++reserved;
    }
    if (sys->owner == kNoEmpire && capacity.owned + reserved >= capacity.territories)
        return failWith("殖民管理容量不足（含在建名额）；研究社会/工程科技可提高上限");
    const Planet* best = nullptr;
    for (u32 id : sys->planets) {
        const Planet* p = st.planet(id);
        if (!p || p->colonized || p->owner != kNoEmpire || p->type == PlanetType::GasGiant ||
            p->type == PlanetType::Asteroid || p->type == PlanetType::Shattered) continue;
        if (p->habitability < Fixed::pct(20) && !techCompleted(e.tech, 57)) continue;
        if (!best || p->habitability > best->habitability) best = p;
    }
    if (!best) return failWith("没有可建立据点的未殖民行星；低宜居度行星需要人工生态圈科技");
    const int distance = st.map.hops(e.capital, system);
    if (distance < 0) return failWith("目标与首都没有航线连接");
    project.kind = ProjectKind::Colony;
    project.target = system;
    project.auxiliary = best->id;
    const Fixed priceFactor = fxClamp(Fixed(1) + empireModifier(e, ModKind::ColonyCost), Fixed::pct(50), Fixed(2));
    cost.credits = ((Fixed(18000 + best->size * 600 + distance * 2000) * priceFactor).rawValue() + FIX - 1) / FIX;
    cost.resources[static_cast<int>(Commodity::Alloys)] = 300;
    cost.resources[static_cast<int>(Commodity::Components)] = 180;
    cost.resources[static_cast<int>(Commodity::Energy)] = 600;
    cost.resources[static_cast<int>(Commodity::Food)] = 500;
    cost.resources[static_cast<int>(Commodity::Medicines)] = 100;
    const i64 hostile = (Fixed(1) - fxClamp(best->habitability, Fixed(0), Fixed(1))).rawValue() / 100;
    const Fixed speed = fxClamp(Fixed(1) + empireModifier(e, ModKind::BuildRate), Fixed::pct(50), Fixed(2));
    const i64 base = 8 + distance + hostile;
    project.totalTicks = static_cast<u32>(std::max<i64>(6, (base * FIX + speed.rawValue() - 1) / speed.rawValue()));
    project.upkeep = 500 + distance * 100;
    project.supplies[static_cast<int>(Commodity::Energy)] = 20;
    project.supplies[static_cast<int>(Commodity::Food)] = 15;
    project.supplies[static_cast<int>(Commodity::Medicines)] = 2;
    project.supplies[static_cast<int>(Commodity::Components)] = 2;
    return true;
}
}  // namespace

ColonialCapacity colonialCapacity(const GameState& st, u32 empire) {
    ColonialCapacity c;
    const Empire* e = st.empire(empire);
    if (!e) return c;
    // 完成集合去重，不允许重复科技记录增加容量。
    for (int i = 0; i < kTechCount; ++i) {
        if (!techCompleted(e->tech, i)) continue;
        if (techInfo(i).branch == TechBranch::Society) ++c.societyTechs;
        if (techInfo(i).branch == TechBranch::Engineering) ++c.engineeringTechs;
    }
    c.territories += c.societyTechs + (c.engineeringTechs / 4) * 2;
    c.parallel += c.societyTechs / 8 + c.engineeringTechs / 8;
    if (ethic(*e, EthicAxis::Expansion)) { c.parallel += 1; c.territories += 4; }
    if (ethic(*e, EthicAxis::Isolation)) { c.parallel -= 1; c.territories -= 2; }
    if (ethic(*e, EthicAxis::Collectivism)) c.territories += 2;
    if (e->government == 9 || e->government == 14) { ++c.parallel; c.territories += 4; }
    if (e->government == 15) c.territories += 3;
    if (e->government == 7 || e->government == 16) c.territories += 2;
    if (e->government == 17 || e->government == 6) c.territories += 2;
    c.parallel = std::clamp(c.parallel, 1, 5);
    c.territories = std::clamp(c.territories, 3, 48);
    for (const auto& p : e->developmentProjects) if (p.kind == ProjectKind::Colony) ++c.active;
    for (const auto& s : st.map.systems) if (s.owner == empire && s.id != e->capital) ++c.owned;
    return c;
}

ColonialUpkeep colonialUpkeep(const GameState& st, u32 empire) {
    ColonialUpkeep upkeep;
    const Empire* e = st.empire(empire);
    if (!e) return upkeep;
    std::vector<int> distances(st.map.systems.size(), -1);
    std::queue<u32> frontier;
    if (e->capital < distances.size()) { distances[e->capital] = 0; frontier.push(e->capital); }
    while (!frontier.empty()) {
        const u32 id = frontier.front(); frontier.pop();
        for (u32 next : st.map.systems[id].links) {
            if (next >= distances.size() || distances[next] >= 0) continue;
            distances[next] = distances[id] + 1; frontier.push(next);
        }
    }
    for (const auto& sys : st.map.systems) {
        if (sys.owner != empire || sys.id == e->capital) continue;
        int planets = 0;
        for (u32 pid : sys.planets) {
            const auto* p = st.planet(pid);
            if (p && p->owner == empire && p->colonized) ++planets;
        }
        // 每个海外星系都有行政/运输费，每颗定居行星都有补给需求。
        const int distance = sys.id < distances.size() && distances[sys.id] >= 0 ? distances[sys.id] : static_cast<int>(st.map.systems.size());
        upkeep.credits += Fixed(80 + 20 * planets + 15 * distance);
        upkeep.resources[static_cast<int>(Commodity::Energy)] += Fixed(8 * planets);
        upkeep.resources[static_cast<int>(Commodity::Food)] += Fixed(5 * planets);
        upkeep.resources[static_cast<int>(Commodity::Medicines)] += Fixed(planets);
    }
    const auto capacity = colonialCapacity(st, empire);
    const int excess = std::max(0, capacity.owned - capacity.territories);
    upkeep.credits *= Fixed(1) + fxMin(Fixed::pct(5) * Fixed(excess), Fixed(3));
    return upkeep;
}

Fixed resourceDemand(const GameState& st, const Empire& e, int commodity) {
    if (commodity < 0 || commodity >= kCommodityCount) return Fixed(0);
    Fixed need = e.demand[static_cast<std::size_t>(commodity)];
    const int units = commodity == static_cast<int>(Commodity::Energy) ? 8 :
                      commodity == static_cast<int>(Commodity::Food) ? 5 :
                      commodity == static_cast<int>(Commodity::Medicines) ? 1 : 0;
    if (units) for (const auto& p : st.planets) {
        const auto* sys = st.system(p.system);
        if (p.colonized && p.owner == e.id && p.system != e.capital && sys && sys->owner == e.id)
            need += Fixed(units);
    }
    for (const auto& edict : e.nationalEdicts)
        if (edict.kind == 0 && st.tick < edict.expiresTick && commodity != static_cast<int>(Commodity::Credits))
            need *= Fixed::pct(90);
    return need;
}

bool startColony(GameState& st, u32 empire, u32 system, std::string* message) {
    Empire* e = st.empire(empire);
    if (!e) { if (message) *message = "帝国不存在"; return false; }
    DevelopmentProject project;
    ProjectCost cost;
    if (!colonyPlan(st, *e, system, project, cost, message) || !payProject(st, *e, cost, message)) return false;
    addDevelopment(st, *e, project, cost, message);
    return true;
}

std::string colonyReport(const GameState& st, u32 empire, u32 system) {
    const Empire* e = st.empire(empire);
    if (!e) return "帝国不存在\n";
    const auto c = colonialCapacity(st, empire);
    const auto upkeep = colonialUpkeep(st, empire);
    std::string out = "═══ 殖民管理 ═══\n  同时殖民 " + std::to_string(c.active) + " / " + std::to_string(c.parallel) +
        " 项；非首都星系 " + std::to_string(c.owned) + " / " + std::to_string(c.territories) + "\n";
    int reserved = 0;
    for (const auto& project : e->developmentProjects) {
        const auto* target = st.system(project.target);
        if (project.kind == ProjectKind::Colony && target && target->owner == kNoEmpire) ++reserved;
    }
    out += "  在建项目另预占 " + std::to_string(reserved) + " 个新星系管理名额\n";
    out += "  政体：" + std::string(governmentInfo(e->government).nameZh) + "；伦理：";
    for (u8 id : e->ethics) out += std::string(ethicInfo(id).nameZh) + " ";
    out += "\n  社会科技 " + std::to_string(c.societyTechs) + " 项，工程科技 " + std::to_string(c.engineeringTechs) + " 项\n";
    out += "  已有殖民地维护 " + fixedStr(upkeep.credits, 0) + " cr/季，能源 " +
        fixedStr(upkeep.resources[0], 0) + "、食物 " + fixedStr(upkeep.resources[2], 0) + "、药品 " +
        fixedStr(upkeep.resources[5], 0) + " /季\n";
    if (system != kNoSystem) {
        DevelopmentProject p;
        ProjectCost cost;
        std::string reason;
        if (!colonyPlan(st, *e, system, p, cost, &reason)) return out + "  无法启动：" + reason + "\n";
        out += "\n  据点行星：" + st.planet(p.auxiliary)->name + " (#" + std::to_string(p.auxiliary) + ")\n";
        out += "  启动资金 " + std::to_string(cost.credits) + " cr；工期 " + std::to_string(p.totalTicks) + " 季\n";
        for (int i = 0; i < kCommodityCount; ++i)
            if (cost.resources[i]) out += "    " + std::string(commodityName(i)) + " " + std::to_string(cost.resources[i]) + "\n";
        out += "  施工维护 " + std::to_string(p.upkeep) + " cr/季，能源 20、食物 15、药品 2、部件 2 /季\n";
    }
    out += "  colony --detail <星系> 预估；colony <星系> 启动；queue 查看/取消项目。\n";
    return out;
}
}  // namespace gf
