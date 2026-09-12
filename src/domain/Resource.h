#pragma once
// 标的信息表：22 种现货 + 4 期期货 + 3 类交易所
#include <array>
#include <string_view>

#include "util/Fixed.h"

namespace gf {

enum class Commodity : u8 {
    Energy = 0,
    Minerals,
    Food,
    Alloys,
    Components,
    Medicines,
    Supermaterials,
    Exotic,
    DataCrystals,
    Influence,
    Unity,
    Credits,
    Luxury,
    Volatiles,
    RareGases,
    Polymers,
    Electronics,
    Robotics,
    Bioproducts,
    Antimatter,
    Relics,
    Contraband,
    Count,
};

inline constexpr int kCommodityCount = static_cast<int>(Commodity::Count);
inline constexpr int kFuturesTerms = 4;   // F1..F4
inline constexpr int kExchangeCount = 3;  // CX / FX / BZ
inline constexpr int kBookDepthMax = 96;  // 每侧最多 96 档（方案 §6.2）

enum class EcoCategory : u8 { Raw, Industrial, Consumer, Strategic, Political, Currency, Count };

enum class ExchangeKind : u8 { CoreRing = 0, Frontier = 1, BlackMarket = 2 };

struct CommodityInfo {
    Commodity id;
    std::string_view idName;  // 命令行标识："alloys"
    std::string_view nameZh;  // 中文显示名
    std::string_view symbol;  // 3 字母符号
    EcoCategory cat;
    Fixed basePrice;      // 基准价（初始做市报价锚）
    Fixed volatility;     // 基础波动率 σ_daily
    Fixed storage;        // 每单位每 tick 仓储成本
    Fixed freight;        // 基础运费系数（跨所套利偏差来源）
    i64 typicalVolume;    // 典型 tick 成交量（V20 量级）
    bool tradable;        // 是否可在公开市场交易
    bool strategic;       // 是否触发霸权/封锁关注
};

struct ExchangeInfo {
    ExchangeKind kind;
    std::string_view idName;  // "CX"
    std::string_view nameZh;
    Fixed fee;                // 单边手续费率
    Fixed transitCost;        // 跨所运费
    Fixed regulator;          // 监管强度 0..1（操纵被查概率的基础）
    Fixed depthBonus;         // 流动性加成
    bool blackMarket;
};

[[nodiscard]] const CommodityInfo& commodityInfo(Commodity c);
[[nodiscard]] const CommodityInfo& commodityInfo(int idx);
[[nodiscard]] const ExchangeInfo& exchangeInfo(int idx);
[[nodiscard]] int commodityIndexByName(std::string_view idName);  // -1 未找到
[[nodiscard]] int exchangeIndexByName(std::string_view idName);
[[nodiscard]] std::string_view commodityName(int idx);
[[nodiscard]] std::string_view categoryName(EcoCategory c);
/// 期货期限：F1=1 季度 … F4=4 季度
[[nodiscard]] int futuresTermTicks(int term);

}  // namespace gf
