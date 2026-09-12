#pragma once
// 起义与党派斗争
//
// 现状问题：
//   早期只有「政变」一种国内危机，且它只是一次性扣分 ——
//   民怨再高、派系再不满，领土也不会动摇，
//   玩家因此可以把民怨推到 100% 而毫发无伤（只损失一点稳定度）。
//
// 本模块补上**领土层面的代价**：
//   不安 → 叛乱 → 割据：星系会在极端不满下逐步脱离控制，
//   最终成立新国家或倒向邻国。派系则会在关键时刻逼宫。
#include <string>
#include <vector>

#include "domain/Domestic.h"
#include "util/Fixed.h"

namespace gf {

struct GameState;
struct Empire;

/// 一个星系的动乱阶段
enum class RevoltStage : u8 {
    Calm = 0,      // 平静
    Unrest,        // 不安：产出下降、驻军增加
    Revolt,        // 叛乱：停止纳贡，驻军与中央对抗
    Secession,     // 割据：脱离控制，成为独立势力或倒向邻国
    Count,
};

[[nodiscard]] std::string_view revoltStageName(RevoltStage s);
[[nodiscard]] std::string revoltStageDesc(RevoltStage s);

/// 一个正在动乱的星系
struct Revolt {
    u32 system = 0xFFFFFFFFu;
    u32 owner = 0xFFFFFFFFu;
    RevoltStage stage = RevoltStage::Calm;
    Fixed severity = Fixed(0);      // 0..1
    u64 sinceTick = 0;
    u32 lastEventTick = 0;
    /// 主导派系（不满最强的那个）
    FactionKind leader = FactionKind::Populist;
    /// 中央已投入的镇压资源
    Fixed suppressed = Fixed(0);
};

/// 每 tick：推进动乱阶段、触发事件、执行割据
void revoltPhase(GameState& st);

/// 某星系的动乱风险 0..1（由民怨、派系不满、稳定度、驻军共同决定）
[[nodiscard]] Fixed revoltRiskOf(const GameState& st, u32 system);

/// 查询某星系的动乱状态
[[nodiscard]] const Revolt* revoltAt(const GameState& st, u32 system);
[[nodiscard]] RevoltStage revoltStageOf(const GameState& st, u32 system);

/// 中央派兵镇压某星系的动乱（消耗军事力量与国库）
[[nodiscard]] bool suppressRevolt(GameState& st, u32 empire, u32 system, std::string* msg);

// ---------------------------------------------------------------------------
// 党派斗争
// ---------------------------------------------------------------------------
/// 派系的影响力格局（哪个派系在上升、哪个在衰落）
[[nodiscard]] std::string factionStruggleReport(const GameState& st, u32 empire);

/// 最强派系的逼宫：当它影响力最高而满意度极低时提出最后通牒。
/// 返回是否有待回应的通牒。
[[nodiscard]] bool factionUltimatumPending(const GameState& st, u32 empire, FactionKind* which);
/// 回应通牒：让步（满足诉求，代价是政策偏移与资源）或拒绝（满意度暴跌、动乱风险上升）
[[nodiscard]] bool answerUltimatum(GameState& st, u32 empire, bool concede, std::string* msg);

/// 文本
[[nodiscard]] std::string revoltReport(const GameState& st, u32 empire);

}  // namespace gf
