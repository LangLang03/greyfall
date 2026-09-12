#include "domain/Development.h"

#include <algorithm>
#include "core/GameState.h"
#include "domain/Construction.h"
#include "domain/Starbase.h"

namespace gf {
namespace {
bool reject(std::string* message, const std::string& text) { if (message) *message = text; return false; }
int branchCount(const Empire& e, TechBranch branch) {
    int count = 0;
    for (int i = 0; i < kTechCount; ++i)
        if (techInfo(i).branch == branch && techCompleted(e.tech, i)) ++count;
    return count;
}
i64 installment(i64 total, int stage, int count) { return total / count + (stage < total % count ? 1 : 0); }
}  // namespace

bool startMegaStage(GameState& st, u32 empire, int definition, u32 system, std::string* message) {
    Empire* e = st.empire(empire);
    const auto* sys = st.system(system);
    if (!e || !e->alive || !sys || sys->owner != empire || definition < 0 || definition >= kMegastructureCount)
        return reject(message, "巨构编号或目标星系无效");
    const auto& info = megastructureInfo(definition);
    if (info.requireTech >= 0 && !techCompleted(e->tech, info.requireTech))
        return reject(message, "需要先完成科技【" + std::string(techInfo(info.requireTech).nameZh) + "】");
    if (hasDevelopment(*e, ProjectKind::Mega)) return reject(message, "同时只能施工一个巨构阶段");
    MegastructureBuild* existing = nullptr;
    for (auto& m : e->megas) {
        if (m.defId == definition && m.system != system) return reject(message, "同类巨构每个帝国限一座，须在原星系继续");
        if (m.system == system) {
            if (m.defId != definition || m.complete) return reject(message, "该星系已有其他巨构或该巨构已经完工");
            existing = &m;
        }
    }
    if (sys->megastructure) return reject(message, "该星系已有完工巨构");
    for (const auto& owned : st.map.systems)
        if (owned.owner == empire && owned.megastructure && owned.megastructureId == static_cast<u32>(definition))
            return reject(message, "本国已有同类巨构（包括占领所得），不能再建");
    const int stage = existing ? existing->stage : 0;
    if (stage < 0 || stage >= info.stages) return reject(message, "巨构阶段已完成");
    ProjectCost cost;
    cost.credits = installment(info.creditCost, stage, info.stages);
    for (int i = 0; i < kCommodityCount; ++i) cost.resources[i] = installment(info.cost[i], stage, info.stages);
    if (!payProject(st, *e, cost, message)) return false;
    if (!existing) {
        MegastructureBuild m;
        m.id = static_cast<u32>(e->megas.size()); m.defId = static_cast<u8>(definition);
        m.owner = empire; m.system = system; m.startedTick = st.tick;
        e->megas.push_back(m);
    }
    DevelopmentProject project;
    project.kind = ProjectKind::Mega; project.target = system; project.definition = static_cast<u32>(definition);
    const Fixed speed = fxMin(Fixed(2), shipyardSpeedMultiplier(st, empire));
    project.totalTicks = static_cast<u32>(std::max<i64>(6, ((8 + stage * 4) * FIX + speed.rawValue() - 1) / speed.rawValue()));
    project.upkeep = std::max<i64>(500, info.creditCost / 1000);
    project.supplies[static_cast<int>(Commodity::Energy)] = 30;
    project.supplies[static_cast<int>(Commodity::Components)] = 5;
    addDevelopment(st, *e, project, cost, message);
    return true;
}

bool startStarbaseProject(GameState& st, u32 empire, u32 system, bool upgrade, std::string* message) {
    Empire* e = st.empire(empire);
    const auto* sys = st.system(system);
    if (!e || !e->alive || !sys || sys->owner != empire) return reject(message, "只能在本国星系施工");
    if (hasDevelopment(*e, ProjectKind::Starbase, system)) return reject(message, "该基地已经在施工");
    int active = 0;
    for (const auto& p : e->developmentProjects) if (p.kind == ProjectKind::Starbase) ++active;
    if (active >= 2) return reject(message, "同时最多施工两座恒星基地");
    const auto* base = starbaseAt(st, system);
    if (upgrade && (!base || base->owner != empire)) return reject(message, "该星系没有本国恒星基地");
    if (!upgrade && base) return reject(message, "该星系已有恒星基地");
    const int tier = upgrade ? static_cast<int>(base->tier) + 1 : 0;
    if (tier >= static_cast<int>(StarbaseTier::Count)) return reject(message, "已是最高等级");
    ProjectCost cost;
    cost.credits = starbaseUpgradeCredits(static_cast<StarbaseTier>(tier));
    cost.resources[static_cast<int>(Commodity::Alloys)] = starbaseUpgradeAlloys(static_cast<StarbaseTier>(tier));
    if (!payProject(st, *e, cost, message)) return false;
    DevelopmentProject project;
    project.kind = ProjectKind::Starbase; project.target = system; project.definition = static_cast<u32>(tier);
    project.totalTicks = static_cast<u32>(4 + tier * 2); project.upkeep = 200 + tier * 100;
    project.supplies[static_cast<int>(Commodity::Energy)] = 10 + tier * 5;
    addDevelopment(st, *e, project, cost, message);
    return true;
}

bool startAscension(GameState& st, u32 empire, int path, std::string* message) {
    auto* e = st.empire(empire);
    if (!e || path < 0 || path >= 5) return reject(message, "未知飞升路径");
    if (e->ascensions & (1u << path)) return reject(message, "该飞升路径已经完成，不能重复叠加");
    if (hasDevelopment(*e, ProjectKind::Ascension)) return reject(message, "已有飞升项目在进行");
    int completed = 0;
    for (int i = 0; i < 5; ++i) if (e->ascensions & (1u << i)) ++completed;
    if (completed >= 2) return reject(message, "每个纪元最多完成两条飞升路径");
    const TechBranch branches[] = {TechBranch::Psionics, TechBranch::Engineering, TechBranch::Biology, TechBranch::Physics, TechBranch::Psionics};
    const int required = path == 4 ? 12 : 4;
    if (branchCount(*e, branches[path]) < required)
        return reject(message, "需要完成该路径对应分支的 " + std::to_string(required) + " 项科技");
    ProjectCost cost;
    cost.credits = path == 4 ? 120000 : 50000;
    cost.unity = path == 4 ? 4000 : (path == 3 ? 2500 : 2000);
    cost.resources[static_cast<int>(Commodity::DataCrystals)] = 200;
    cost.resources[static_cast<int>(Commodity::Components)] = 150;
    if (!payProject(st, *e, cost, message)) return false;
    DevelopmentProject project;
    project.kind = ProjectKind::Ascension; project.definition = static_cast<u32>(path); project.target = e->capital;
    project.totalTicks = path == 4 ? 24 : 12; project.upkeep = 1000;
    project.supplies[static_cast<int>(Commodity::Energy)] = 20;
    project.supplies[static_cast<int>(Commodity::DataCrystals)] = 3;
    addDevelopment(st, *e, project, cost, message);
    return true;
}

bool startRecruitment(GameState& st, u32 empire, std::string* message) {
    auto* e = st.empire(empire);
    if (!e) return reject(message, "帝国不存在");
    if (hasDevelopment(*e, ProjectKind::Recruitment)) return reject(message, "已有一批人员正在训练");
    DevelopmentProject project;
    project.kind = ProjectKind::Recruitment; project.target = e->capital; project.totalTicks = 3;
    for (u32 id : e->fleets) {
        const auto* fleet = st.fleet(id);
        const auto* sys = fleet ? st.system(fleet->system) : nullptr;
        if (fleet && fleet->owner == empire && fleet->battle == kNoEmpire && sys && sys->owner == empire &&
            (fleet->morale < Fixed(1) || fleet->supply < Fixed(1))) project.fleets.push_back(id);
    }
    if (project.fleets.empty()) return reject(message, "没有驻于本国、未交战且需要补充的舰队");
    const auto count = static_cast<i64>(project.fleets.size());
    ProjectCost cost;
    cost.credits = 3000 + 2000 * count;
    cost.resources[static_cast<int>(Commodity::Food)] = 30 * count;
    cost.resources[static_cast<int>(Commodity::Medicines)] = 10 * count;
    cost.resources[static_cast<int>(Commodity::Components)] = 20 * count;
    if (!payProject(st, *e, cost, message)) return false;
    project.upkeep = 500 + 100 * count;
    project.supplies[static_cast<int>(Commodity::Food)] = 5 * count;
    addDevelopment(st, *e, project, cost, message);
    return true;
}

bool startGeneProject(GameState& st, u32 empire, int gene, std::string* message) {
    auto* e = st.empire(empire);
    if (!e || gene < 0 || gene >= static_cast<int>(GeneMod::Count)) return reject(message, "未知基因方向");
    if (e->geneMods[gene]) return reject(message, "该基因改造已完成");
    if (hasDevelopment(*e, ProjectKind::GeneMod)) return reject(message, "已有基因改造项目");
    if (branchCount(*e, TechBranch::Biology) < 2) return reject(message, "基因改造需要完成两项生物科技");
    ProjectCost cost;
    cost.credits = 40000; cost.unity = geneModCost(static_cast<GeneMod>(gene));
    cost.resources[static_cast<int>(Commodity::Medicines)] = 150;
    cost.resources[static_cast<int>(Commodity::Bioproducts)] = 100;
    if (!payProject(st, *e, cost, message)) return false;
    DevelopmentProject project;
    project.kind = ProjectKind::GeneMod; project.definition = static_cast<u32>(gene); project.target = e->capital;
    project.totalTicks = 8; project.upkeep = 750;
    project.supplies[static_cast<int>(Commodity::Medicines)] = 5;
    project.supplies[static_cast<int>(Commodity::Bioproducts)] = 3;
    addDevelopment(st, *e, project, cost, message);
    return true;
}
bool startProcessing(GameState& st, u32 empire, i64 batches, std::string* message) {
    auto* e = st.empire(empire);
    if (!e || !e->alive || batches < 1 || batches > 10) return reject(message, "每份加工订单必须为 1～10 批");
    if (hasDevelopment(*e, ProjectKind::Processing)) return reject(message, "已有罐头加工订单，完工后才能再启动");
    const auto* capital = st.system(e->capital);
    if (!capital || capital->owner != empire) return reject(message, "需要控制首都才能组织加工");
    ProjectCost cost; cost.credits = batches * 800;
    cost.resources[static_cast<int>(Commodity::Food)] = batches * 200;
    cost.resources[static_cast<int>(Commodity::Energy)] = batches * 20;
    cost.resources[static_cast<int>(Commodity::Polymers)] = batches * 10;
    if (!payProject(st, *e, cost, message)) return false;
    DevelopmentProject project; project.kind = ProjectKind::Processing; project.target = e->capital;
    project.definition = static_cast<u32>(batches); project.totalTicks = 3; project.upkeep = batches * 100;
    project.supplies[static_cast<int>(Commodity::Energy)] = batches * 5;
    project.supplies[static_cast<int>(Commodity::Components)] = batches;
    addDevelopment(st, *e, project, cost, message);
    return true;
}

bool startMigration(GameState& st, u32 empire, u32 system, std::string* message) {
    auto* e = st.empire(empire); const auto* target = st.system(system);
    if (!e || !e->alive || !target || target->owner != empire) return reject(message, "只能迁往本国星系");
    if (!isNomadic(st, empire)) return reject(message, "只有游牧政体可以迁徙首都");
    if (e->capital == system || hasDevelopment(*e, ProjectKind::Migration)) return reject(message, "首都已在该星系或正在迁徙");
    const int distance = st.map.hops(e->capital, system);
    if (distance < 0) return reject(message, "目标没有航线连接");
    if (e->influence < Fixed(300)) return reject(message, "迁徙需要 300 影响力");
    ProjectCost cost; cost.credits = 10000;
    cost.resources[static_cast<int>(Commodity::Energy)] = 100;
    cost.resources[static_cast<int>(Commodity::Components)] = 50;
    if (!payProject(st, *e, cost, message)) return false;
    e->influence -= Fixed(300);
    DevelopmentProject project; project.kind = ProjectKind::Migration; project.target = system;
    project.totalTicks = static_cast<u32>(4 + distance); project.upkeep = 500;
    project.supplies[static_cast<int>(Commodity::Energy)] = 15;
    addDevelopment(st, *e, project, cost, message);
    return true;
}

}  // namespace gf
