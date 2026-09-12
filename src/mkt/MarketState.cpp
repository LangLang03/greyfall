#include "mkt/MarketState.h"

namespace gf {

const char* orderKindName(OrderKind k) {
    switch (k) {
        case OrderKind::Limit: return "限价";
        case OrderKind::Market: return "市价";
        case OrderKind::Iceberg: return "冰量";
        case OrderKind::Stop: return "止损";
    }
    return "?";
}

const char* tifName(Tif t) { return t == Tif::Day ? "day" : "gtc"; }

const char* manipKindName(ManipKind k) {
    switch (k) {
        case ManipKind::WashTrading: return "洗售";
        case ManipKind::Spoofing: return "虚假挂单";
        case ManipKind::Corner: return "囤积逼空";
        case ManipKind::PumpDump: return "拉抬出货";
        case ManipKind::FrontRunning: return "前置交易";
        case ManipKind::Insider: return "内幕交易";
        case ManipKind::Count: break;
    }
    return "?";
}

ManipKind manipKindFromName(std::string_view s) {
    for (int i = 0; i < static_cast<int>(ManipKind::Count); ++i)
        if (s == manipKindName(static_cast<ManipKind>(i))) return static_cast<ManipKind>(i);
    return ManipKind::WashTrading;
}

}  // namespace gf
