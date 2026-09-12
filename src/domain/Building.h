#pragma once
// 建筑与巨构
#include <string>
#include <string_view>
#include <vector>

#include "domain/Resource.h"
#include "util/Fixed.h"

namespace gf {

inline constexpr int kBuildingCount = 28;
inline constexpr int kMegastructureCount = 6;

enum class BuildingEffect : u8 {
    ProdEnergy, ProdMinerals, ProdFood, ProdAlloys, ProdComponents, ProdResearch, ProdCredits,
    ProdUnity, ProdInfluence, Stability, Defense, Trading, Shipyard, Storage, ClueDiscovery, ProdMedicines, Count,
};

struct BuildingInfo {
    u8 id;
    std::string_view idName;
    std::string_view nameZh;
    std::string_view desc;
    BuildingEffect effect;
    Fixed effectValue;
    std::array<i64, kCommodityCount> cost{};
    i64 creditCost;
    i64 upkeep;
    u8 tier;
    i16 requireTech;
    bool unique;      // 每行星限一
};

struct MegastructureInfo {
    u8 id;
    std::string_view idName;
    std::string_view nameZh;
    std::string_view desc;
    std::array<i64, kCommodityCount> cost{};
    i64 creditCost;
    int stages;
    i16 requireTech;
    int apCost;         // 每阶段推进消耗 AP
    Fixed effectValue;
    BuildingEffect effect;
};

struct MegastructureBuild {
    u32 id = 0;
    u8 defId = 0;
    u32 system = 0;
    u32 owner = 0;
    int stage = 0;
    Fixed progress = Fixed(0);
    bool complete = false;
    u64 startedTick = 0;
};

[[nodiscard]] const BuildingInfo& buildingInfo(int idx);
[[nodiscard]] const MegastructureInfo& megastructureInfo(int idx);
[[nodiscard]] int buildingIndexByName(std::string_view s);
[[nodiscard]] int megastructureIndexByName(std::string_view s);
[[nodiscard]] std::string_view buildingEffectName(BuildingEffect e);

}  // namespace gf
