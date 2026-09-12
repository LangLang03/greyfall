#pragma once
// 决议引擎：触发判定、倒计时推进、效果聚合与过期
#include "core/GameState.h"
#include "domain/Resolution.h"

namespace gf {

struct TickReport;

/// 每 tick 结算：应用持续效果、判定自动触发、推进倒计时
void resolutionPhase(GameState& st, TickReport& rep);
/// AI 决议决策：非玩家帝国主动发起有利的「主动决议 / 牺牲换利」。
/// 没有这一步，21 项可主动发起的决议只对玩家开放，
/// AI 只能被动接受自动触发的部分。
void resolutionAiPhase(GameState& st);

/// 玩家主动发起一个决议（Active / Tradeoff）
[[nodiscard]] bool resolutionActivate(GameState& st, u32 empire, u16 defId, std::string* err);

/// 聚合某帝国的决议修正（修正类目标求和）
[[nodiscard]] Fixed resolutionModifier(const Empire& e, ModKind kind);

/// 某帝国当前是否处于某决议的生效期内
[[nodiscard]] bool hasResolution(const Empire& e, u16 defId);

/// 决议列表文本（resolve 命令）
[[nodiscard]] std::string resolutionListText(const GameState& st, u32 empire, bool onlyAvailable);

/// 单个决议详情
[[nodiscard]] std::string resolutionDetailText(const GameState& st, u32 empire, u16 defId);

}  // namespace gf
