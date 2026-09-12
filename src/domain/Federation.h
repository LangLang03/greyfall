#pragma once
// 联邦：加权投票 + logrolling（以让利换票）
#include <string>
#include <vector>

#include "util/Fixed.h"

namespace gf {

enum class VoteSubject : u8 { AdmitMember = 0, ExpelMember, CommonFleet, TaxHarmonize, SanctionMember,
                              WarDeclaration, LimitManipulation, CrisisResponse, Reform, Count };

struct FederalMotion {
    u32 id = 0;
    VoteSubject subject = VoteSubject::AdmitMember;
    u32 target = 0;          // 相关帝国
    u32 proposer = 0;
    u64 proposedTick = 0;
    Fixed threshold = Fixed::pct(50);
    std::vector<u32> yes;
    std::vector<u32> no;
    std::vector<u32> abstain;
    bool resolved = false;
    bool passed = false;
    Fixed yesWeight = Fixed(0);
    Fixed noWeight = Fixed(0);
};

struct Federation {
    u32 id = 0;
    std::string name;
    u32 founder = 0;
    std::vector<u32> members;
    Fixed cohesion = Fixed::pct(60);
    Fixed treasury = Fixed(0);
    Fixed commonFleet = Fixed(0);
    std::vector<FederalMotion> motions;
    /// 我方在联邦中的让利承诺（logrolling 筹码）
    Fixed concession = Fixed(0);
};

/// 计算某帝国在联邦中的投票权重：实力 + 贡献 + logrolling 让利
[[nodiscard]] Fixed federationVoteWeight(Fixed power, Fixed contribution, Fixed concession);

}  // namespace gf
