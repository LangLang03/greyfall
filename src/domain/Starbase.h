#pragma once
// 恒星基地：星系级的永久设施
//
// 与行星建筑的区别：
//   行星建筑建在**行星**上，一格一座，产出资源；
//   恒星基地建在**星系**上，一座星系一座，是主权的实体标志 ——
//   它提供防御、扩展控制范围、并为舰队提供补给与维修。
//
// 升级链：前哨站 → 星港 → 堡垒 → 要塞 → 星际要塞
// 等级越高，防御越强、补给与维修加成越大、可支持的舰队规模越大。
#include <string>
#include <vector>

#include "util/Fixed.h"

namespace gf {

struct GameState;
struct Empire;

enum class StarbaseTier : u8 {
    Outpost = 0,     // 前哨站：宣示主权
    Starport,        // 星港：可维修与补给
    Fortress,        // 堡垒：具备实质防御
    Citadel,         // 要塞：区域防御核心
    StarFortress,    // 星际要塞：几乎不可攻克
    Count,
};

[[nodiscard]] std::string_view starbaseTierName(StarbaseTier t);
[[nodiscard]] std::string starbaseTierDesc(StarbaseTier t);
/// 升级到该等级所需的信用点与合金
[[nodiscard]] i64 starbaseUpgradeCredits(StarbaseTier t);
[[nodiscard]] i64 starbaseUpgradeAlloys(StarbaseTier t);
/// 该等级的防御贡献（加入星系防御）
[[nodiscard]] Fixed starbaseDefense(StarbaseTier t);
/// 该等级的舰队补给/维修加成
[[nodiscard]] Fixed starbaseSupplyBonus(StarbaseTier t);

struct Starbase {
    u32 system = 0xFFFFFFFFu;
    u32 owner = 0xFFFFFFFFu;
    StarbaseTier tier = StarbaseTier::Outpost;
    u64 builtTick = 0;
    /// 累计承受过的攻击次数（用于展示「久经战阵」）
    u32 siegesSurvived = 0;
};

/// 在某星系建立前哨站
[[nodiscard]] bool starbaseFound(GameState& st, u32 empire, u32 system, std::string* msg);
/// 升级某星系的恒星基地
[[nodiscard]] bool starbaseUpgrade(GameState& st, u32 empire, u32 system, std::string* msg);
/// 拆除
[[nodiscard]] bool starbaseDismantle(GameState& st, u32 empire, u32 system, std::string* msg);
/// 查询某星系的恒星基地（nullptr = 无）
[[nodiscard]] const Starbase* starbaseAt(const GameState& st, u32 system);
/// 某帝国恒星基地的合计防御（加入 combatOdds 的防守方）
[[nodiscard]] Fixed starbaseDefenseOf(const GameState& st, u32 empire);
/// 某帝国恒星基地提供的补给加成
[[nodiscard]] Fixed starbaseSupplyOf(const GameState& st, u32 empire);

/// 每 tick：被攻陷的星系，其恒星基地易主或降级
void starbasePhase(GameState& st);

/// 文本
[[nodiscard]] std::string starbaseReport(const GameState& st, u32 empire);
/// 某星系的基地简述（供 starmap --system 使用）
[[nodiscard]] std::string starbaseBrief(const GameState& st, u32 system);

}  // namespace gf
