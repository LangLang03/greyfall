#pragma once
// 政策系统（V3 / HOI4 式「法律」）
//
// 与 `edict` 的区别：
//   edict   —— 一次性、立即生效、按次付费
//   policy  —— 持久化、分组互斥、每季维护、推行需要过渡期
//
// 每个政策组同时只有一项生效；切换政策要付政治成本（影响力）并经历
// 过渡期（过渡期内无法再次切换，且效果按进度线性生效）。
#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "domain/Domestic.h"  // FactionKind
#include "domain/Species.h"   // ModKind
#include "util/Fixed.h"

namespace gf {

inline constexpr int kPolicyGroupCount = 5;

enum class PolicyGroup : u8 {
    Economic = 0,     // 经济体制
    Military,         // 军事体制
    Social,           // 社会体制
    Diplomatic,       // 外交路线
    Intelligence,     // 情报体制
    Count,
};

/// 单条政策效果（复用 ModKind 体系，与科技/决议一致）
struct PolicyEffect {
    ModKind kind = ModKind::Count;
    Fixed value = Fixed(0);
};

struct PolicyOption {
    u8 id = 0;
    PolicyGroup group = PolicyGroup::Economic;
    std::string_view idName;
    std::string_view nameZh;
    std::string_view desc;
    /// 推行时一次性消耗的影响力
    i64 influenceCost = 0;
    /// 每季信用点维护
    i64 upkeep = 0;
    /// 过渡期季数
    int transition = 1;
    std::array<PolicyEffect, 3> effects{};
    u8 effectCount = 0;
    /// 对派系满意度的影响（受益方 / 受损方）
    FactionKind favored = FactionKind::Count;
    FactionKind harmed = FactionKind::Count;
    Fixed factionDelta = Fixed(0);
};

/// 每组的政策状态
struct PolicyState {
    /// 每组当前「已生效」的选项（kNoPolicy = 尚未设定）
    std::array<u8, kPolicyGroupCount> active{};
    /// 正在推行的目标（kNoPolicy = 无）
    std::array<u8, kPolicyGroupCount> pending{};
    /// 推行剩余季数
    std::array<u8, kPolicyGroupCount> transitionLeft{};
    /// 累计推行次数
    u32 enactCount = 0;
};

inline constexpr u8 kNoPolicy = 0xFF;

[[nodiscard]] std::size_t policyOptionCount();
[[nodiscard]] const PolicyOption& policyOption(std::size_t idx);
[[nodiscard]] const PolicyOption* policyOptionById(u8 id);
[[nodiscard]] std::string_view policyGroupName(PolicyGroup g);
[[nodiscard]] PolicyGroup policyGroupFromName(std::string_view s);
/// 该组当前可选的选项
[[nodiscard]] std::vector<const PolicyOption*> policyOptionsIn(PolicyGroup g);
/// 按名字或编号查找
[[nodiscard]] const PolicyOption* policyFind(std::string_view key);

/// 政策提供的修正合计（含过渡期的线性生效）
[[nodiscard]] Fixed policyModifier(const struct GameState& st, u32 empire, ModKind kind);
/// 政策对某派系满意度的影响合计（瞬时值，用于展示）
[[nodiscard]] Fixed policyFactionImpact(const struct GameState& st, u32 empire, FactionKind f);
/// 政策决定的该派系**均衡满意度**。
/// 早期实现把 policyFactionImpact 作为每季线性漂移直接累加，
/// 导致满意度在数十季内必然饱和到 0% 或 100%，派系与议会机制随之失效。
/// 正确做法：政策决定一个均衡水平，满意度向其平滑收敛。
[[nodiscard]] Fixed policyFactionTarget(const struct GameState& st, u32 empire, FactionKind f);
/// 每季维护费合计
[[nodiscard]] i64 policyUpkeep(const struct GameState& st, u32 empire);
/// 初始化（开局为每组设定默认政策）
void policyInitDefaults(struct GameState& st, u32 empire);

/// 推行政策
[[nodiscard]] bool policyEnact(struct GameState& st, u32 empire, const PolicyOption& opt, std::string* err);
/// 每 tick 推进过渡期
void policyPhase(struct GameState& st);

/// AI 政策决策：非玩家帝国按伦理与处境选择并推行政策。
/// 没有这一步，20 项政策只对玩家生效，AI 永远停在默认设置上，
/// 政策系统的一半意义就没了。
void policyAiPhase(struct GameState& st);

/// 政策总览文本
[[nodiscard]] std::string policyText(const struct GameState& st, u32 empire);
/// 单个政策详情
[[nodiscard]] std::string policyDetailText(const struct GameState& st, u32 empire, const PolicyOption& opt);
/// 效果文本
[[nodiscard]] std::string policyEffectText(const PolicyOption& opt);

}  // namespace gf
