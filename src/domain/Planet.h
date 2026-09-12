#pragma once
// 行星
#include <string>
#include <vector>

#include "domain/Resource.h"
#include "util/Fixed.h"
#include "util/Ids.h"

namespace gf {

enum class PlanetType : u8 {
    Barren = 0, Rocky, Desert, Tundra, Ocean, Jungle, Arctic, Lava, Toxic, GasGiant, Asteroid,
    Gaia, Ecumenopolis, Machine, RingWorld, Habitat, Shattered, Count
};

struct Planet {
    u32 id = 0;
    u32 system = 0;
    std::string name;
    PlanetType type = PlanetType::Rocky;
    int size = 12;              // 1..25
    i64 pops = 0;               // 人口（单位：千人）
    Fixed habitability = Fixed::pct(50);
    Fixed stability = Fixed::pct(60);
    Fixed development = Fixed(0);   // 开发度 0..10
    Fixed unrest = Fixed(0);
    std::array<Fixed, kCommodityCount> yield{};   // 每 tick 基础产出
    std::vector<u32> buildings;                   // Building id 列表
    std::vector<u32> districts;                   // 区域类型 id
    bool capital = false;
    bool colonized = false;
    u32 settlementTicksLeft = 0;
    u64 settlementStartedTick = 0;  // 新据点的 12 季产能爬坡期，既有行星为 0
    // 帝国 id。默认必须是 kNoEmpire —— 用 0 作默认值会让无主行星
    // 被误判为「帝国 0（玩家）所有」，玩家因此可以在全星系任意行星上建造。
    u32 owner = kNoEmpire;
    Fixed devastation = Fixed(0);   // 战争破坏度
    u32 anomaly = 0;            // 异常点 id（0 = 无）
    Fixed garrison = Fixed(0);  // 地面防御

    /// 建造队列：建筑不再即时完成，需要工期。
    /// 早期版本 `build` 直接把建筑塞进 buildings 列表 —— 玩家可以在同一季
    /// 瞬间铺满所有行星，建造速率与产能加成因此完全失去意义。
    struct BuildOrder {
        u32 building = 0;      // 建筑 id
        u32 ticksLeft = 0;     // 剩余工期
        u32 totalTicks = 1;    // 总工期（用于进度显示）
        i64 paidCredits = 0;   // 已支付
        std::string note;      // 备注（如“扩建”）
    };
    std::vector<BuildOrder> buildQueue;

    /// 该行星是否正在建造（用于限制并行数）
    [[nodiscard]] bool isBuilding() const { return !buildQueue.empty(); }
};

[[nodiscard]] std::string_view planetTypeName(PlanetType t);
[[nodiscard]] PlanetType planetTypeFromName(std::string_view s);
/// 该类型的基础产出模板（每单位规模）
void planetBaseYield(PlanetType t, std::array<Fixed, kCommodityCount>& out);
/// 按本季开头的库存与需求节流后的自然产量。
[[nodiscard]] Fixed planetNaturalProduction(const Planet& p, u8 commodity, Fixed stock, Fixed demand);

}  // namespace gf
