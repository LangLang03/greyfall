#pragma once
// 线索超图与推断引擎的数据定义（算法在 clue/*.cpp）
#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "domain/Provenance.h"
#include "util/Fixed.h"

namespace gf {

inline constexpr int kClueCount = 340;
inline constexpr int kConclusionCount = 84;
inline constexpr int kMinimalSetCount = 210;
inline constexpr int kContradictionCount = 160;
inline constexpr int kActCount = 7;

/// 边类型 5 种
enum class ClueEdgeKind : u8 {
    Corroborate = 0,  // 印证
    Contradict,       // 矛盾
    Prereq,           // 前置
    Belongs,          // 归属
    About,            // 指涉主体
    Count,
};

enum class ClueTag : u8 {
    Silence = 0, Migrant, Signal, Artifact, Witness, Ruin, Document, Rumor, Panopticon, Origin,
    Betrayal, Faction, Market, Military, Tech, Ritual, Count,
};
inline constexpr int kClueTagCount = static_cast<int>(ClueTag::Count);

struct ClueDef {
    u16 id = 0;
    std::string_view idName;
    std::string_view nameZh;
    std::string_view text;
    std::array<ClueTag, 4> tags{};
    u8 tagCount = 0;
    u8 act = 1;                // 所属幕
    u8 tier = 1;
    /// 静态关联（content 中声明的初始超边）
    std::vector<u16> linked;
    ClueEdgeKind linkKind = ClueEdgeKind::Corroborate;
};

/// 线索实例（运行时状态）
struct ClueNode {
    u16 def = 0;
    bool known = false;
    Fixed credibility = Fixed(0);
    Provenance prov;
    u8 act = 1;
    u64 discoveredTick = 0;
    bool archived = false;
};

struct ClueEdge {
    u16 a = 0;
    u16 b = 0;
    ClueEdgeKind kind = ClueEdgeKind::Corroborate;
    Fixed weight = Fixed::pct(50);
    bool adjudicated = false;   // 矛盾是否已裁定
    u16 winner = 0;             // 裁定胜出方
    u64 createdTick = 0;
    bool playerMade = false;
};

/// 结论（可由最小充分集解锁）
struct ConclusionDef {
    u16 id = 0;
    std::string_view idName;
    std::string_view nameZh;
    std::string_view text;
    u8 act = 1;
    /// 合取范式：每个析取子句（任一节点成立则该子句满足）
    std::vector<std::vector<u16>> clauses;
    u16 rewardClue = 0xFFFFu;
    u16 rewardItem = 0xFFFFu;
    i16 rewardTech = -1;
    bool trueConclusion = true;   // 是否为真相（误判会走假分支）
    /// 需要来源多样性 ≥3 等附加约束
    bool requireDiversity = true;
    bool requireInsider = true;
    Fixed marketImpact = Fixed(0);   // 真相公开引发的价格跳跃
};

/// 剧情幕状态
struct PlotState {
    u8 act = 1;
    std::vector<u16> knownClues;
    std::vector<u16> conclusionsReached;
    std::vector<u16> committedConclusions;   // 已 --commit 的
    std::vector<u16> falseConclusions;       // 误判的
    std::array<Fixed, 8> endingVector{};     // 8 维结局向量
    std::vector<u16> pendingBeats;
    bool hiddenActUnlocked = false;
    u32 storyFlags = 0;
};

[[nodiscard]] const ClueDef& clueDef(int idx);
[[nodiscard]] const ConclusionDef& conclusionDef(int idx);
[[nodiscard]] int clueIndexByName(std::string_view s);
[[nodiscard]] int conclusionIndexByName(std::string_view s);
[[nodiscard]] std::string_view clueEdgeKindName(ClueEdgeKind k);
[[nodiscard]] ClueEdgeKind clueEdgeKindFromName(std::string_view s);
[[nodiscard]] std::string_view clueTagName(ClueTag t);
[[nodiscard]] ClueTag clueTagFromName(std::string_view s);
[[nodiscard]] std::string_view endingDimName(int dim);

}  // namespace gf
