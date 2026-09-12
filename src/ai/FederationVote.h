#pragma once
// 联邦投票：实力权重 + 贡献 + logrolling（以贸易让利换票）
#include "core/GameState.h"
#include "domain/Federation.h"

namespace gf {

/// 计算某帝国在某动议上的立场 -1..1
[[nodiscard]] Fixed voteStance(const GameState& st, u32 voter, const FederalMotion& motion);

/// 加权投票 + logrolling；返回是否通过
[[nodiscard]] bool resolveMotion(GameState& st, u32 federationId, FederalMotion& motion);

/// 生成一个动议（AI 或玩家提出）
[[nodiscard]] FederalMotion proposeMotion(GameState& st, u32 federationId, VoteSubject subject, u32 target,
                                          u32 proposer);

/// 阶段 7：联邦决议
void federationPhase(GameState& st);

/// 玩家在联邦中的投票（envoy vote yes|no）
[[nodiscard]] bool playerVote(GameState& st, u32 motionId, bool yes, std::string* err);

/// logrolling：以让利换票
[[nodiscard]] bool offerConcession(GameState& st, u32 federationId, u32 toMember, Fixed concession,
                                   std::string* err);

}  // namespace gf
