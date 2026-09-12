#pragma once
// 决议系统：主动决策 / 自动触发 / 纯增益 / 条件阻止 / 倒计时成败分叉 / 代价换利益
//
// 五种类型：
//   Active      主动发起：玩家执行 resolve <id>，支付代价后立即生效
//   Auto        自动触发：条件满足即生效（可能是增益，也可能是减益）
//   Preventable 可阻止：条件不满足会自动触发减益；条件满足则被阻止（并可能获得增益）
//   Countdown   倒计时：触发后开始计时，在期限内完成目标 → 增益，超时 → 减益
//   Tradeoff    牺牲换利：玩家主动发起，永久牺牲一项属性换取另一项
#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "domain/Species.h"
#include "util/Fixed.h"

namespace gf {

inline constexpr int kResolutionCount = 48;

enum class ResolutionKind : u8 {
    Active = 0,       // 主动发起（花代价换收益）
    Auto,             // 自动触发（条件满足即生效）
    Preventable,      // 需满足条件阻止自动触发
    Countdown,        // 倒计时：完成转增益，超时转减益
    Tradeoff,         // 永久牺牲一项换另一项
    Count,
};

/// 效果作用目标
enum class ResTarget : u8 {
    // —— 修正类：并入 empireModifier（百分数）——
    ResResearch = 0,
    ResBuild,
    ResTrade,
    ResGrowth,
    ResMilitary,
    ResStability,
    ResUnrest,
    ResDiplo,
    ResCredit,
    ResDetection,
    ResColony,
    ResManip,
    // —— 数值类：每 tick 增量 ——
    Treasury,
    Influence,
    Unity,
    Military,
    Economy,
    Capacity,
    // —— 战斗类 ——
    FleetPower,
    Count,
};

/// 单条效果
struct ResEffect {
    ResTarget target = ResTarget::Treasury;
    Fixed value = Fixed(0);
};

/// 触发/阻止条件。全部为「且」关系；设为零值表示不参与判定。
struct ResCondition {
    i64 minTick = 0;
    i16 requireTech = -1;        // 需要已完成该科技
    i16 forbidTech = -1;         // 需要未完成该科技
    Fixed unrestAbove = Fixed(0);      // 民怨 ≥ 该值
    Fixed stabilityBelow = Fixed(0);   // 稳定度 ≤ 该值（0 表示不判定）
    Fixed treasuryBelow = Fixed(0);    // 国库 ≤ 该值（0 表示不判定）
    Fixed legitimacyBelow = Fixed(0);
    i64 maxSystems = 0;                // 星系数 ≤（0 表示不判定）
    i64 minSystems = 0;                // 星系数 ≥
    u32 atWarWithAny = 0;              // 1 = 需要处于战争状态
    i16 stockBelowCommodity = -1;      // 该资源库存低于阈值
    i64 stockBelowQty = 0;
    // —— 良性状态条件（主要供「阻止」槽使用）——
    Fixed treasuryAbove = Fixed(0);     // 国库 ≥
    Fixed unrestBelow = Fixed(0);       // 民怨 ≤
    Fixed stabilityAbove = Fixed(0);    // 稳定度 ≥
    i16 stockAboveCommodity = -1;       // 该资源库存 ≥ 阈值
    i64 stockAboveQty = 0;
};

/// 主动发起所支付的代价
struct ResCost {
    i64 credits = 0;
    i64 influence = 0;
    i64 unity = 0;
    i64 ap = 1;
    i16 commodity = -1;   // 需要消耗的资源
    i64 qty = 0;
    /// 永久牺牲（Tradeoff 用）：把该目标按 value 永久调整
    ResTarget sacrificeTarget = ResTarget::Count;
    Fixed sacrificeValue = Fixed(0);
};

struct ResolutionDef {
    u8 id = 0;
    std::string_view idName;
    std::string_view nameZh;
    std::string_view desc;
    ResolutionKind kind = ResolutionKind::Active;
    /// 触发条件（Auto / Preventable / Countdown 用来判定是否发生）
    ResCondition trigger;
    /// 阻止条件（Preventable 用：满足则阻止触发）
    ResCondition prevent;
    /// 玩家执行 resolve 时的前置条件
    ResCondition require;
    ResCost cost;
    /// 生效效果
    ResEffect onActivate;    // 触发/发起时立即生效
    ResEffect onTick;        // 生效期间每 tick 持续
    int duration = 0;        // onTick 的持续季数（0 = 不持续）
    /// Countdown：期限与完成后的奖励 / 超时惩罚
    int countdownTicks = 0;
    ResEffect onComplete;
    ResEffect onFail;

    // ---- 互斥与依赖链 ----
    /// 互斥组（0 = 不属于任何组）。同组的决议不能同时生效 ——
    /// 例如「战时经济」与「平准基金」代表互相排斥的经济路线。
    u8 exclusionGroup = 0;
    /// 前置决议：必须已经生效过（在 history 中）
    std::array<u8, 2> requiresRes{};
    u8 requireCount = 0;
    /// 被本决议阻止的决议：本决议生效期间，这些决议无法触发
    std::array<u8, 2> blocksRes{};
    u8 blockCount = 0;
};

/// 互斥组名称（用于 UI）
[[nodiscard]] std::string_view resExclusionGroupName(u8 g);
/// 检查某决议当前能否被发起/触发。
/// 返回 false 时 *reason 给出人类可读的原因（互斥冲突 / 缺少前置 / 被阻止）。
[[nodiscard]] bool resCanActivate(const struct GameState& st, u32 empire, int defIdx,
                                  std::string* reason);

/// 生效中的效果实例
struct ActiveEffect {
    u16 defId = 0;
    ResTarget target = ResTarget::Treasury;
    Fixed value = Fixed(0);
    int ticksLeft = 0;      // -1 = 永久
    bool positive = true;
    std::string source;
};

/// 进行中的倒计时
struct CountdownState {
    u16 defId = 0;
    int ticksLeft = 0;
    /// 目标：在期限内把目标值推到 goal（例如稳定度 ≥ goal）
    ResTarget target = ResTarget::Count;
    Fixed goal = Fixed(0);
    bool completed = false;
    bool failed = false;
};

/// 每个帝国的决议状态
struct EmpireResolutions {
    std::vector<ActiveEffect> active;
    std::vector<u16> triggered;    // 已触发过的决议（避免重复）
    std::vector<u16> completed;    // 倒计时成功完成
    std::vector<u16> failed;       // 倒计时失败
    std::vector<CountdownState> countdowns;
};

[[nodiscard]] const ResolutionDef& resolutionDef(int idx);
[[nodiscard]] int resolutionIndexByName(std::string_view s);
[[nodiscard]] std::string_view resolutionKindName(ResolutionKind k);
[[nodiscard]] std::string_view resTargetName(ResTarget t);
/// 修正类目标 → ModKind 的映射（非修正类返回 ModKind::Count）
[[nodiscard]] ModKind resTargetToMod(ResTarget t);
/// 该效果是否为「正面」
[[nodiscard]] bool resEffectIsPositive(ResTarget t, Fixed value);
/// 条件是否满足
[[nodiscard]] bool resConditionMet(const struct GameState& st, const struct Empire& e, const ResCondition& c);
/// 条件的人类可读描述
[[nodiscard]] std::string resConditionText(const ResCondition& c);
/// 效果的人类可读描述
[[nodiscard]] std::string resEffectText(const ResEffect& ef, bool withSign = true);

}  // namespace gf
