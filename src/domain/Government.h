#pragma once
// 政体机制：让「民主制」与「独裁制」在玩法上真正不同
//
// 现状问题：
//   早期政体只是几组数字（行动点、投票权重、民怨偏移、合法性基线），
//   民主制与独裁制的实际玩法完全一样 —— 都靠民怨与稳定度收敛，
//   没有选举、没有继承危机、没有镇压，玩家感受不到差别。
//
// 本模块引入**合法性来源**这一核心区分：
//   选举授权 —— 合法性来自选票；定期改选，失去民心就下台
//   血统传统 —— 合法性来自传承；继承危机是主要风险
//   恐惧镇压 —— 合法性来自压制；可主动镇压，但会累积怨恨
//   绩效表现 —— 合法性来自成果；经济与战果直接决定
//   信仰教义 —— 合法性来自教义；容忍度低但凝聚力高
//   共识协同 —— 合法性来自群体意志；无派系但决策慢
#include <string>
#include <vector>

#include "domain/Personnel.h"
#include "util/Fixed.h"

namespace gf {

struct GameState;
struct Empire;

/// 合法性来源：决定合法性如何涨落、以及有哪些专属手段
enum class LegitimacySource : u8 {
    Election = 0,    // 选举授权
    Tradition,       // 血统传统
    Fear,            // 恐惧镇压
    Performance,     // 绩效表现
    Faith,           // 信仰教义
    Consensus,       // 共识协同
    Count,
};

[[nodiscard]] std::string_view legitimacySourceName(LegitimacySource s);
[[nodiscard]] std::string legitimacySourceDesc(LegitimacySource s);
[[nodiscard]] LegitimacySource legitimacySourceOf(u8 government);

/// 政体是「选举制」还是「威权制」——影响选举、镇压、政变等一整套机制
[[nodiscard]] bool isElective(u8 government);
/// 政体是否允许主动镇压
[[nodiscard]] bool allowsRepression(u8 government);

// ---------------------------------------------------------------------------
// 选举
// ---------------------------------------------------------------------------
struct Candidate {
    std::string name;
    RulerTrait trait = RulerTrait::None;
    Fixed skill = Fixed::pct(50);
    Fixed support = Fixed(0);     // 0..1 民意支持
    bool incumbent = false;
};

struct Election {
    bool active = false;
    u32 startTick = 0;
    u32 endTick = 0;
    std::vector<Candidate> candidates;
    /// 玩家/该帝国公开支持的候选人下标（-1 = 未表态）
    int endorsed = -1;
    /// 已投入的竞选资源
    Fixed campaignSpent = Fixed(0);
    u32 electionsHeld = 0;
};

/// 政体运行状态（挂在 Empire 上）
struct GovernmentState {
    Election election;
    /// 威权制专属：镇压强度 0..1（提高合法性，但累积怨恨）
    Fixed repression = Fixed(0);
    /// 累积的怨恨：镇压的代价，会推高民怨与政变风险
    Fixed resentment = Fixed(0);
    /// 继承危机倒计时（传统/信仰类政体，领袖过世时可能触发）
    u32 successionCrisis = 0;
};

/// 每 tick：选举进程、合法性按来源演化、镇压代价、继承危机
void governmentPhase(GameState& st);

/// 主动镇压（仅威权制）：立即提高合法性，但累积怨恨
[[nodiscard]] bool suppress(GameState& st, u32 empire, std::string* msg);

/// 发起选举（仅选举制，需距上次选举足够久）
[[nodiscard]] bool callElection(GameState& st, u32 empire, std::string* msg);
/// 在选举中公开支持某位候选人（消耗影响力，提升其支持率）
[[nodiscard]] bool endorseCandidate(GameState& st, u32 empire, int index, std::string* msg);

/// 当前政体带来的合法性修正（替代原先只用基线的做法）
[[nodiscard]] Fixed legitimacyFromSource(const GameState& st, u32 empire);

/// 政体专属的行动点与议会门槛差异说明
[[nodiscard]] std::string governmentReport(const GameState& st, u32 empire);

/// 选举界面文本
[[nodiscard]] std::string electionText(const GameState& st, u32 empire);

}  // namespace gf
