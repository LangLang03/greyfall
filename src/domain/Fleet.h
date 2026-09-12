#pragma once
// 舰队与订单
#include <string>
#include <string_view>
#include <vector>

#include "domain/FleetDesign.h"
#include "util/Fixed.h"
#include "util/Ids.h"

namespace gf {

enum class FleetOrder : u8 { Idle = 0, Move, Patrol, Embargo, Engage, Escort, Blockade, Retreat, Count };

struct FleetDesign {
    u32 id = 0;
    std::string name;
    HullClass hull = HullClass::Corvette;
    std::vector<u8> modules;
    Fixed firepower = Fixed(0);
    Fixed defense = Fixed(0);
    Fixed speed = Fixed(0);
    Fixed supplyUse = Fixed(0);
    std::array<i64, kCommodityCount> buildCost{};
    i64 creditCost = 0;
    /// 该设计是否为玩家自定义（用于 ship <design> 查询）
    bool custom = false;
};

struct Fleet {
    u32 id = 0;
    std::string name;
    u32 owner = 0;
    u32 design = 0;          // FleetDesign id
    u32 system = 0;          // 当前星系
    u32 targetSystem = 0;    // 目标星系（kNoSystem 表示无）
    FleetOrder order = FleetOrder::Idle;
    Fixed strength = Fixed(100);   // 综合战力（随损耗变化）
    Fixed morale = Fixed::pct(80);
    Fixed supply = Fixed::pct(100);
    Fixed stealth = Fixed(0);
    i64 upkeep = 0;
    bool veteran = false;

    // ---- HOI4 风格战斗属性 ----
    /// 组织度：部队的「意志」。组织度归零即退出战斗/撤退，
    /// 兵力（strength）只决定伤害承受。这是战斗可持续多 tick 的关键。
    Fixed org = Fixed(100);
    Fixed maxOrg = Fixed(100);
    /// 累积经验 0..1000，决定老练度等级
    Fixed experience = Fixed(0);
    /// 隶属指挥官
    u32 commander = 0xFFFFFFFFu;
    /// 是否正在战斗中
    u32 battle = 0xFFFFFFFFu;
    /// 战斗准备度：静止时累积，参与进攻时消耗（HOI4 的 planning bonus）
    Fixed planning = Fixed(0);
};

// kNoSystem / kNoFleet 统一定义在 util/Ids.h

[[nodiscard]] std::string_view fleetOrderName(FleetOrder o);
[[nodiscard]] FleetOrder fleetOrderFromName(std::string_view s);

/// 重算设计的派生属性（模块变动后必须调用）
void recomputeDesign(FleetDesign& d);

/// 新建一份设计：指定舰体与名称。返回新设计的 id（失败返回 kNoDesign）
[[nodiscard]] u32 designCreate(struct Empire& e, HullClass hull, const std::string& name,
                               std::string* err);

/// 在设计上安装一个模块（受舰体等级与槽位限制）
[[nodiscard]] bool designInstallModule(struct Empire& e, u32 designId, int moduleIdx,
                                       std::string* err);
/// 从设计上卸下第 slot 个模块（按安装顺序）
[[nodiscard]] bool designRemoveModule(struct Empire& e, u32 designId, int slot, std::string* err);
/// 清空设计的全部模块
[[nodiscard]] bool designClearModules(struct Empire& e, u32 designId, std::string* err);
/// 把现役舰队改造为新设计（消耗合金与信用点）
[[nodiscard]] bool fleetRefit(struct GameState& st, u32 empire, u32 fleetId, u32 designId,
                              std::string* err);
[[nodiscard]] int hullIndexByName(std::string_view s);
[[nodiscard]] std::string_view hullClassName(HullClass c);
[[nodiscard]] std::string_view moduleSlotName(ModuleSlot s);

}  // namespace gf
