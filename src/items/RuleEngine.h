#pragma once
// RuleEngine —— 道具的声明式规则图求值（每 tick + 每次动作后迭代到不动点 ≤16 轮）
#include "core/GameState.h"
#include "items/ItemDef.h"

namespace gf {

/// 规则求值结果（全为倍率/加成，默认 0 或 1）
struct RuleResult {
    Fixed negotiationDiscount = Fixed(0);
    Fixed tradeMargin = Fixed(0);
    Fixed intelGain = Fixed(0);
    Fixed intelDefense = Fixed(0);
    Fixed diplomacyWeight = Fixed(0);
    Fixed combatBonus = Fixed(0);
    Fixed transportSlippage = Fixed(0);
    Fixed beliefNoise = Fixed(0);
    Fixed clueYield = Fixed(0);
    Fixed researchBoost = Fixed(0);
    Fixed stabilityBoost = Fixed(0);
    Fixed marginRelief = Fixed(0);
    Fixed creditBoost = Fixed(0);
    /// Conflict：[伪造] 与 [审计] 同持 → FraudDetect 权重 ×2
    Fixed fraudWeight = Fixed(1);
    int rounds = 0;
    std::vector<std::string> trace;
};

/// 迭代到不动点（≤16 轮，优先级 tag 模式匹配）
[[nodiscard]] RuleResult ruleEngine(const GameState& st);

/// 门槛检查：无所需 tag 则 action 不可执行
[[nodiscard]] bool itemGateOpen(const GameState& st, std::string_view action);
/// 门槛的说明（用于报错）
[[nodiscard]] std::string itemGateReason(std::string_view action);

/// 合成：A + B → C（失败率受 tag 冲突影响）
[[nodiscard]] bool combineItems(GameState& st, u16 a, u16 b, u16* out, std::string* err);

/// 拆解回材
[[nodiscard]] bool disassembleItem(GameState& st, u16 item, std::string* err);

/// 使用道具（触发效果，含市场干预与信念干预）
[[nodiscard]] bool useItem(GameState& st, u16 item, u32 target, std::string* err);

/// 造假冒牌：以假数据污染 AI 的 ToModel 与 FraudDetect
[[nodiscard]] bool forgeProveItem(GameState& st, u16 item, std::string* err);

/// item --explain 的求值轨迹
[[nodiscard]] std::string itemExplain(const GameState& st, u16 item);

/// 库存清单文本
[[nodiscard]] std::string inventoryText(const GameState& st, const std::string& tagFilter);

/// 按 tag 查找玩家持有的第一件道具（-1 表示无）
[[nodiscard]] int findItemByTag(const GameState& st, ItemTag tag);

}  // namespace gf
