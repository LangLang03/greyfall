#pragma once
// 外交交易：AI 之间的互助协定，以及 AI 主动向玩家提出的提案
//
// 与 negotiate 的分工：
//   negotiate       —— 玩家发起的双边谈判（条款由玩家拟）
//   diplomacyPhase  —— AI 主动发起的提案（推给玩家的「提案箱」）
//   aiTradePhase    —— AI 之间的交易（在后台结算，只留日志与观感影响）
#include <string>
#include <vector>

#include "util/Fixed.h"

namespace gf {

struct GameState;

/// 一份等待玩家回应的 AI 提案
enum class ProposalKind : u8 {
    TradeGoods = 0,    // 资源互换
    ResearchPact,      // 研究协定（双方研究速率小幅提升）
    NonAggression,     // 互不侵犯
    JointIntel,        // 联合情报
    TerritorySwap,     // 领土互换（极少出现，且必须接壤对等）
    Tribute,           // 索贡（对方以实力压你）
    Count,
};

struct Proposal {
    u32 id = 0;
    ProposalKind kind = ProposalKind::TradeGoods;
    u32 from = 0;              // 提出方（AI）
    u32 to = 0;                // 接收方
    u64 createdTick = 0;
    u32 expiresTick = 0;
    std::string title;
    std::string body;
    /// 接受/拒绝的后果提示
    std::string ifAccept;
    std::string ifReject;
    /// 提案对玩家的公平度（用于「是否值得谴责」的判断）
    int fairnessPct = 0;       // -100..100，负数表示玩家吃亏
};

/// 待决提案箱
struct ProposalBox {
    std::vector<Proposal> items;
    u32 nextId = 1;
    [[nodiscard]] bool empty() const { return items.empty(); }
    [[nodiscard]] std::size_t size() const { return items.size(); }
    [[nodiscard]] Proposal* find(u32 id);
};

[[nodiscard]] std::string_view proposalKindName(ProposalKind k);

/// 每 tick：AI 视关系、立场、需求生成提案（推给玩家）
void diplomacyPhase(GameState& st);

/// 每 tick：AI 之间的交易（只影响双方，不打扰玩家）
void aiTradePhase(GameState& st);

/// 接受 / 拒绝一份提案
[[nodiscard]] bool proposalAccept(GameState& st, u32 id, std::string* msg);
[[nodiscard]] bool proposalReject(GameState& st, u32 id, std::string* msg);

/// 文本
[[nodiscard]] std::string proposalText(const GameState& st, u32 id);
[[nodiscard]] std::string proposalListText(const GameState& st);

}  // namespace gf
