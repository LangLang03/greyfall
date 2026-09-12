#include "domain/FleetDesign.h"

#include <initializer_list>
#include <utility>

#include "util/Str.h"

namespace gf {
namespace {

using Cost = std::initializer_list<std::pair<Commodity, Fixed>>;

std::array<Fixed, kCommodityCount> makeCost(Cost c) {
    std::array<Fixed, kCommodityCount> out{};
    for (const auto& kv : c) {
        int idx = static_cast<int>(kv.first);
        if (idx >= 0 && idx < kCommodityCount) out[static_cast<std::size_t>(idx)] = kv.second;
    }
    return out;
}

ModuleInfo mk(u8 id, std::string_view idName, std::string_view zh, std::string_view desc, HullClass minHull,
              ModuleSlot slot, Fixed costMul, Cost cost, ModuleEffect eff, Fixed val, i16 tech) {
    ModuleInfo m;
    m.id = id;
    m.idName = idName;
    m.nameZh = zh;
    m.desc = desc;
    m.minHull = minHull;
    m.slot = slot;
    m.costMul = costMul;
    m.buildCost = makeCost(cost);
    m.effect = eff;
    m.effectValue = val;
    m.unlockTech = tech;
    return m;
}

std::vector<ModuleInfo> buildModules() {
    std::vector<ModuleInfo> v;
    v.reserve(kModuleCount);
    using H = HullClass;
    using S = ModuleSlot;
    using E = ModuleEffect;
    // 武器
    v.push_back(mk(0, "massDriver", "质量投射器", "廉价可靠的动能主炮。", H::Corvette, S::Weapon, Fixed(1),
                   {{Commodity::Alloys, 40}}, E::Firepower, Fixed(12), -1));
    v.push_back(mk(1, "laserBattery", "激光炮组", "对护盾效率高。", H::Corvette, S::Weapon, Fixed::raw(1200),
                   {{Commodity::Alloys, 55}, {Commodity::Electronics, 20}}, E::Firepower, Fixed(20), 5));
    v.push_back(mk(2, "plasmaLance", "等离子长矛", "对装甲穿透极强。", H::Destroyer, S::Weapon, Fixed::raw(1600),
                   {{Commodity::Alloys, 90}, {Commodity::RareGases, 40}}, E::Firepower, Fixed(34), 20));
    v.push_back(mk(3, "railgunArray", "电磁炮阵列", "高射速压制火力。", H::Frigate, S::Weapon, Fixed::raw(1400),
                   {{Commodity::Alloys, 80}, {Commodity::Polymers, 30}}, E::Firepower, Fixed(28), 5));
    v.push_back(mk(4, "phaseCannon", "相位炮", "无视部分护盾。", H::Cruiser, S::Weapon, Fixed::raw(2200),
                   {{Commodity::Supermaterials, 30}, {Commodity::Exotic, 6}}, E::Firepower, Fixed(58), 31));
    v.push_back(mk(5, "antimatterTorpedo", "反物质鱼雷", "一次性巨额伤害。", H::Cruiser, S::Weapon,
                   Fixed::raw(2600), {{Commodity::Antimatter, 12}, {Commodity::Components, 60}}, E::Firepower,
                   Fixed(74), 30));
    v.push_back(mk(6, "psionicLash", "灵能鞭笞", "无视装甲，直接冲击船员心智。", H::Frigate, S::Weapon,
                   Fixed::raw(2000), {{Commodity::Relics, 4}, {Commodity::Unity, 120}}, E::Firepower, Fixed(46),
                   72));
    v.push_back(mk(7, "pointDefense", "近防炮", "拦截来袭弹药与鱼雷。", H::Corvette, S::Weapon, Fixed(1),
                   {{Commodity::Alloys, 30}, {Commodity::Electronics, 15}}, E::Defense, Fixed(14), 5));
    // 防御
    v.push_back(mk(8, "armorPlate", "复合装甲板", "最基础的生存手段。", H::Corvette, S::Defense, Fixed(1),
                   {{Commodity::Alloys, 45}}, E::Defense, Fixed(16), -1));
    v.push_back(mk(9, "shieldEmitter", "护盾发射器", "可再生防护。", H::Frigate, S::Defense, Fixed::raw(1500),
                   {{Commodity::Supermaterials, 18}, {Commodity::RareGases, 35}}, E::Defense, Fixed(30), 6));
    v.push_back(mk(10, "crystalWeave", "晶体编织层", "自修复装甲。", H::Cruiser, S::Defense, Fixed::raw(1900),
                   {{Commodity::Supermaterials, 40}, {Commodity::Minerals, 90}}, E::Defense, Fixed(44), 21));
    v.push_back(mk(11, "adaptiveHull", "自适应船体", "随战斗调整结构。", H::Battleship, S::Defense,
                   Fixed::raw(2400), {{Commodity::Supermaterials, 60}, {Commodity::Robotics, 40}}, E::Defense,
                   Fixed(62), 25));
    v.push_back(mk(12, "mindBarrier", "心灵屏障舱", "免疫灵能攻击与读心。", H::Frigate, S::Defense,
                   Fixed::raw(1800), {{Commodity::Relics, 3}, {Commodity::Unity, 150}}, E::Defense, Fixed(24),
                   72));
    v.push_back(mk(13, "repairSwarm", "维修蜂群", "战斗中自我修复。", H::Destroyer, S::Defense, Fixed::raw(1700),
                   {{Commodity::Robotics, 30}, {Commodity::Components, 45}}, E::RepairCost, Fixed::pct(-25), 26));
    // 推进
    v.push_back(mk(14, "chemicalDrive", "化学推进器", "廉价但笨重。", H::Corvette, S::Drive, Fixed(1),
                   {{Commodity::Volatiles, 30}}, E::Speed, Fixed(10), -1));
    v.push_back(mk(15, "ionDrive", "离子引擎", "巡航效率高。", H::Corvette, S::Drive, Fixed::raw(1100),
                   {{Commodity::RareGases, 25}, {Commodity::Alloys, 25}}, E::Speed, Fixed(22), 3));
    v.push_back(mk(16, "warpCoil", "曲率线圈", "跨越航线所需的折叠引擎。", H::Destroyer, S::Drive,
                   Fixed::raw(1800), {{Commodity::Supermaterials, 25}, {Commodity::RareGases, 60}}, E::JumpRange,
                   Fixed(1), 32));
    v.push_back(mk(17, "inertialDamper", "惯性阻尼器", "大幅提升机动。", H::Frigate, S::Drive, Fixed::raw(1500),
                   {{Commodity::Electronics, 45}, {Commodity::Polymers, 30}}, E::Speed, Fixed(30), 11));
    v.push_back(mk(18, "jumpDrive", "跃迁引擎", "跳跃范围与速度兼得。", H::Cruiser, S::Drive, Fixed::raw(2300),
                   {{Commodity::Exotic, 5}, {Commodity::Supermaterials, 45}}, E::JumpRange, Fixed(2), 45));
    v.push_back(mk(19, "gravitySail", "引力帆", "利用恒星风，近乎零燃料。", H::Battleship, S::Drive,
                   Fixed::raw(1600), {{Commodity::Supermaterials, 35}, {Commodity::Volatiles, 80}}, E::Speed,
                   Fixed(26), 30));
    // 通用
    v.push_back(mk(20, "cargoHold", "货舱模块", "提升运力。", H::Corvette, S::Utility, Fixed(1),
                   {{Commodity::Alloys, 25}}, E::Cargo, Fixed(200), -1));
    v.push_back(mk(21, "supplyTender", "补给舱", "延长舰队续航。", H::Frigate, S::Utility, Fixed::raw(1200),
                   {{Commodity::Polymers, 40}, {Commodity::Food, 60}}, E::Supply, Fixed(40), 3));
    v.push_back(mk(22, "sensorArray", "传感阵列", "提升侦测与先手。", H::Corvette, S::Utility, Fixed::raw(1300),
                   {{Commodity::Electronics, 35}}, E::Scan, Fixed(30), 4));
    v.push_back(mk(23, "cloakField", "隐匿场", "降低被发现概率。", H::Destroyer, S::Utility, Fixed::raw(2100),
                   {{Commodity::Exotic, 4}, {Commodity::DataCrystals, 20}}, E::Stealth, Fixed(45), 33));
    v.push_back(mk(24, "quantumComputer", "量子计算机", "实时解算战术。", H::Cruiser, S::Utility, Fixed::raw(1900),
                   {{Commodity::DataCrystals, 30}, {Commodity::Electronics, 70}}, E::Firepower, Fixed(18), 13));
    v.push_back(mk(25, "medicalBay", "医疗舱", "降低船员伤亡，提高士气。", H::Frigate, S::Utility, Fixed::raw(1100),
                   {{Commodity::Medicines, 40}, {Commodity::Bioproducts, 30}}, E::Supply, Fixed(25), 14));
    v.push_back(mk(26, "forgeShip", "舰载熔炉", "就地补给与生产。", H::Battleship, S::Utility, Fixed::raw(2000),
                   {{Commodity::Alloys, 120}, {Commodity::Robotics, 25}}, E::Supply, Fixed(60), 26));
    v.push_back(mk(27, "diplomaticSuite", "外交舱", "可作为移动谈判场所。", H::Cruiser, S::Utility,
                   Fixed::raw(1500), {{Commodity::Luxury, 50}, {Commodity::Influence, 60}}, E::Scan, Fixed(15),
                   18));
    v.push_back(mk(28, "intelSuite", "情报舱", "随舰情报分析。", H::Destroyer, S::Utility, Fixed::raw(1700),
                   {{Commodity::DataCrystals, 25}, {Commodity::Electronics, 60}}, E::Scan, Fixed(35), 17));
    v.push_back(mk(29, "marketTerminal", "市场终端", "在舰队所在星系直接交易。", H::Frigate, S::Utility,
                   Fixed::raw(1300), {{Commodity::Electronics, 40}, {Commodity::Influence, 40}}, E::Cargo,
                   Fixed(120), 7));
    v.push_back(mk(30, "escortBeacon", "护航信标", "提升同星系友军防御。", H::Destroyer, S::Utility,
                   Fixed::raw(1200), {{Commodity::Alloys, 50}, {Commodity::Electronics, 30}}, E::Defense,
                   Fixed(18), 5));
    // 核心
    v.push_back(mk(31, "reactorMk1", "聚变堆", "标准舰载动力核心。", H::Corvette, S::Core, Fixed(1),
                   {{Commodity::Energy, 80}}, E::Speed, Fixed(8), -1));
    v.push_back(mk(32, "reactorMk2", "反物质堆", "功率密度翻倍。", H::Destroyer, S::Core, Fixed::raw(2200),
                   {{Commodity::Antimatter, 6}, {Commodity::Energy, 200}}, E::Speed, Fixed(20), 30));
    v.push_back(mk(33, "zeroPointCore", "零点能核心", "几乎无限的能源。", H::Titan, S::Core, Fixed::raw(3200),
                   {{Commodity::Exotic, 12}, {Commodity::Supermaterials, 90}}, E::Speed, Fixed(34), 60));
    v.push_back(mk(34, "commandBridge", "指挥舰桥", "提升整支舰队的协同。", H::Battleship, S::Core,
                   Fixed::raw(2000), {{Commodity::DataCrystals, 40}, {Commodity::Influence, 80}}, E::Firepower,
                   Fixed(22), 19));
    v.push_back(mk(35, "titanCore", "泰坦核心", "仅泰坦级可用，决定性武器平台。", H::Titan, S::Core,
                   Fixed::raw(4000), {{Commodity::Antimatter, 30}, {Commodity::Exotic, 20}}, E::Firepower,
                   Fixed(120), 61));
    return v;
}

// 每种舰体有明确的**战场定位**，而不只是数值不同：
// 屏卫吸收火力、突击高火力低防御、战列均衡、母舰远程投送、
// 隐匿侦察偷袭、攻坚专打恒星基地。编队搭配因此才有意义。
constexpr HullInfo kHulls[] = {
    {HullClass::Corvette, "corvette", "护卫舰", 1200, Fixed(30), Fixed(20), Fixed(40), Fixed(8), 3,
     Fixed(12), HullRole::Screen},
    {HullClass::Frigate, "frigate", "护卫舰-重型", 2600, Fixed(55), Fixed(45), Fixed(34), Fixed(14), 4,
     Fixed(22), HullRole::Stealth},
    {HullClass::Destroyer, "destroyer", "驱逐舰", 5400, Fixed(95), Fixed(80), Fixed(28), Fixed(22), 5,
     Fixed(40), HullRole::Striker},
    {HullClass::Cruiser, "cruiser", "巡洋舰", 12000, Fixed(170), Fixed(150), Fixed(22), Fixed(36), 6,
     Fixed(82), HullRole::Line},
    {HullClass::Battleship, "battleship", "战列舰", 30000, Fixed(320), Fixed(300), Fixed(16), Fixed(58),
     8, Fixed(180), HullRole::Siege},
    {HullClass::Titan, "titan", "泰坦", 90000, Fixed(680), Fixed(620), Fixed(10), Fixed(96), 10,
     Fixed(460), HullRole::Carrier},
};
static_assert(sizeof(kHulls) / sizeof(kHulls[0]) == static_cast<std::size_t>(HullClass::Count));

std::string_view kSlotNames[] = {"武器", "防御", "推进", "通用", "核心"};

}  // namespace

const ModuleInfo& moduleInfo(int idx) {
    static const std::vector<ModuleInfo> table = buildModules();
    if (idx < 0 || idx >= static_cast<int>(table.size())) idx = 0;
    return table[static_cast<std::size_t>(idx)];
}

const HullInfo& hullInfo(HullClass c) {
    std::size_t i = static_cast<std::size_t>(c);
    if (i >= static_cast<std::size_t>(HullClass::Count)) i = 0;
    return kHulls[i];
}

int moduleIndexByName(std::string_view s) {
    static const std::vector<ModuleInfo> table = buildModules();
    for (std::size_t i = 0; i < table.size(); ++i)
        if (iequals(table[i].idName, s) || table[i].nameZh == s) return static_cast<int>(i);
    return -1;
}

int hullIndexByName(std::string_view s) {
    for (int i = 0; i < static_cast<int>(HullClass::Count); ++i)
        if (iequals(kHulls[static_cast<std::size_t>(i)].idName, s) ||
            kHulls[static_cast<std::size_t>(i)].nameZh == s)
            return i;
    return -1;
}

std::string_view hullClassName(HullClass c) {
    std::size_t i = static_cast<std::size_t>(c);
    if (i >= static_cast<std::size_t>(HullClass::Count)) return "?";
    return kHulls[i].nameZh;
}

std::string_view moduleSlotName(ModuleSlot s) {
    std::size_t i = static_cast<std::size_t>(s);
    if (i >= static_cast<std::size_t>(ModuleSlot::Count)) return "?";
    return kSlotNames[i];
}

}  // namespace gf
