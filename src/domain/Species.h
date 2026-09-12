#pragma once
// 种族 / 伦理 / 公民特质 / 政体 —— 表定义（数据在 content/ 中，只读）
#include <array>
#include <string_view>
#include <vector>

#include "util/Fixed.h"

namespace gf {

inline constexpr int kSpeciesCount = 18;
inline constexpr int kEthicsCount = 12;
inline constexpr int kCivicsCount = 24;
inline constexpr int kGovernmentCount = 18;

/// 伦理轴（每种伦理给一组全局修正）
enum class EthicAxis : u8 { Ecology, Militarism, Commerce, Science, Faith, Liberty, Order, Expansion,
                             Isolation, Collectivism, Individualism, Purity, Count };

/// 修正维度
enum class ModKind : u8 {
    TradeMargin, MarketFee, ResearchRate, BuildRate, MilitaryPower, Stability, Unrest, IntelDefense,
    DiploWeight, ColonyCost, Growth, ManipulationSkill, Detection, InfluenceGain, CreditRating, Count
};

struct Modifier {
    ModKind kind;
    Fixed value;
};

struct SpeciesInfo {
    u8 id;
    std::string_view idName;
    std::string_view nameZh;
    std::string_view desc;
    std::array<Modifier, 4> mods;
    u8 modCount;
    std::string_view portraitGlyph;
};

struct EthicInfo {
    u8 id;
    std::string_view idName;
    std::string_view nameZh;
    std::string_view desc;
    std::array<Modifier, 3> mods;
    u8 modCount;
};

struct CivicInfo {
    u8 id;
    std::string_view idName;
    std::string_view nameZh;
    std::string_view desc;
    std::array<Modifier, 2> mods;
    u8 modCount;
    /// 互斥公民（同持时效果冲突，最多 3 个）
    std::array<u8, 3> conflicts{};
    u8 conflictCount = 0;
};

struct GovernmentInfo {
    u8 id;
    std::string_view idName;
    std::string_view nameZh;
    std::string_view desc;
    int apBonus;          // 行动点加成
    Fixed voteWeight;     // 联邦投票权重系数
    Fixed unrestBias;     // 民心偏移
    Fixed legitimacy;     // 合法性基线
    // ---- 政体差异化修正（带默认值，既有条目可省略）----
    /// 工厂/建造效率：社会主义最高
    Fixed buildEfficiency = Fixed(0);
    /// 民生开销：社会主义最高（高福利的代价）
    Fixed upkeepBias = Fixed(0);
    /// 正当化速度：法西斯最高（更易获得战争理由）
    Fixed justificationBonus = Fixed(0);
    /// 关系改善：民主主义最高（议会外交）
    Fixed opinionGain = Fixed(0);
    /// 腐败倾向：资本主义最高
    Fixed corruptionBias = Fixed(0);
    /// 议会依赖度：民主主义最高（政策过渡更慢）
    Fixed parliamentaryDrag = Fixed(0);
};

[[nodiscard]] const SpeciesInfo& speciesInfo(int idx);
[[nodiscard]] const EthicInfo& ethicInfo(int idx);
[[nodiscard]] const CivicInfo& civicInfo(int idx);
[[nodiscard]] const GovernmentInfo& governmentInfo(int idx);
[[nodiscard]] int speciesIndexByName(std::string_view s);
[[nodiscard]] int ethicIndexByName(std::string_view s);
[[nodiscard]] int civicIndexByName(std::string_view s);
[[nodiscard]] int governmentIndexByName(std::string_view s);
[[nodiscard]] std::string_view modKindName(ModKind k);

}  // namespace gf
