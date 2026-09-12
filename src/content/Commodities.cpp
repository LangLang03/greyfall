#include "domain/Resource.h"

#include <cstring>

#include "util/Str.h"

namespace gf {
namespace {

// 22 种现货：基准价 / 基础波动率 / 仓储 / 运费 / 典型成交量
constexpr CommodityInfo kCommodities[] = {
    {Commodity::Energy, "energy", "能源", "ENR", EcoCategory::Raw, Fixed::raw(1200), Fixed::pct(3),
     Fixed::raw(2), Fixed::pct(40), 180000, true, false},
    {Commodity::Minerals, "minerals", "矿物", "MIN", EcoCategory::Raw, Fixed::raw(2400), Fixed::pct(3),
     Fixed::raw(3), Fixed::pct(45), 150000, true, false},
    {Commodity::Food, "food", "食物", "FOD", EcoCategory::Raw, Fixed::raw(3100), Fixed::pct(2),
     Fixed::raw(4), Fixed::pct(50), 120000, true, false},
    {Commodity::Alloys, "alloys", "合金", "ALY", EcoCategory::Industrial, Fixed::raw(31400), Fixed::pct(4),
     Fixed::raw(6), Fixed::pct(60), 42000, true, true},
    {Commodity::Components, "components", "部件", "CMP", EcoCategory::Industrial, Fixed::raw(58000),
     Fixed::pct(4), Fixed::raw(9), Fixed::pct(65), 26000, true, true},
    {Commodity::Medicines, "medicines", "药品", "MED", EcoCategory::Consumer, Fixed::raw(44000), Fixed::pct(5),
     Fixed::raw(11), Fixed::pct(70), 18000, true, true},
    {Commodity::Supermaterials, "supermaterials", "超材料", "SPM", EcoCategory::Strategic,
     Fixed::raw(142000), Fixed::pct(6), Fixed::raw(18), Fixed::pct(80), 8600, true, true},
    {Commodity::Exotic, "exotic", "异物质", "EXO", EcoCategory::Strategic, Fixed::raw(320000), Fixed::pct(9),
     Fixed::raw(26), Fixed::pct(120), 4200, true, true},
    {Commodity::DataCrystals, "datacrystals", "数据晶", "DTC", EcoCategory::Strategic, Fixed::raw(210000),
     Fixed::pct(8), Fixed::raw(15), Fixed::pct(90), 6200, true, true},
    {Commodity::Influence, "influence", "影响力", "INF", EcoCategory::Political, Fixed::raw(150000),
     Fixed::pct(4), Fixed::raw(0), Fixed::pct(30), 5200, true, false},
    {Commodity::Unity, "unity", "凝聚力", "UNI", EcoCategory::Political, Fixed::raw(95000), Fixed::pct(3),
     Fixed::raw(0), Fixed::pct(30), 7400, true, false},
    {Commodity::Credits, "credits", "信用点", "CRD", EcoCategory::Currency, Fixed::raw(1000), Fixed::raw(2),
     Fixed::raw(0), Fixed::raw(0), 0, false, false},
    {Commodity::Luxury, "luxury", "奢侈品", "LUX", EcoCategory::Consumer, Fixed::raw(76000), Fixed::pct(5),
     Fixed::raw(13), Fixed::pct(75), 11000, true, false},
    {Commodity::Volatiles, "volatiles", "挥发物", "VOL", EcoCategory::Raw, Fixed::raw(5600), Fixed::pct(4),
     Fixed::raw(5), Fixed::pct(50), 96000, true, false},
    {Commodity::RareGases, "raregases", "稀有气体", "RGS", EcoCategory::Raw, Fixed::raw(18000), Fixed::pct(5),
     Fixed::raw(8), Fixed::pct(60), 34000, true, false},
    {Commodity::Polymers, "polymers", "聚合物", "PLY", EcoCategory::Industrial, Fixed::raw(12500), Fixed::pct(3),
     Fixed::raw(5), Fixed::pct(50), 62000, true, false},
    {Commodity::Electronics, "electronics", "电子元件", "ELC", EcoCategory::Industrial, Fixed::raw(41000),
     Fixed::pct(4), Fixed::raw(8), Fixed::pct(60), 28000, true, false},
    {Commodity::Robotics, "robotics", "机器人", "ROB", EcoCategory::Industrial, Fixed::raw(88000), Fixed::pct(5),
     Fixed::raw(12), Fixed::pct(70), 13000, true, true},
    {Commodity::Bioproducts, "bioproducts", "生物制品", "BIO", EcoCategory::Consumer, Fixed::raw(27000),
     Fixed::pct(4), Fixed::raw(9), Fixed::pct(65), 31000, true, false},
    {Commodity::Antimatter, "antimatter", "反物质", "AMT", EcoCategory::Strategic, Fixed::raw(480000),
     Fixed::pct(12), Fixed::raw(40), Fixed::pct(160), 2100, true, true},
    {Commodity::Relics, "relics", "遗物", "RLC", EcoCategory::Strategic, Fixed::raw(260000), Fixed::pct(15),
     Fixed::raw(20), Fixed::pct(110), 1400, true, false},
    {Commodity::Contraband, "contraband", "违禁品", "CTB", EcoCategory::Consumer, Fixed::raw(130000),
     Fixed::pct(18), Fixed::raw(6), Fixed::pct(140), 3800, true, false},
};
static_assert(sizeof(kCommodities) / sizeof(kCommodities[0]) == static_cast<std::size_t>(kCommodityCount),
              "商品表数量必须等于 kCommodityCount");

constexpr ExchangeInfo kExchanges[] = {
    {ExchangeKind::CoreRing, "CX", "核心区环币交易所", Fixed::bp(20), Fixed::pct(1), Fixed::pct(85),
     Fixed::pct(100), false},
    {ExchangeKind::Frontier, "FX", "边疆自由市场", Fixed::bp(60), Fixed::pct(3), Fixed::pct(35),
     Fixed::pct(60), false},
    {ExchangeKind::BlackMarket, "BZ", "黑市", Fixed::pct(2), Fixed::pct(5), Fixed::pct(5), Fixed::pct(30),
     true},
};
static_assert(sizeof(kExchanges) / sizeof(kExchanges[0]) == static_cast<std::size_t>(kExchangeCount),
              "交易所表数量必须等于 kExchangeCount");

std::string_view kCategoryNames[] = {"原料", "工业品", "消费品", "战略物资", "政治资源", "通货"};

}  // namespace

const CommodityInfo& commodityInfo(Commodity c) {
    std::size_t i = static_cast<std::size_t>(c);
    if (i >= static_cast<std::size_t>(kCommodityCount)) i = 0;
    return kCommodities[i];
}
const CommodityInfo& commodityInfo(int idx) {
    if (idx < 0 || idx >= kCommodityCount) idx = 0;
    return kCommodities[static_cast<std::size_t>(idx)];
}
const ExchangeInfo& exchangeInfo(int idx) {
    if (idx < 0 || idx >= kExchangeCount) idx = 0;
    return kExchanges[static_cast<std::size_t>(idx)];
}

int commodityIndexByName(std::string_view name) {
    for (int i = 0; i < kCommodityCount; ++i) {
        if (kCommodities[static_cast<std::size_t>(i)].idName == name) return i;
        if (iequals(kCommodities[static_cast<std::size_t>(i)].idName, name)) return i;
    }
    // 允许用中文名检索
    for (int i = 0; i < kCommodityCount; ++i)
        if (kCommodities[static_cast<std::size_t>(i)].nameZh == name) return i;
    return -1;
}

int exchangeIndexByName(std::string_view name) {
    for (int i = 0; i < kExchangeCount; ++i) {
        if (iequals(kExchanges[static_cast<std::size_t>(i)].idName, name)) return i;
        if (kExchanges[static_cast<std::size_t>(i)].nameZh == name) return i;
    }
    return -1;
}

std::string_view commodityName(int idx) {
    if (idx < 0 || idx >= kCommodityCount) return "?";
    return kCommodities[static_cast<std::size_t>(idx)].nameZh;
}

std::string_view categoryName(EcoCategory c) {
    std::size_t i = static_cast<std::size_t>(c);
    if (i >= static_cast<std::size_t>(EcoCategory::Count)) return "?";
    return kCategoryNames[i];
}

int futuresTermTicks(int term) { return (term < 0 ? 0 : term) + 1; }

}  // namespace gf
