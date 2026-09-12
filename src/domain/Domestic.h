#pragma once
// 国内派系双层博弈（玩家自己也被困在重复博弈中）
#include <string>
#include <string_view>
#include <vector>

#include "util/Fixed.h"

namespace gf {

enum class FactionKind : u8 {
    Military = 0,   // 军部
    Merchant,       // 商会
    Technocrat,     // 技工/技术官僚
    Fundamentalist, // 原教旨
    Labor,          // 劳工/平民
    Nobility,       // 旧贵族
    Populist,       // 民粹
    Syndicate,      // 辛迪加（灰色资本）
    Count,
};

struct Faction {
    FactionKind kind = FactionKind::Merchant;
    std::string name;
    Fixed influence = Fixed::pct(20);     // 政治影响力
    Fixed satisfaction = Fixed::pct(50);  // 满意度
    Fixed demandPressure = Fixed(0);      // 未满足诉求的累积压力
    u32 lastDemandTick = 0;
    u64 nextSatisfyTick = 0;
    std::string lastDemand;
    /// 是否已被 AI 渗透（代理战）
    u32 patron = 0xFFFFFFFFu;
    Fixed patronFunding = Fixed(0);
};

/// 国内状态
struct Domestic {
    std::vector<Faction> factions;
    Fixed unrest = Fixed(0);         // 民怨 0..1
    Fixed legitimacy = Fixed::pct(60);
    Fixed coupRisk = Fixed(0);
    u32 coupCountdown = 0;
    /// 上次政变的 tick（用于冷却，避免无限重复政变）
    u32 lastCoupTick = 0;
    Fixed repression = Fixed(0);     // 压制程度（降低民怨但激怒自由派）
    Fixed legitimacyDecay = Fixed(0);
};

[[nodiscard]] std::string_view factionKindName(FactionKind k);
[[nodiscard]] FactionKind factionKindFromName(std::string_view s);
/// 该派系当前可能提出的诉求文本
[[nodiscard]] std::string factionDemandText(FactionKind k);

}  // namespace gf
