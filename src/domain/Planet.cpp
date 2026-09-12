#include "domain/Planet.h"
#include <algorithm>

#include "util/Str.h"

namespace gf {
Fixed planetNaturalProduction(const Planet& p, u8 commodity, Fixed stock, Fixed demand) {
    if (commodity >= kCommodityCount) return Fixed(0);
    Fixed production = p.yield[commodity] * (Fixed(1) + p.development / Fixed(20)) *
                       (Fixed::pct(50) + p.stability / Fixed(2));
    production *= Fixed(1) - Fixed::pct(75) * Fixed(std::min<u32>(12, p.settlementTicksLeft)) / Fixed(12);
    if (demand.rawValue() > 0 && stock.rawValue() > (demand * Fixed(6)).rawValue()) {
        Fixed over = (stock - demand * Fixed(6)) / (demand * Fixed(14));
        production = production * (Fixed(1) - fxClamp(over, Fixed(0), Fixed(1)));
    }
    return fxMax(production, Fixed(0));
}

namespace {

constexpr const char* kTypeNames[] = {
    "荒芜", "岩质", "沙漠", "苔原", "海洋", "丛林", "极地", "熔岩", "毒气", "气态巨行星",
    "小行星带", "盖亚", "城市行星", "机械行星", "环世界", "栖息地", "碎裂世界",
};
static_assert(sizeof(kTypeNames) / sizeof(kTypeNames[0]) == static_cast<std::size_t>(PlanetType::Count));

constexpr const char* kTypeIds[] = {
    "barren", "rocky", "desert", "tundra", "ocean", "jungle", "arctic", "lava", "toxic", "gasgiant",
    "asteroid", "gaia", "ecumenopolis", "machine", "ringworld", "habitat", "shattered",
};

}  // namespace

std::string_view planetTypeName(PlanetType t) {
    std::size_t i = static_cast<std::size_t>(t);
    if (i >= static_cast<std::size_t>(PlanetType::Count)) return "?";
    return kTypeNames[i];
}

PlanetType planetTypeFromName(std::string_view s) {
    for (int i = 0; i < static_cast<int>(PlanetType::Count); ++i) {
        if (kTypeNames[i] == s || kTypeIds[i] == s) return static_cast<PlanetType>(i);
    }
    return PlanetType::Rocky;
}

void planetBaseYield(PlanetType t, std::array<Fixed, kCommodityCount>& out) {
    for (auto& v : out) v = Fixed(0);
    auto set = [&out](Commodity c, Fixed v) { out[static_cast<std::size_t>(c)] = v; };
    set(Commodity::Energy, Fixed(40));
    set(Commodity::Food, Fixed(20));
    // 所有殖民行星都有基础商业活动（信用点产出）
    set(Commodity::Credits, Fixed(25));
    switch (t) {
        case PlanetType::Barren:
            set(Commodity::Minerals, Fixed(30));
            set(Commodity::Relics, Fixed(6));            // 荒芜行星的前文明遗迹
            break;
        case PlanetType::Rocky:
            set(Commodity::Minerals, Fixed(55));
            set(Commodity::Alloys, Fixed(4));
            set(Commodity::Supermaterials, Fixed(3));    // 矿脉伴生的稀有材料
            break;
        case PlanetType::Desert:
            set(Commodity::Minerals, Fixed(40));
            set(Commodity::Volatiles, Fixed(25));
            set(Commodity::Robotics, Fixed(8));          // 干旱世界的自动化矿业
            break;
        case PlanetType::Tundra:
            set(Commodity::Food, Fixed(28));
            set(Commodity::Polymers, Fixed(18));
            break;
        case PlanetType::Ocean:
            set(Commodity::Food, Fixed(70));
            set(Commodity::Bioproducts, Fixed(30));
            break;
        case PlanetType::Jungle:
            set(Commodity::Food, Fixed(80));
            set(Commodity::Medicines, Fixed(22));
            set(Commodity::Bioproducts, Fixed(36));
            break;
        case PlanetType::Arctic:
            set(Commodity::RareGases, Fixed(26));
            set(Commodity::Volatiles, Fixed(20));
            set(Commodity::Antimatter, Fixed(3));        // 气态巨行星的对撞采集
            break;
        case PlanetType::Lava:
            set(Commodity::Minerals, Fixed(70));
            set(Commodity::Energy, Fixed(90));
            set(Commodity::Supermaterials, Fixed(5));    // 高温冶炼
            break;
        case PlanetType::Toxic:
            set(Commodity::RareGases, Fixed(42));
            set(Commodity::Polymers, Fixed(30));
            set(Commodity::Contraband, Fixed(6));        // 监管真空地带
            break;
        case PlanetType::GasGiant:
            set(Commodity::Volatiles, Fixed(90));
            set(Commodity::RareGases, Fixed(60));
            set(Commodity::Energy, Fixed(60));
            break;
        case PlanetType::Asteroid:
            set(Commodity::Minerals, Fixed(85));
            set(Commodity::Exotic, Fixed(2));
            break;
        case PlanetType::Gaia:
            set(Commodity::Food, Fixed(120));
            set(Commodity::Bioproducts, Fixed(60));
            set(Commodity::Luxury, Fixed(20));
            break;
        case PlanetType::Ecumenopolis:
            set(Commodity::Alloys, Fixed(60));
            set(Commodity::Components, Fixed(40));
            set(Commodity::Credits, Fixed(200));
            set(Commodity::Supermaterials, Fixed(14));   // 高阶制造业
            break;
        case PlanetType::Machine:
            set(Commodity::Robotics, Fixed(50));
            set(Commodity::Supermaterials, Fixed(8));
            set(Commodity::Electronics, Fixed(60));
            set(Commodity::DataCrystals, Fixed(12));     // 机器智能的算力结晶
            break;
        case PlanetType::RingWorld:
            set(Commodity::Food, Fixed(200));
            set(Commodity::Luxury, Fixed(50));
            set(Commodity::Antimatter, Fixed(6));        // 环世界尺度的大型对撞设施
            break;
        case PlanetType::Habitat:
            set(Commodity::Electronics, Fixed(45));
            set(Commodity::Components, Fixed(25));
            set(Commodity::DataCrystals, Fixed(8));
            break;
        case PlanetType::Shattered:
            set(Commodity::Exotic, Fixed(12));
            set(Commodity::Relics, Fixed(4));
            set(Commodity::Contraband, Fixed(10));       // 法外之地
            break;
        case PlanetType::Count:
            break;
    }
}

}  // namespace gf
