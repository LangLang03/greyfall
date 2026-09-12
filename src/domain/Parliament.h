#pragma once
// 派系立法与议会博弈（Victoria 3 式）
//
// 与已有 `policy`（政令，玩家单方面决定）的区别：
//   policy     —— 玩家直接推行，只需影响力和过渡期
//   legislation —— 必须**议会表决通过**才能生效
//
// 机制：
//   * 每个派系在议会占有**席位**（由影响力与政体决定）。
//   * 每项法案有各派系的**立场**（赞成/反对/未定），由派系理念与法案性质决定。
//   * 玩家可**拉票**：用资源、承诺、或牺牲其他派系利益换取支持。
//   * 表决按席位加权计票，通过门槛由政体决定（简单多数 / 绝对多数 / 2/3）。
//   * 表决失败会损失政治资本并激怒支持者；强行通过（`--force`）代价更高。
#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "domain/Domestic.h"
#include "domain/Species.h"
#include "util/Fixed.h"

namespace gf {

inline constexpr int kBillCount = 36;

/// 法案类别
enum class BillCategory : u8 {
    Economic = 0,
    Military,
    Social,
    Political,     // 政体改革
    Diplomatic,
    Count,
};

/// 表决门槛
enum class VoteThreshold : u8 { SimpleMajority = 0, AbsoluteMajority, SuperMajority, Count };

/// 单个派系对某法案的立场
enum class Stance : u8 { Opposed = 0, Undecided, Supportive, Count };

struct BillEffect {
    ModKind kind = ModKind::Count;
    Fixed value = Fixed(0);
};

struct BillDef {
    u8 id = 0;
    std::string_view idName;
    std::string_view nameZh;
    std::string_view desc;
    BillCategory category = BillCategory::Economic;
    /// 各派系的**天然**立场倾向（索引 = FactionKind）
    std::array<i8, static_cast<std::size_t>(FactionKind::Count)> baseStance{};
    std::array<BillEffect, 3> effects{};
    u8 effectCount = 0;
    /// 通过后的一次性政治成本（影响力）
    i64 politicalCost = 0;
    /// 通过后的持续民意影响
    Fixed unrestDelta = Fixed(0);
    /// 需要的门槛（默认由政体决定，此处可覆盖）
    VoteThreshold overrideThreshold = VoteThreshold::Count;
    /// 是否为「激进改革」（保守派强烈反对）
    bool radical = false;
};

/// 议会中的一个派系席位与当前立场
struct Seat {
    FactionKind faction = FactionKind::Merchant;
    int seats = 0;
    Fixed influence = Fixed(0);
    Fixed satisfaction = Fixed(0);
    Stance stance = Stance::Undecided;
    /// 玩家为该派系付出的拉票代价（用于结算与日志）
    std::string persuasion;
    /// 已承诺的让步（影响后续满意度）
    Fixed promised = Fixed(0);
};

/// 一次表决的进行状态
struct BillSession {
    bool active = false;
    u8 billId = 0;
    u64 startTick = 0;
    /// 已连续表决的季数（可延期再议）
    int rounds = 0;
    std::vector<Seat> seats;
    int yesSeats = 0;
    int noSeats = 0;
    int undecidedSeats = 0;
    Fixed threshold = Fixed::pct(50);
    VoteThreshold thresholdKind = VoteThreshold::SimpleMajority;
    /// 上次表决结果
    bool resolved = false;
    bool passed = false;
    std::string lastResult;
};

/// 帝国的议会状态
struct Parliament {
    /// 各派系席位（索引 = FactionKind）
    std::array<int, static_cast<std::size_t>(FactionKind::Count)> seats{};
    int totalSeats = 0;
    /// 当前表决
    BillSession session;
    /// 历史通过 / 否决的法案
    std::vector<u16> passed;
    std::vector<u16> rejected;
    /// 政治资本（拉票消耗的资源）
    Fixed capital = Fixed::pct(50);
    /// 政体决定的门槛
    VoteThreshold threshold = VoteThreshold::SimpleMajority;
    /// 累积的立法次数
    u32 legislationCount = 0;
};

[[nodiscard]] const BillDef& billDef(int idx);
[[nodiscard]] int billIndexByName(std::string_view s);
[[nodiscard]] std::string_view billCategoryName(BillCategory c);
[[nodiscard]] std::string_view voteThresholdName(VoteThreshold t);
[[nodiscard]] std::string_view stanceName(Stance s);
[[nodiscard]] std::string billEffectText(const BillDef& b);

/// 初始化议会席位（由派系影响力与政体决定）
void parliamentInit(struct GameState& st, u32 empire);

/// AI 立法：非玩家帝国在条件合适时自行提案、拉票并表决。
/// 没有这一步，议会机制只对玩家生效，AI 成了静态背景，
/// 政治博弈也就失去了一半意义（V3 的 AI 同样会推动立法）。
void parliamentAiPhase(struct GameState& st);
/// 每 tick：若无表决则按需发起；推进匿名表决
void parliamentPhase(struct GameState& st);

/// 发起一项法案的表决
[[nodiscard]] bool billPropose(struct GameState& st, u32 empire, u16 billId, std::string* err);
/// 拉票：向某派系付出代价以改变其立场
[[nodiscard]] bool billPersuade(struct GameState& st, u32 empire, FactionKind f, std::string* err);
/// 立即表决
[[nodiscard]] bool billVote(struct GameState& st, u32 empire, std::string* err);
/// 强行通过（代价高昂，激怒反对派）
[[nodiscard]] bool billForcePass(struct GameState& st, u32 empire, std::string* err);
/// 撤回法案
[[nodiscard]] bool billWithdraw(struct GameState& st, u32 empire, std::string* err);

/// 议会提供的修正合计
[[nodiscard]] Fixed parliamentModifier(const struct GameState& st, u32 empire, ModKind kind);
/// 已通过法案对民怨的**均衡**影响（作用于目标值，而非每季累加）
[[nodiscard]] Fixed parliamentUnrestTarget(const struct GameState& st, u32 empire);
/// 已通过法案对合法性的均衡影响
[[nodiscard]] Fixed parliamentLegitimacyTarget(const struct GameState& st, u32 empire);

/// 估算某法案在议会的预期票数（不改变状态）。
/// AI 用它挑选「有可能通过」的法案，而不是反复提交同一项必败法案。
struct VoteEstimate {
    int yes = 0;
    int no = 0;
    int undecided = 0;
    Fixed ratio = Fixed(0);          // yes / (yes+no)
    bool wouldPass = false;
};
[[nodiscard]] VoteEstimate estimateVotes(const struct GameState& st, u32 empire, u16 billId);

/// 议会文本
[[nodiscard]] std::string parliamentText(const struct GameState& st, u32 empire);
[[nodiscard]] std::string billDetailText(const struct GameState& st, u32 empire, u16 billId);

}  // namespace gf
