#pragma once
// 和平会议与领土割让（HOI4 / V3 式）
//
// 与已有机制的关系：
//   negotiate  —— 和平时期的双边交易（可索取资源/科技/领土）
//   PeaceConference —— **战争结束时的清算**：胜方用「战争分数」兑换要求，
//                      包括割让星系、赔款、技术转移、人力与附庸。
//
// 触发条件（任一）：
//   * 战争分数达到阈值（有限胜利）
//   * 一方首都被占领（无条件接受 —— 战败方无权拒绝任何要求）
//
// 分数经济：
//   占领星系 / 赢得战斗会累积战争分数；每个要求在会议中**消耗**分数。
//   分数耗尽即无法再索取 —— 这是「胜利也要有所取舍」的来源。
#include <string>
#include <vector>

#include "util/Fixed.h"

namespace gf {

struct GameState;
struct Empire;

/// 一项和平要求
enum class PeaceDemandKind : u8 {
    AnnexSystem = 0,   // 割让星系
    Reparations,       // 战争赔款（信用点）
    TechTransfer,      // 技术转移（科技）
    Manpower,          // 人力（人口）
    DisarmFleet,       // 解除武装（摧毁目标舰队）
    Vassalize,         // 附庸（极端要求）
    Count,
};

struct PeaceDemand {
    PeaceDemandKind kind = PeaceDemandKind::AnnexSystem;
    u32 target = 0;          // 星系编号 / 科技编号 / 舰队编号
    Fixed amount = Fixed(0); // 赔款额 / 人力数
    Fixed cost = Fixed(0);   // 占用的战争分数
    std::string text;        // 人类可读描述
};

/// 一次和平会议
struct PeaceConference {
    bool active = false;
    u32 winner = 0;             // 占优方（提出要求的一方）
    u32 loser = 0;
    Fixed warScore = Fixed(0);  // 胜方可用的战争分数
    Fixed spent = Fixed(0);     // 已索取消耗
    bool unconditional = false; // 战败方首都已失守 ⇒ 无权拒绝
    u32 startTick = 0;
    std::vector<PeaceDemand> demands;
    /// 上次结果说明
    std::string lastResult;
};

[[nodiscard]] std::string_view peaceDemandName(PeaceDemandKind k);

/// 计算某个星系的「割让代价」（战争分数）
[[nodiscard]] Fixed annexCost(const GameState& st, u32 system);
/// 计算赔款要求所需分数
[[nodiscard]] Fixed reparationsCost(Fixed credits);
/// 技术转移的分数代价
[[nodiscard]] Fixed techTransferCost();
/// 人力的分数代价（per 1000 人）
[[nodiscard]] Fixed manpowerCost(Fixed thousands);
/// 附庸的分数代价
[[nodiscard]] Fixed vassalizeCost();

/// 当前会议中已消耗 / 剩余分数
[[nodiscard]] Fixed peaceSpent(const GameState& st, u32 empire);
[[nodiscard]] Fixed peaceRemaining(const GameState& st, u32 empire);

/// 是否应当召开和平会议（自动判定）
[[nodiscard]] bool shouldConvenePeace(const GameState& st, u32 a, u32 b);
/// 召开会议
[[nodiscard]] bool convenePeace(GameState& st, u32 winner, u32 loser, std::string* err);
/// 添加一项要求
[[nodiscard]] bool addDemand(GameState& st, u32 empire, const PeaceDemand& d, std::string* err);
/// 移除一项要求
[[nodiscard]] bool removeDemand(GameState& st, u32 empire, std::size_t index, std::string* err);
/// 执行会议：结算全部要求并结束战争
[[nodiscard]] bool concludePeace(GameState& st, u32 empire, std::string* err);
/// 放弃会议（维持战争状态）
[[nodiscard]] bool abandonPeace(GameState& st, u32 empire, std::string* err);

/// 输出便捷构造
[[nodiscard]] PeaceDemand makeAnnex(const GameState& st, u32 system);
[[nodiscard]] PeaceDemand makeReparations(Fixed credits);
[[nodiscard]] PeaceDemand makeTech(u32 techId);
[[nodiscard]] PeaceDemand makeManpower(Fixed thousands);

/// 每 tick：AI 之间的战争自动召开并结束会议
void peacePhase(GameState& st);

/// 文本
[[nodiscard]] std::string peaceText(const GameState& st, u32 empire);
/// 对某场战争的会议状态（供 UI 查询）
[[nodiscard]] const PeaceConference* findConference(const GameState& st, u32 a, u32 b);

}  // namespace gf
