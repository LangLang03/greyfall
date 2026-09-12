#include "domain/Provenance.h"

namespace gf {

Fixed channelBaseCredibility(ProvChannel c) {
    switch (c) {
        case ProvChannel::DirectObservation: return Fixed::pct(90);
        case ProvChannel::MarketFlow: return Fixed::pct(80);
        case ProvChannel::SpyNetwork: return Fixed::pct(75);
        case ProvChannel::Intercept: return Fixed::pct(70);
        case ProvChannel::Rumor: return Fixed::pct(35);
        case ProvChannel::Forgery: return Fixed::pct(20);
        case ProvChannel::Gift: return Fixed::pct(60);
        case ProvChannel::Archive: return Fixed::pct(85);
        case ProvChannel::Analysis: return Fixed::pct(65);
        case ProvChannel::Testimony: return Fixed::pct(55);
        case ProvChannel::Panopticon: return Fixed::pct(95);
        case ProvChannel::Count: break;
    }
    return Fixed::pct(50);
}

Fixed Provenance::effectiveCredibility(u64 nowTick) const {
    Fixed base = credibility;
    // 信号成本加成：上限 +20%
    Fixed costBonus = fxMin(signalCost * Fixed::pct(20), Fixed::pct(20));
    // 时间衰减：每 tick -1%，下限 35%
    i64 age = static_cast<i64>(nowTick) - static_cast<i64>(tick);
    if (age < 0) age = 0;
    Fixed decay = Fixed(1) - fxMin(Fixed::raw(age * 10), Fixed::pct(65));
    Fixed v = (base + costBonus) * decay;
    if (forged) v = v * Fixed::pct(45);
    return fxClamp(v, Fixed(0), Fixed(1));
}

std::string_view provChannelName(ProvChannel c) {
    switch (c) {
        case ProvChannel::DirectObservation: return "直接观测";
        case ProvChannel::MarketFlow: return "订单流痕迹";
        case ProvChannel::SpyNetwork: return "间谍网络";
        case ProvChannel::Intercept: return "通讯截获";
        case ProvChannel::Rumor: return "流言";
        case ProvChannel::Forgery: return "伪造";
        case ProvChannel::Gift: return "对方给予";
        case ProvChannel::Archive: return "档案";
        case ProvChannel::Analysis: return "推演";
        case ProvChannel::Testimony: return "证词";
        case ProvChannel::Panopticon: return "泛视网络";
        case ProvChannel::Count: break;
    }
    return "?";
}

ProvChannel provChannelFromName(std::string_view s) {
    for (int i = 0; i < static_cast<int>(ProvChannel::Count); ++i)
        if (provChannelName(static_cast<ProvChannel>(i)) == s) return static_cast<ProvChannel>(i);
    return ProvChannel::DirectObservation;
}

}  // namespace gf
