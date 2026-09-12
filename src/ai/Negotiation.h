#pragma once
// 谈判系统
//
// 设计要点：
//   1. AI 不主动倾向于谈判。只有当「谈判权重」因领土、经济、制裁、战争、
//      军事劣势等累积到阈值以上时，它才愿意坐上谈判桌。
//   2. 谈判条款可交换，也可单方面索取：资源、人力、科技、领土、资金、影响力。
//   3. 若一方首都已被占领且已放弃抵抗，对方必须无条件接受任何条款。
//   4. 索取超出对方支付能力时，由盟友按可支付额度代付（义务转移到盟友身上）。
#include <string>
#include <string_view>
#include <vector>

#include "core/GameState.h"
#include "util/Fixed.h"

namespace gf {

/// 条款类型
enum class TermKind : u8 {
    Credits = 0,
    Influence,
    Unity,
    Commodity,   // extra = 资源编号，amount = 数量
    Tech,        // amount = 科技编号
    System,      // amount = 星系编号
    Manpower,    // amount = 人口（千人）
    Count,
};

/// 交易评价档次：AI 对一份提案的观感
enum class DealRating : u8 {
    Reasonable = 0,     // 合理：对方不亏
    SlightlyUnfair,     // 稍微不合理：对方小亏
    VeryUnfair,         // 很不合理：对方大亏（会被直接拒绝）
    Count,
};

[[nodiscard]] std::string_view dealRatingName(DealRating r);

struct Term {
    TermKind kind = TermKind::Credits;
    i64 amount = 0;
    i64 extra = 0;
};

/// 一次谈判的完整条款
struct NegotiationTerms {
    std::vector<Term> demand;   // 要求对方付出
    std::vector<Term> offer;    // 我方付出
};

/// 谈判权重分解
struct NegotiationWeight {
    Fixed total = Fixed(0);
    Fixed territory = Fixed(0);
    Fixed economy = Fixed(0);
    Fixed sanctions = Fixed(0);
    Fixed war = Fixed(0);
    Fixed military = Fixed(0);
    Fixed threshold = Fixed(0);
    bool willing = false;
    std::string reason;
};

struct NegotiationResult {
    bool accepted = false;
    bool unconditional = false;
    Fixed weight = Fixed(0);
    Fixed demandValue = Fixed(0);
    Fixed offerValue = Fixed(0);
    std::string reason;
    /// 盟友代付记录
    std::vector<std::string> allyContributions;
    /// 实际执行的条款
    std::vector<std::string> applied;
    /// 提案对**对方**的公平度评价（无论是否接受都会给出）
    DealRating rating = DealRating::Reasonable;
    /// 评价说明（供玩家判断要不要谴责）
    std::string ratingNote;
};

/// 星系的真实估值：行星数量、人口、开发度、首都与巨构溢价。
/// 用于领土交易 —— 不再是一个拍脑袋的常数。
[[nodiscard]] Fixed systemValue(const GameState& st, u32 system);

/// 领土交易合法性：只有「接壤互换」才可能成交。
/// 规则：
///   * 双方互相割让星系（单方面索取领土一律拒绝）
///   * 割让的星系必须与**接收方**的现有领土接壤
///   * 首都不参与任何交易
/// 返回是否合法，并在 note 中说明原因。
[[nodiscard]] bool territorySwapLegal(const GameState& st, u32 proposer, u32 target,
                                      const NegotiationTerms& terms, std::string* note);

/// 计算 target 面对 proposer 时的谈判权重
[[nodiscard]] NegotiationWeight negotiationWeight(const GameState& st, u32 target, u32 proposer);

/// 某条款对某帝国的估值（信用点等价）
[[nodiscard]] Fixed termValue(const GameState& st, u32 empire, const Term& t);
[[nodiscard]] Fixed termsValue(const GameState& st, u32 empire, const std::vector<Term>& terms);

/// 首都是否已被对方占领
[[nodiscard]] bool capitalOccupied(const GameState& st, u32 owner, u32 attacker);

/// 是否已放弃抵抗（无舰队 或 军力远逊于对方）
[[nodiscard]] bool hasSurrendered(const GameState& st, u32 empire, u32 against);

/// 解析条款字符串："credits=10000,alloys=5000,tech=comp6,system=12,manpower=300"
[[nodiscard]] bool parseTerms(std::string_view text, std::vector<Term>& out, std::string* err);

/// 执行谈判。proposer 向 target 提出条款，按权重与支付能力判定是否接受并落地。
[[nodiscard]] NegotiationResult negotiate(GameState& st, u32 proposer, u32 target,
                                          const NegotiationTerms& terms);

/// 条款文本
[[nodiscard]] std::string termText(const GameState& st, const Term& t);

/// 谈判态势报告（negotiate --status）
[[nodiscard]] std::string negotiationReport(const GameState& st, u32 target, u32 proposer);

}  // namespace gf
