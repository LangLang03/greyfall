#pragma once
// 信誉与观感：由观测通道（噪声 = 距离/宣传/反间谍）更新
#include "core/GameState.h"
#include "domain/Mind.h"

namespace gf {

/// 观测通道：距离噪声 / 宣传偏移 / 反间谍强度
[[nodiscard]] ObservationChannel observationChannel(const GameState& st, u32 observer, u32 target);

/// 更新 observer 对 target 的信誉与怨恨
void reputationUpdate(GameState& st, u32 observer, u32 target, Fixed eventValue, Fixed halfLife = Fixed::raw(900));

/// 全体对 target 的信誉更新（例如背叛被公开）
void reputationUpdateAll(GameState& st, u32 target, Fixed eventValue, u32 exclude = 0xFFFFFFFFu);

/// 信誉现值（供 BetrayalCalculus 使用）
[[nodiscard]] Fixed reputationOf(const GameState& st, u32 observer, u32 target);

/// 怨恨衰减（grudgeHalfLife）
void grudgeDecay(GameState& st);

/// 观感矩阵的一轮自然演化
void opinionPhase(GameState& st);

}  // namespace gf
