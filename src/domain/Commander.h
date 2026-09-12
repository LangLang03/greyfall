#pragma once
// 指挥官与战斗（HOI4 风格：组织度驱动的多 tick 战斗 + 战斗宽度 + 指挥官特质 + 经验）
#include <string>
#include <string_view>
#include <vector>

#include "util/Fixed.h"

namespace gf {

/// 指挥官特质
enum class CommanderTrait : u8 {
    None = 0,
    Offensive,    // 攻势：进攻加成
    Defensive,    // 防御：防守加成
    Logistician,  // 后勤：补给消耗降低
    Maneuver,     // 机动：行军速度与先手
    Trickster,    // 诡道：战斗宽度惩罚对方
    Inspiring,    // 鼓舞：组织度恢复加快
    Siege,        // 攻坚：对要塞/巨构加成
    Count,
};

/// 军衔：由累计战功晋升（HOI4 式）
enum class CommanderRank : u8 {
    Captain = 0,   // 上尉
    Major,         // 少校
    Colonel,       // 上校
    General,       // 少将
    Marshal,       // 元帅
    Count,
};
inline constexpr int kCommanderRankCount = static_cast<int>(CommanderRank::Count);

/// 一名指挥官最多可习得的特质数（军衔越高越多）
inline constexpr int kMaxCommanderTraits = 3;

struct Commander {
    u32 id = 0;
    std::string name;
    u32 owner = 0;
    CommanderTrait trait = CommanderTrait::None;
    /// 军衔（由战功晋升）
    CommanderRank rank = CommanderRank::Captain;
    /// 已习得的额外特质（最多 kMaxCommanderTraits 个，不含主特质）
    std::vector<CommanderTrait> traits;
    /// 距下次晋升的累计战功
    Fixed merit = Fixed(0);
    Fixed attack = Fixed::pct(50);     // 0..1 技能
    Fixed defense = Fixed::pct(50);
    Fixed logistics = Fixed::pct(50);
    Fixed planning = Fixed::pct(50);   // 规划：累积战斗准备加成
    /// 隶属舰队（kNoFleet 表示未分配）
    u32 fleet = 0xFFFFFFFFu;
    u32 battlesWon = 0;
    u32 battlesLost = 0;
    Fixed experience = Fixed(0);       // 0..1 累积经验
};

/// 多 tick 战斗实例
struct Battle {
    u32 id = 0;
    u32 system = 0;
    u32 attacker = 0;
    u32 defender = 0;
    u64 startTick = 0;
    int ticks = 0;

    /// 双方投入的舰队
    std::vector<u32> attackerFleets;
    std::vector<u32> defenderFleets;
    /// 指挥官
    u32 attackerCommander = 0xFFFFFFFFu;
    u32 defenderCommander = 0xFFFFFFFFu;

    /// 战斗宽度：每方同时能展开的战力上限
    Fixed attackerWidth = Fixed(0);
    Fixed defenderWidth = Fixed(0);
    Fixed widthCap = Fixed(1200);
    /// 战斗宽度惩罚（混乱/被压制）
    Fixed attackerPenalty = Fixed(0);
    Fixed defenderPenalty = Fixed(0);

    /// 累计损失
    Fixed attackerLoss = Fixed(0);
    Fixed defenderLoss = Fixed(0);
    Fixed attackerOrgLoss = Fixed(0);
    Fixed defenderOrgLoss = Fixed(0);

    /// 推进度：0..1，达到 1 时进攻方夺取星系
    Fixed progress = Fixed(0);
    /// 地形修正
    Fixed terrainDefense = Fixed(0);
    std::string terrainName;
    bool resolved = false;
    bool attackerWon = false;
    /// 结束原因
    std::string outcome;
};

[[nodiscard]] std::string_view commanderTraitName(CommanderTrait t);
[[nodiscard]] CommanderTrait commanderTraitFromName(std::string_view s);
/// 特质对攻/防/后勤的加成
[[nodiscard]] Fixed traitAttackBonus(CommanderTrait t);
[[nodiscard]] Fixed traitDefenseBonus(CommanderTrait t);
[[nodiscard]] Fixed traitLogisticsBonus(CommanderTrait t);
[[nodiscard]] Fixed traitWidthFactor(CommanderTrait t);

/// 老练度等级（0 新兵 .. 4 精锐）
[[nodiscard]] int veterancyLevel(Fixed experience);
[[nodiscard]] std::string_view veterancyName(int level);
/// 老练度对战斗力的乘数
[[nodiscard]] Fixed veterancyMultiplier(int level);

// ---- 军衔与晋升 ----
[[nodiscard]] std::string_view commanderRankName(CommanderRank r);
[[nodiscard]] CommanderRank commanderRankFromName(std::string_view s);
/// 晋升到该军衔所需的累计战功
[[nodiscard]] Fixed rankMeritRequired(CommanderRank r);
/// 军衔带来的技能上限提升
[[nodiscard]] Fixed rankSkillCap(CommanderRank r);
/// 军衔带来的统率加成（影响展开宽度）
[[nodiscard]] Fixed rankCommandBonus(CommanderRank r);
/// 该军衔允许的特质数
[[nodiscard]] int rankTraitSlots(CommanderRank r);

/// 授予战功并处理晋升与特质获取。返回晋升到的军衔（未晋升则为当前军衔）
CommanderRank commanderAwardMerit(struct GameState& st, u32 commanderId, Fixed merit);
/// 该指挥官是否拥有某特质（含主特质与已习得特质）
[[nodiscard]] bool commanderHasTrait(const Commander& c, CommanderTrait t);
/// 特质加成合计：主特质 + 已习得特质
[[nodiscard]] Fixed commanderTraitAttack(const Commander& c);
[[nodiscard]] Fixed commanderTraitDefense(const Commander& c);
[[nodiscard]] Fixed commanderTraitLogistics(const Commander& c);
[[nodiscard]] Fixed commanderTraitWidth(const Commander& c);
/// 花费影响力进行「军校深造」：立即提升技能并给予战功
[[nodiscard]] bool commanderTrain(struct GameState& st, u32 commanderId, Fixed influenceCost,
                                  std::string* err);
/// 军衔文本（含特质与战功）
[[nodiscard]] std::string commanderRankText(const Commander& c);

}  // namespace gf
