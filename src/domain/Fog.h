#pragma once
// 战争迷雾：没有情报就只能看到模糊的估计
//
// 设计动机：
//   早期版本里 `greyfall empires` 直接列出所有国家的**精确国库、国力与舰队数**。
//   玩家因此无需任何情报工作就能看出谁虚弱、谁富裕，开战决策变成纯粹的算术。
//   这让间谍系统（渗透、侦察、反间谍）几乎失去意义。
//
// 现行规则：
//   * 对每个国家有一个 0..100% 的**情报等级**，由渗透度、条约、贸易、
//     接壤程度与对方的反间谍能力共同决定；
//   * 不同信息有不同门槛（军力 20%、国库 40%、科技 55%、政策 65%……）；
//   * 未达门槛时只给出**模糊估计**（区间或定性描述），而不是精确数字。
#include <string>

#include "util/Fixed.h"

namespace gf {

struct GameState;
struct Empire;

/// 可以窥探的信息类别
enum class IntelField : u8 {
    Military = 0,    // 军力与舰队规模
    Treasury,        // 国库
    Tech,            // 科技进度
    Policies,        // 政策与决议
    Relations,       // 与他国的关系
    FleetPositions,  // 舰队位置
    Count,
};

[[nodiscard]] std::string_view intelFieldName(IntelField f);
/// 看到该类信息所需的情报等级
[[nodiscard]] Fixed intelThreshold(IntelField f);

/// 观察者对目标的情报等级 0..1
[[nodiscard]] Fixed intelLevel(const GameState& st, u32 observer, u32 target);

/// 是否已经能看到某类信息
[[nodiscard]] bool intelKnown(const GameState& st, u32 observer, u32 target, IntelField f);

/// 模糊化：未达门槛时返回估计文本（区间/定性），达到门槛时返回精确值文本。
[[nodiscard]] std::string intelNumber(const GameState& st, u32 observer, u32 target, IntelField f,
                                      Fixed value, int decimals);

/// 该国在我眼中的情报概览（哪些知道、哪些不知道）
[[nodiscard]] std::string intelPicture(const GameState& st, u32 observer, u32 target);

/// 情报来源分解（供 UI 说明「为什么我知道/不知道」）
[[nodiscard]] std::string intelSourceBreakdown(const GameState& st, u32 observer, u32 target);

/// 每 tick：情报等级随渗透与外交状态自然演化（含衰减）
void intelPhase(GameState& st);

}  // namespace gf
