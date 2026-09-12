#pragma once
// 舰船设计与模块
#include <string>
#include <string_view>
#include <vector>

#include "domain/Resource.h"
#include "util/Fixed.h"

namespace gf {

struct FleetDesign;
struct Empire;
struct GameState;

inline constexpr int kModuleCount = 36;

enum class HullClass : u8 { Corvette = 0, Frigate, Destroyer, Cruiser, Battleship, Titan, Count };

enum class ModuleSlot : u8 { Weapon = 0, Defense, Drive, Utility, Core, Count };

enum class ModuleEffect : u8 {
    Firepower, Defense, Speed, Supply, Cargo, Scan, Stealth, RepairCost, BuildCost, JumpRange, Count
};

struct ModuleInfo {
    u8 id;
    std::string_view idName;
    std::string_view nameZh;
    std::string_view desc;
    HullClass minHull;
    ModuleSlot slot;
    Fixed costMul;
    std::array<Fixed, kCommodityCount> buildCost{};
    ModuleEffect effect;
    Fixed effectValue;
    i16 unlockTech;
};

/// 舰体角色：同样吨位下定位不同，决定编队搭配
enum class HullRole : u8 {
    Screen = 0,    // 屏卫：廉价、量大、掩护主力
    Striker,       // 突击：高火力、低防御
    Line,          // 战列：攻防均衡的主力
    Carrier,       // 母舰：搭载舰载机，远程投送
    Stealth,       // 隐匿：侦察与偷袭
    Siege,         // 攻坚：对恒星基地与巨构特化
    Count,
};

[[nodiscard]] std::string_view hullRoleName(HullRole r);
[[nodiscard]] std::string hullRoleDesc(HullRole r);

struct HullInfo {
    HullClass cls;
    std::string_view idName;
    std::string_view nameZh;
    i64 baseCost;          // credits
    Fixed baseFirepower;
    Fixed baseDefense;
    Fixed baseSpeed;
    Fixed baseSupply;
    int slots;             // 模块槽数
    Fixed maintenance;     // 每 tick 维护
    HullRole role = HullRole::Line;   // 定位
};

[[nodiscard]] const ModuleInfo& moduleInfo(int idx);
[[nodiscard]] const HullInfo& hullInfo(HullClass c);
[[nodiscard]] int moduleIndexByName(std::string_view s);


}  // namespace gf
