#pragma once
// 人事体系：领袖、科学家、编队与集团军
//
// 与已有的 Commander 的关系：
//   Commander  —— 舰队指挥官（战功晋升、特质、技能）
//   本模块在其之上补三层：
//     * Ruler      —— 国家领袖：给全国提供长期修正，有任期与继承
//     * Scientist  —— 科研部科学家：分管一个研究分支，加速立项推进
//     * Formation  —— 编队/集团军：把多支舰队编成一体，由集团军司令统率，
//                     获得协同与后勤加成（单打独斗的舰队没有这些）
#include <string>
#include <vector>

#include "util/Fixed.h"

namespace gf {

struct GameState;
struct Empire;

// ---------------------------------------------------------------------------
// 领袖
// ---------------------------------------------------------------------------
enum class RulerTrait : u8 {
    None = 0,
    Administrator,   // 行政干才：建造与稳定
    Warlord,         // 军事强人：军力与战争疲劳抗性
    Scientist,       // 学者出身：研究速率
    Merchant,        // 商界背景：贸易与信用
    Demagogue,       // 煽动家：影响力与民粹支持
    Reformer,        // 改革者：政策过渡更快、合法性更高
    Count,
};

[[nodiscard]] std::string_view rulerTraitName(RulerTrait t);
[[nodiscard]] std::string rulerTraitDesc(RulerTrait t);

struct Ruler {
    std::string name;
    RulerTrait trait = RulerTrait::None;
    Fixed skill = Fixed::pct(50);      // 0..1 能力
    u64 reignStart = 0;
    u32 age = 40;
    /// 非民主政体：无固定任期；民主政体：到期改选
    bool elected = false;
    u32 termEnd = 0;
    u32 electionsWon = 0;
};

/// 领袖带来的国家修正
void rulerModifiers(const GameState& st, u32 empire, std::vector<std::pair<int, Fixed>>* out);

/// 每 tick：任期、改选、自然更替
void rulerPhase(GameState& st);

// ---------------------------------------------------------------------------
// 科学家
// ---------------------------------------------------------------------------
enum class ScientistField : u8 {
    Physics = 0,
    Society,
    Engineering,
    Biology,
    Computing,
    Psionics,
    Count,
};

[[nodiscard]] std::string_view scientistFieldName(ScientistField f);

struct Scientist {
    u32 id = 0;
    std::string name;
    u32 owner = 0;
    ScientistField field = ScientistField::Physics;
    Fixed skill = Fixed::pct(40);       // 0..1
    /// 当前负责的分支（0xFF = 空闲）
    u8 assignedBranch = 0xFF;
    u32 projectsCompleted = 0;
    u64 recruitedTick = 0;
};

/// 招募一名科学家（消耗影响力）
[[nodiscard]] bool scientistRecruit(GameState& st, u32 empire, std::string* msg);
/// 把科学家派到某个研究分支
[[nodiscard]] bool scientistAssign(GameState& st, u32 empire, u32 scientistId, int branch,
                                   std::string* msg);
/// 科学家对该分支立项的进度加成（0..1 的比例）
[[nodiscard]] Fixed scientistBonus(const GameState& st, u32 empire, int branch);

// ---------------------------------------------------------------------------
// 编队 / 集团军
// ---------------------------------------------------------------------------
struct Formation {
    u32 id = 0;
    std::string name;
    u32 owner = 0;
    std::vector<u32> fleets;
    /// 集团军司令（指挥官 id，kNoCommander 表示未指定）
    u32 commander = 0xFFFFFFFFu;
    Fixed coordination = Fixed(0);    // 协同度：随共同作战提升
    u64 createdTick = 0;
    u32 battlesWon = 0;
};

/// 新建集团军
[[nodiscard]] bool formationCreate(GameState& st, u32 empire, const std::string& name,
                                   std::string* msg);
/// 把舰队编入集团军
[[nodiscard]] bool formationAddFleet(GameState& st, u32 empire, u32 formationId, u32 fleetId,
                                     std::string* msg);
/// 从集团军移出舰队
[[nodiscard]] bool formationRemoveFleet(GameState& st, u32 empire, u32 formationId, u32 fleetId,
                                        std::string* msg);
/// 任命集团军司令
[[nodiscard]] bool formationAssignCommander(GameState& st, u32 empire, u32 formationId,
                                            u32 commanderId, std::string* msg);
/// 解散集团军
[[nodiscard]] bool formationDisband(GameState& st, u32 empire, u32 formationId, std::string* msg);
/// 某舰队所属的集团军（nullptr = 未编入）
[[nodiscard]] const Formation* formationOfFleet(const GameState& st, u32 fleetId);
/// 集团军带来的战力倍率（协同 + 司令加成）
[[nodiscard]] Fixed formationPowerMultiplier(const GameState& st, u32 fleetId);

/// 每 tick：协同度演化（共同作战提升、长期闲置衰减）
void formationPhase(GameState& st);

/// 文本
[[nodiscard]] std::string rulerReport(const GameState& st, u32 empire);
[[nodiscard]] std::string scientistReport(const GameState& st, u32 empire);
[[nodiscard]] std::string formationReport(const GameState& st, u32 empire);

}  // namespace gf
