#pragma once
// 道具系统：声明式规则图的数据定义（规则求值在 items/RuleEngine）
#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "domain/Provenance.h"
#include "util/Fixed.h"

namespace gf {

inline constexpr int kItemCount = 130;
inline constexpr int kRecipeCount = 120;
inline constexpr int kSynergyCount = 70;
inline constexpr int kConflictCount = 45;
inline constexpr int kGateCount = 40;
inline constexpr int kClueBridgeCount = 60;
inline constexpr int kMaxItemTags = 6;

enum class ItemTag : u8 {
    Contract = 0,     // 契约
    Observation,      // 观测
    Forgery,          // 伪造
    Audit,            // 审计
    Barrier,          // 屏障
    Market,           // 市场干预
    Belief,           // 信念干预
    Clue,             // 线索桥
    Carrier,          // 载体
    Currency,         // 金融
    Medical,          // 医疗
    Military,         // 军用
    Data,             // 数据
    Relic,            // 遗物
    Contraband,       // 违禁
    Count,
};
inline constexpr int kItemTagCount = static_cast<int>(ItemTag::Count);

enum class ItemEffect : u8 {
    None = 0,
    NegotiationDiscount,   // 谈判折价
    TradeMargin,           // 贸易毛利加成
    IntelGain,             // 情报获取
    IntelDefense,          // 反间谍
    DiplomacyWeight,       // 外交权重
    CombatBonus,           // 战力系数
    TransportSlippage,     // 运输滑点
    MarketFakeStock,       // 制造假库存（不影响真实簿）
    BeliefNoise,           // 降低 AI modelConfidence / 注入梯度噪声
    ClueYield,             // 产出线索
    ResearchBoost,
    StabilityBoost,
    MarginRelief,          // 保证金减免
    CreditBoost,
    Count,
};

struct ItemDef {
    u16 id = 0;
    std::string_view idName;
    std::string_view nameZh;
    std::string_view desc;
    std::array<ItemTag, kMaxItemTags> tags{};
    u8 tagCount = 0;
    u8 tier = 1;              // 1..5
    i64 baseCost = 0;         // credits
    ItemEffect effect = ItemEffect::None;
    Fixed effectValue{};
    bool consumable = false;
    bool tradeable = true;
};

struct ItemInstance {
    u16 def = 0;
    u32 count = 1;
    Provenance prov;
    bool forged = false;
    /// 伪造时的"污染强度"（进入 AI 的 FraudDetect 打分）
    Fixed contamination = Fixed(0);
    u64 acquiredTick = 0;
};

struct Inventory {
    std::vector<ItemInstance> items;
};

/// 配方：A + B → C，可多输入；失败率受 tag 冲突影响
struct RecipeInfo {
    u16 id = 0;
    std::string_view idName;
    std::vector<u16> inputs;
    u16 output = 0;
    u8 outputCount = 1;
    u8 depth = 1;             // 配方链深度（≤4）
    Fixed baseFailRate = Fixed::pct(10);
};

/// 协同：同持生效
struct SynergyInfo {
    u16 id = 0;
    std::string_view idName;
    ItemTag tagA = ItemTag::Contract;
    ItemTag tagB = ItemTag::Observation;
    ItemEffect effect = ItemEffect::None;
    Fixed value{};
    std::string_view desc;
};

/// 互斥相克
struct ConflictInfo {
    u16 id = 0;
    ItemTag tagA = ItemTag::Forgery;
    ItemTag tagB = ItemTag::Audit;
    Fixed penalty{};          // 例如 FraudDetect 权重 ×2 ⇒ penalty = 1.0 表示 +100%
    std::string_view desc;
};

/// 门槛：无 tagA 则不可执行 action
struct GateInfo {
    u16 id = 0;
    ItemTag required = ItemTag::Barrier;
    std::string_view action;  // "spy.psionic" / "breach" 等
    std::string_view desc;
};

/// 线索桥：使用道具产出特定线索
struct ClueBridgeInfo {
    u16 id = 0;
    u16 item = 0;
    u16 clue = 0;
    Fixed credibilityStart = Fixed::pct(60);
    std::string_view desc;
};

[[nodiscard]] const ItemDef& itemDef(int idx);
[[nodiscard]] int itemIndexByName(std::string_view s);
[[nodiscard]] std::string_view itemTagName(ItemTag t);
[[nodiscard]] ItemTag itemTagFromName(std::string_view s);
[[nodiscard]] std::string_view itemEffectName(ItemEffect e);
[[nodiscard]] const RecipeInfo& recipeInfo(int idx);
[[nodiscard]] const SynergyInfo& synergyInfo(int idx);
[[nodiscard]] const ConflictInfo& conflictInfo(int idx);
[[nodiscard]] const GateInfo& gateInfo(int idx);
[[nodiscard]] const ClueBridgeInfo& clueBridgeInfo(int idx);
[[nodiscard]] bool itemHasTag(const ItemDef& d, ItemTag t) noexcept;

}  // namespace gf
