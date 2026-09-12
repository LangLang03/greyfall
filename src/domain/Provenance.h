#pragma once
// 来源（Provenance）—— 玩家欺诈轴的核心：数据不是"藏起来"，而是"布置出去"
#include <string>
#include <string_view>
#include <vector>

#include "util/Fixed.h"

namespace gf {

/// 观测/获取渠道。AI 用渠道 + 信号成本判断"他为何要让我看到这个"
enum class ProvChannel : u8 {
    DirectObservation = 0,  // 直接观测（泛视网络）
    MarketFlow,             // 市场订单流痕迹
    SpyNetwork,             // 间谍获取
    Intercept,              // 通讯截获
    Rumor,                  // 流言
    Forgery,                // 伪造
    Gift,                   // 对方主动给予（成本信号）
    Archive,                // 档案/历史
    Analysis,               // 推演得出
    Testimony,              // 证人/叛逃者
    Panopticon,             // 泛视网络直接读取
    Count,
};

struct Provenance {
    ProvChannel channel = ProvChannel::DirectObservation;
    u32 source = 0xFFFFFFFFu;     // 来源主体
    Fixed signalCost = Fixed(0);  // 获得它付出的代价（越高越可信）
    Fixed credibility = Fixed::pct(70);
    u64 tick = 0;
    u32 rollbackAt = 0;           // 产生时的读档次数（AI 会看这个）
    bool forged = false;          // 是否伪造
    u32 forger = 0xFFFFFFFFu;     // 伪造者
    std::string note;

    /// 综合可信度：渠道基线 × 信号成本加成 × 时间衰减
    [[nodiscard]] Fixed effectiveCredibility(u64 nowTick) const;
};

[[nodiscard]] std::string_view provChannelName(ProvChannel c);
[[nodiscard]] ProvChannel provChannelFromName(std::string_view s);
/// 渠道的基线可信度
[[nodiscard]] Fixed channelBaseCredibility(ProvChannel c);

/// AI 的"一致性异常"打分：为什么他此刻让我看到这个？
struct ExposureAnomaly {
    u32 field = 0;
    Fixed score = Fixed(0);
    std::string reason;
};

}  // namespace gf
