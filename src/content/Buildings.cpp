#include "domain/Building.h"

#include <initializer_list>
#include <utility>

#include "util/Str.h"

namespace gf {
namespace {

using Cost = std::initializer_list<std::pair<Commodity, i64>>;

std::array<i64, kCommodityCount> makeCost(Cost c) {
    std::array<i64, kCommodityCount> out{};
    for (const auto& kv : c) {
        int idx = static_cast<int>(kv.first);
        if (idx >= 0 && idx < kCommodityCount) out[static_cast<std::size_t>(idx)] = kv.second;
    }
    return out;
}

BuildingInfo mk(u8 id, std::string_view idName, std::string_view zh, std::string_view desc,
                BuildingEffect eff, Fixed val, Cost cost, i64 credits, i64 upkeep, u8 tier, i16 tech,
                bool uniq) {
    BuildingInfo b;
    b.id = id;
    b.idName = idName;
    b.nameZh = zh;
    b.desc = desc;
    b.effect = eff;
    b.effectValue = val;
    b.cost = makeCost(cost);
    b.creditCost = credits;
    b.upkeep = upkeep;
    b.tier = tier;
    b.requireTech = tech;
    b.unique = uniq;
    return b;
}

std::vector<BuildingInfo> buildBuildings() {
    std::vector<BuildingInfo> v;
    v.reserve(kBuildingCount);
    v.push_back(mk(0, "fusionPlant", "聚变电厂", "行星级聚变供能，是一切工业的底座。",
                   BuildingEffect::ProdEnergy, Fixed(120), {{Commodity::Minerals, 80}, {Commodity::Alloys, 20}},
                   4000, 40, 1, -1, false));
    v.push_back(mk(1, "miningGrid", "采矿网格", "自动化露天采掘阵列。", BuildingEffect::ProdMinerals,
                   Fixed(110), {{Commodity::Minerals, 60}, {Commodity::Alloys, 30}}, 4200, 45, 1, -1, false));
    v.push_back(mk(2, "hydroFarm", "水培农场", "封闭生态农业，稳定粮食产出。", BuildingEffect::ProdFood,
                   Fixed(130), {{Commodity::Minerals, 50}, {Commodity::Volatiles, 30}}, 3600, 30, 1, -1,
                   false));
    v.push_back(mk(3, "alloyFoundry", "合金熔炉", "把矿物转化为舰船与巨构的骨架。",
                   BuildingEffect::ProdAlloys, Fixed(90), {{Commodity::Minerals, 180}, {Commodity::Energy, 120}},
                   12000, 140, 2, 2, false));
    v.push_back(mk(4, "componentMill", "部件工厂", "精密加工，产出部件。", BuildingEffect::ProdComponents,
                   Fixed(70), {{Commodity::Alloys, 160}, {Commodity::Polymers, 90}}, 18000, 200, 2, 3, false));
    v.push_back(mk(5, "researchLab", "研究实验室", "基础科学设施。", BuildingEffect::ProdResearch, Fixed(85),
                   {{Commodity::Electronics, 120}, {Commodity::Minerals, 100}}, 15000, 180, 2, 1, false));
    v.push_back(mk(6, "tradeHub", "贸易枢纽", "吸引跨所订单流，提升本所深度。", BuildingEffect::Trading,
                   Fixed::pct(15), {{Commodity::Alloys, 120}, {Commodity::Electronics, 80}}, 22000, 220, 3, 6,
                   true));
    v.push_back(mk(7, "shipyard", "轨道船坞", "解锁并加速舰船建造。", BuildingEffect::Shipyard,
                   Fixed::pct(20), {{Commodity::Alloys, 260}, {Commodity::Components, 120}}, 30000, 320, 3, 8,
                   false));
    v.push_back(mk(8, "garrisonFort", "要塞卫戍", "提升行星防御与地面战力。", BuildingEffect::Defense,
                   Fixed(80), {{Commodity::Alloys, 200}, {Commodity::Minerals, 120}}, 16000, 180, 2, 5, false));
    v.push_back(mk(9, "propagandaBureau", "宣传局", "降低民怨、抬高合法性。", BuildingEffect::Stability,
                   Fixed::pct(12), {{Commodity::Electronics, 90}, {Commodity::Luxury, 40}}, 14000, 160, 2, -1,
                   false));
    v.push_back(mk(10, "archiveVault", "档案穹顶", "提高线索发现率与可信度起点。",
                   BuildingEffect::ClueDiscovery, Fixed::pct(18),
                   {{Commodity::DataCrystals, 60}, {Commodity::Electronics, 110}}, 26000, 240, 3, 12, true));
    v.push_back(mk(11, "merchantExchange", "商会交易所", "降低本方市场手续费。", BuildingEffect::ProdCredits,
                   Fixed(60), {{Commodity::Alloys, 90}, {Commodity::Luxury, 60}}, 17000, 150, 2, 7, false));
    v.push_back(mk(12, "unitySpire", "凝聚尖塔", "持续产出凝聚力。", BuildingEffect::ProdUnity, Fixed(45),
                   {{Commodity::Alloys, 140}, {Commodity::Relics, 4}}, 28000, 260, 3, 20, true));
    v.push_back(mk(13, "influenceChancery", "影响力公署", "持续产出影响力。", BuildingEffect::ProdInfluence,
                   Fixed(38), {{Commodity::DataCrystals, 40}, {Commodity::Electronics, 120}}, 30000, 280, 3, 21,
                   true));
    v.push_back(mk(14, "deepCoreMine", "深核矿场", "深层开采，稀有气体与挥发物。",
                   BuildingEffect::ProdMinerals, Fixed(70),
                   {{Commodity::Minerals, 220}, {Commodity::Robotics, 40}}, 21000, 240, 3, 9, false));
    v.push_back(mk(15, "gasRefinery", "气体精炼厂", "稀有气体提纯。", BuildingEffect::ProdEnergy, Fixed(85),
                   {{Commodity::Volatiles, 200}, {Commodity::Alloys, 90}}, 19000, 210, 2, 10, false));
    v.push_back(mk(16, "biomedLab", "生物医学中心", "持续生产药品，供民生与殖民补给。",
                   BuildingEffect::ProdMedicines, Fixed(75),
                   {{Commodity::Bioproducts, 120}, {Commodity::Electronics, 90}}, 20000, 220, 3, 14, false));
    v.push_back(mk(17, "antimatterRing", "反物质环", "反物质产能，风险极高。",
                   BuildingEffect::ProdEnergy, Fixed(160),
                   {{Commodity::Supermaterials, 90}, {Commodity::RareGases, 260}}, 64000, 620, 4, 30, true));
    v.push_back(mk(18, "quantumForge", "量子锻造厂", "超材料产出的唯一途径。",
                   BuildingEffect::ProdComponents, Fixed(110),
                   {{Commodity::Components, 200}, {Commodity::Exotic, 12}}, 72000, 700, 4, 31, true));
    v.push_back(mk(19, "dataCathedral", "数据圣殿", "把数据晶转化为研究点。", BuildingEffect::ProdResearch,
                   Fixed(130), {{Commodity::DataCrystals, 120}, {Commodity::Supermaterials, 60}}, 58000, 560, 4,
                   33, true));
    v.push_back(mk(20, "orbitalDock", "轨道船台", "大型舰船建造。", BuildingEffect::Shipyard, Fixed::pct(35),
                   {{Commodity::Alloys, 420}, {Commodity::Components, 220}}, 68000, 660, 4, 26, true));
    v.push_back(mk(21, "shieldArc", "护盾弧阵", "行星级护盾，极大提高防御。",
                   BuildingEffect::Defense, Fixed(150),
                   {{Commodity::Supermaterials, 80}, {Commodity::Electronics, 200}}, 54000, 520, 4, 24, true));
    v.push_back(mk(22, "silentCloister", "静默修道院", "提高反间谍与情报防御。",
                   BuildingEffect::Stability, Fixed::pct(20),
                   {{Commodity::Relics, 6}, {Commodity::Unity, 300}}, 44000, 400, 3, 36, true));
    v.push_back(mk(23, "marketWard", "市场监察署", "提高操纵检测率。", BuildingEffect::Trading,
                   Fixed::pct(10), {{Commodity::Electronics, 140}, {Commodity::Influence, 220}}, 40000, 380, 3,
                   35, true));
    v.push_back(mk(24, "blackVault", "黑库", "违禁品仓储与洗单。", BuildingEffect::Storage, Fixed(2000),
                   {{Commodity::Contraband, 40}, {Commodity::Alloys, 100}}, 36000, 340, 3, 40, true));
    v.push_back(mk(25, "storageRing", "仓储环", "大幅提高库存上限。", BuildingEffect::Storage, Fixed(1200),
                   {{Commodity::Minerals, 180}, {Commodity::Alloys, 140}}, 24000, 200, 2, 11, false));
    v.push_back(mk(26, "civicForum", "公民广场", "降低民怨，提高派系满意度。", BuildingEffect::Stability,
                   Fixed::pct(15), {{Commodity::Luxury, 120}, {Commodity::Polymers, 90}}, 18000, 190, 2, 22,
                   false));
    v.push_back(mk(27, "anomalyLab", "异常解析站", "解析异常点，产出线索与科技。",
                   BuildingEffect::ClueDiscovery, Fixed::pct(25),
                   {{Commodity::DataCrystals, 90}, {Commodity::Robotics, 60}}, 48000, 460, 4, 13, true));
    return v;
}

std::vector<MegastructureInfo> buildMegas() {
    std::vector<MegastructureInfo> v;
    auto add = [&](u8 id, std::string_view idName, std::string_view zh, std::string_view desc, Cost cost,
                   i64 credits, int stages, i16 tech, int ap, Fixed val, BuildingEffect eff) {
        MegastructureInfo m;
        m.id = id;
        m.idName = idName;
        m.nameZh = zh;
        m.desc = desc;
        m.cost = makeCost(cost);
        m.creditCost = credits;
        m.stages = stages;
        m.requireTech = tech;
        m.apCost = ap;
        m.effectValue = val;
        m.effect = eff;
        v.push_back(m);
    };
    add(0, "dysonRing", "戴森环", "环绕恒星的能量采集环，彻底解决能源约束。",
        {{Commodity::Alloys, 38000}, {Commodity::Supermaterials, 6400}, {Commodity::Components, 9000}}, 400000, 4,
        50, 2, Fixed(600), BuildingEffect::ProdEnergy);
    add(1, "ringWorld", "环世界", "人造环形大陆，可容纳亿万人口。",
        {{Commodity::Alloys, 52000}, {Commodity::Supermaterials, 11000}, {Commodity::Robotics, 4000}}, 620000, 5,
        55, 3, Fixed(1800), BuildingEffect::ProdFood);
    add(2, "matterDecompressor", "物质解压器", "从黑洞吸积盘抽取基础物质。",
        {{Commodity::Alloys, 44000}, {Commodity::Exotic, 1800}, {Commodity::Components, 12000}}, 540000, 4, 60, 2,
        Fixed(900), BuildingEffect::ProdMinerals);
    add(3, "scienceNexus", "科研枢纽", "跨星系研究网络的中枢。",
        {{Commodity::Alloys, 26000}, {Commodity::DataCrystals, 4200}, {Commodity::Electronics, 14000}}, 380000, 3,
        45, 2, Fixed(700), BuildingEffect::ProdResearch);
    add(4, "panopticonSpire", "泛视尖塔", "重建泛视网络的一段，让你也能看穿他人。",
        {{Commodity::Alloys, 30000}, {Commodity::DataCrystals, 6800}, {Commodity::Relics, 60}}, 460000, 4, 65, 3,
        Fixed(55), BuildingEffect::ClueDiscovery);
    add(5, "sentinelArray", "哨兵阵列", "巨型防御工事，使星系几乎不可攻陷。",
        {{Commodity::Alloys, 40000}, {Commodity::Supermaterials, 9000}, {Commodity::Antimatter, 400}}, 500000, 4,
        58, 2, Fixed(2200), BuildingEffect::Defense);
    return v;
}

}  // namespace

const BuildingInfo& buildingInfo(int idx) {
    static const std::vector<BuildingInfo> table = buildBuildings();
    if (idx < 0 || idx >= static_cast<int>(table.size())) idx = 0;
    return table[static_cast<std::size_t>(idx)];
}

const MegastructureInfo& megastructureInfo(int idx) {
    static const std::vector<MegastructureInfo> table = buildMegas();
    if (idx < 0 || idx >= static_cast<int>(table.size())) idx = 0;
    return table[static_cast<std::size_t>(idx)];
}

int buildingIndexByName(std::string_view s) {
    static const std::vector<BuildingInfo> table = buildBuildings();
    for (std::size_t i = 0; i < table.size(); ++i)
        if (iequals(table[i].idName, s) || table[i].nameZh == s) return static_cast<int>(i);
    return -1;
}

int megastructureIndexByName(std::string_view s) {
    static const std::vector<MegastructureInfo> table = buildMegas();
    for (std::size_t i = 0; i < table.size(); ++i)
        if (iequals(table[i].idName, s) || table[i].nameZh == s) return static_cast<int>(i);
    return -1;
}

std::string_view buildingEffectName(BuildingEffect e) {
    switch (e) {
        case BuildingEffect::ProdEnergy: return "能源产出";
        case BuildingEffect::ProdMinerals: return "矿物产出";
        case BuildingEffect::ProdFood: return "食物产出";
        case BuildingEffect::ProdMedicines: return "药品产出";
        case BuildingEffect::ProdAlloys: return "合金产出";
        case BuildingEffect::ProdComponents: return "部件产出";
        case BuildingEffect::ProdResearch: return "研究点";
        case BuildingEffect::ProdCredits: return "信用点";
        case BuildingEffect::ProdUnity: return "凝聚力";
        case BuildingEffect::ProdInfluence: return "影响力";
        case BuildingEffect::Stability: return "稳定";
        case BuildingEffect::Defense: return "防御";
        case BuildingEffect::Trading: return "贸易";
        case BuildingEffect::Shipyard: return "船坞";
        case BuildingEffect::Storage: return "仓储";
        case BuildingEffect::ClueDiscovery: return "线索发现";
        case BuildingEffect::Count: break;
    }
    return "?";
}

}  // namespace gf
