#include "domain/Species.h"

#include "util/Str.h"

namespace gf {
namespace {

using M = Modifier;
using K = ModKind;

constexpr SpeciesInfo kSpecies[] = {
    {0, "human", "人类", "大沉默前遍布各处的旧殖民者后裔，适应力强、政治灵活。",
     {{{K::ResearchRate, Fixed::pct(5)}, {K::DiploWeight, Fixed::pct(10)}, {}, {}}}, 2, "@"},
    {1, "vaker", "瓦克族", "以部族军功立国，崇尚正面决战，内政却因此常在动荡边缘。",
     {{{K::MilitaryPower, Fixed::pct(15)}, {K::Stability, Fixed::pct(-5)}, {}, {}}}, 2, "V"},
    {2, "helvi", "赫尔维商族", "把每一段航线都折算成利差；谈判桌上从不做亏本生意。",
     {{{K::TradeMargin, Fixed::pct(20)}, {K::MarketFee, Fixed::pct(-20)}, {}, {}}}, 2, "$"},
    {3, "psion", "灵族", "能听见别人没说出口的话，也因此很难相信任何人的沉默。",
     {{{K::IntelDefense, Fixed::pct(25)}, {K::ResearchRate, Fixed::pct(8)}, {}, {}}}, 2, "*"},
    {4, "ironoath", "铁誓机械", "以誓约条款驱动的自律集群，建造极快，却几乎不繁衍。",
     {{{K::BuildRate, Fixed::pct(20)}, {K::Growth, Fixed::pct(-15)}, {}, {}}}, 2, "#"},
    {5, "crystal", "晶体共生体", "记忆刻在晶格中，社会结构极其稳定，扩张冲动微弱。",
     {{{K::Stability, Fixed::pct(15)}, {K::Growth, Fixed::pct(-10)}, {}, {}}}, 2, "◆"},
    {6, "swarm", "游牧虫群", "种群即军队，增长压倒一切，却无法理解条约的意义。",
     {{{K::Growth, Fixed::pct(30)}, {K::DiploWeight, Fixed::pct(-25)}, {}, {}}}, 2, "W"},
    {7, "deepsea", "深海族", "在高压海渊中繁衍，对边疆殖民有天然亲和。",
     {{{K::ColonyCost, Fixed::pct(-20)}, {K::MilitaryPower, Fixed::pct(-8)}, {}, {}}}, 2, "~"},
    {8, "firerock", "火岩族", "以熔岩锻造为文明基石，工事与刀锋同样锋利。",
     {{{K::BuildRate, Fixed::pct(15)}, {K::MilitaryPower, Fixed::pct(10)}, {}, {}}}, 2, "^"},
    {9, "winged", "羽翼族", "三维机动的天空种族，对渗透与监听有敏锐直觉。",
     {{{K::Detection, Fixed::pct(20)}, {K::IntelDefense, Fixed::pct(10)}, {}, {}}}, 2, "A"},
    {10, "silicon", "硅基集群", "思考以世代计，科研深厚但迭代缓慢。",
     {{{K::ResearchRate, Fixed::pct(15)}, {K::Growth, Fixed::pct(-20)}, {}, {}}}, 2, "S"},
    {11, "entropy", "熵族", "以制造混乱为生存策略，善于操纵他者的认知。",
     {{{K::ManipulationSkill, Fixed::pct(30)}, {K::Stability, Fixed::pct(-15)}, {}, {}}}, 2, "%"},
    {12, "mirror", "镜面族", "每个个体都是彼此的备份，反间谍能力近乎本能。",
     {{{K::Detection, Fixed::pct(35)}, {K::TradeMargin, Fixed::pct(-10)}, {}, {}}}, 2, "|"},
    {13, "beastmaster", "巨兽驯者", "驱使原生巨兽作战，战力惊人，学术薄弱。",
     {{{K::MilitaryPower, Fixed::pct(20)}, {K::ResearchRate, Fixed::pct(-10)}, {}, {}}}, 2, "&"},
    {14, "voidwalker", "虚空行者", "在航线之外航行，殖民成本极低，社会却缺少根基。",
     {{{K::ColonyCost, Fixed::pct(-30)}, {K::Stability, Fixed::pct(-12)}, {}, {}}}, 2, "o"},
    {15, "clone", "克隆共和", "以批次诞生公民，人口增长迅速，个体影响力稀薄。",
     {{{K::Growth, Fixed::pct(20)}, {K::InfluenceGain, Fixed::pct(-15)}, {}, {}}}, 2, "="},
    {16, "timeweaver", "时间织者", "以长周期规划见长，科研卓越但工程推进迟缓。",
     {{{K::ResearchRate, Fixed::pct(20)}, {K::BuildRate, Fixed::pct(-15)}, {}, {}}}, 2, "T"},
    {17, "nameless", "无名者", "拒绝被记录在泛视网络中的种族，全项均衡但极难被信任。",
     {{{K::CreditRating, Fixed::pct(10)}, {K::DiploWeight, Fixed::pct(-30)}, {}, {}}}, 2, "?"},
};
static_assert(sizeof(kSpecies) / sizeof(kSpecies[0]) == static_cast<std::size_t>(kSpeciesCount));

constexpr EthicInfo kEthics[] = {
    {0, "ecology", "生态", "把行星当作需要照料的生命体，而非燃料。",
     {{{K::Growth, Fixed::pct(8)}, {K::Stability, Fixed::pct(6)}, {}}}, 2},
    {1, "militarism", "尚武", "武备即外交的语言。",
     {{{K::MilitaryPower, Fixed::pct(18)}, {K::DiploWeight, Fixed::pct(-8)}, {}}}, 2},
    {2, "commerce", "商贸", "让每一次交易都成为一次结盟。",
     {{{K::TradeMargin, Fixed::pct(15)}, {K::MarketFee, Fixed::pct(-10)}, {}}}, 2},
    {3, "science", "科学", "真相是可以被测量的，其余皆修辞。",
     {{{K::ResearchRate, Fixed::pct(15)}, {K::Detection, Fixed::pct(5)}, {}}}, 2},
    {4, "faith", "信仰", "沉默是神的语法，我们只是解读它的字母。",
     {{{K::Stability, Fixed::pct(12)}, {K::ResearchRate, Fixed::pct(-6)}, {}}}, 2},
    {5, "liberty", "自由", "没有任何中心有权看到全部。",
     {{{K::IntelDefense, Fixed::pct(12)}, {K::Stability, Fixed::pct(-6)}, {}}}, 2},
    {6, "order", "秩序", "层级是文明的骨架。",
     {{{K::Stability, Fixed::pct(14)}, {K::InfluenceGain, Fixed::pct(8)}, {}}}, 2},
    {7, "expansion", "扩张", "边界只有一种状态：正在移动。",
     {{{K::ColonyCost, Fixed::pct(-15)}, {K::Stability, Fixed::pct(-5)}, {}}}, 2},
    {8, "isolation", "孤立", "航线越少，麻烦越少。",
     {{{K::IntelDefense, Fixed::pct(15)}, {K::TradeMargin, Fixed::pct(-12)}, {}}}, 2},
    {9, "collectivism", "集体", "个体的偏好是可加总的噪声。",
     {{{K::InfluenceGain, Fixed::pct(12)}, {K::Growth, Fixed::pct(6)}, {}}}, 2},
    {10, "individualism", "个体", "每一个不同意都值得被记录。",
     {{{K::ResearchRate, Fixed::pct(10)}, {K::ManipulationSkill, Fixed::pct(-10)}, {}}}, 2},
    {11, "purity", "纯净", "混杂是衰败的第一个征兆。",
     {{{K::MilitaryPower, Fixed::pct(10)}, {K::TradeMargin, Fixed::pct(-8)}, {}}}, 2},
};
static_assert(sizeof(kEthics) / sizeof(kEthics[0]) == static_cast<std::size_t>(kEthicsCount));

constexpr CivicInfo kCivics[] = {
    {0, "mercantile", "重商传统", "商会直接参与立法。",
     {{{K::TradeMargin, Fixed::pct(12)}, {K::MarketFee, Fixed::pct(-8)}}}, 2, {6, 12, 0}, 2},
    {1, "technocracy", "技术官僚", "由通过率最低的考试决定谁发号施令。",
     {{{K::ResearchRate, Fixed::pct(12)}, {K::BuildRate, Fixed::pct(6)}}}, 2, {9, 0, 0}, 1},
    {2, "warriorCulture", "武德文化", "服役是公民权的唯一凭据。",
     {{{K::MilitaryPower, Fixed::pct(14)}, {K::DiploWeight, Fixed::pct(-6)}}}, 2, {7, 0, 0}, 1},
    {3, "pacifist", "和平主义", "宣战需要跨越极高的国内门槛。",
     {{{K::Stability, Fixed::pct(10)}, {K::MilitaryPower, Fixed::pct(-10)}}}, 2, {2, 0, 0}, 1},
    {4, "psionic", "灵能传统", "把心灵当作可训练的器官。",
     {{{K::IntelDefense, Fixed::pct(15)}, {K::ManipulationSkill, Fixed::pct(10)}}}, 2, {16, 0, 0}, 1},
    {5, "mechanized", "机械化", "政策由调度算法执行。",
     {{{K::BuildRate, Fixed::pct(14)}, {K::Growth, Fixed::pct(-10)}}}, 2, {0, 0, 0}, 0},
    {6, "hive", "蜂巢意识", "决策延迟接近于零，异议不存在。",
     {{{K::InfluenceGain, Fixed::pct(15)}, {K::ResearchRate, Fixed::pct(-8)}}}, 2, {5, 0, 0}, 1},
    {7, "feudal", "封建契约", "星系由世袭领主代管。",
     {{{K::Stability, Fixed::pct(12)}, {K::BuildRate, Fixed::pct(-8)}}}, 2, {0, 0, 0}, 0},
    {8, "democratic", "代议民主", "每一次预算都要过票。",
     {{{K::InfluenceGain, Fixed::pct(12)}, {K::BuildRate, Fixed::pct(-6)}}}, 2, {11, 0, 0}, 1},
    {9, "oligarchic", "寡头制", "少数家族掌握多数航线。",
     {{{K::TradeMargin, Fixed::pct(10)}, {K::Stability, Fixed::pct(-8)}}}, 2, {8, 0, 0}, 1},
    {10, "theocratic", "神权", "教义解释权即行政权。",
     {{{K::Stability, Fixed::pct(15)}, {K::ResearchRate, Fixed::pct(-10)}}}, 2, {3, 0, 0}, 1},
    {11, "corporate", "企业国", "公民即股东，选票按持股计。",
     {{{K::TradeMargin, Fixed::pct(16)}, {K::Stability, Fixed::pct(-10)}}}, 2, {8, 0, 0}, 1},
    {12, "nomadic", "游牧", "舰队即是国土。",
     {{{K::ColonyCost, Fixed::pct(-18)}, {K::BuildRate, Fixed::pct(-6)}}}, 2, {0, 0, 0}, 0},
    {13, "agrarian", "农本", "把粮食安全当作最高国策。",
     {{{K::Growth, Fixed::pct(12)}, {K::TradeMargin, Fixed::pct(-6)}}}, 2, {0, 0, 0}, 0},
    {14, "miningGuild", "矿业公会", "矿物即权力。",
     {{{K::BuildRate, Fixed::pct(10)}, {K::TradeMargin, Fixed::pct(4)}}}, 2, {0, 0, 0}, 0},
    {15, "industrialist", "工业主义", "产能决定一切。",
     {{{K::BuildRate, Fixed::pct(15)}, {K::Growth, Fixed::pct(-5)}}}, 2, {0, 0, 0}, 0},
    {16, "voidborn", "虚空之子", "在航线之外出生，适应真空。",
     {{{K::ColonyCost, Fixed::pct(-12)}, {K::MilitaryPower, Fixed::pct(6)}}}, 2, {0, 0, 0}, 0},
    {17, "shadowNetwork", "暗影网络", "情报机构与政府边界模糊。",
     {{{K::ManipulationSkill, Fixed::pct(18)}, {K::Detection, Fixed::pct(10)}}}, 2, {3, 0, 0}, 1},
    {18, "diplomatic", "外交传统", "语言是最便宜的武器。",
     {{{K::DiploWeight, Fixed::pct(18)}, {K::MilitaryPower, Fixed::pct(-6)}}}, 2, {2, 0, 0}, 1},
    {19, "scholarly", "学术传统", "档案比舰队更长久。",
     {{{K::ResearchRate, Fixed::pct(14)}, {K::Detection, Fixed::pct(6)}}}, 2, {0, 0, 0}, 0},
    {20, "artistic", "艺术庇护", "文明的价值在于它留下了什么。",
     {{{K::InfluenceGain, Fixed::pct(14)}, {K::TradeMargin, Fixed::pct(6)}}}, 2, {0, 0, 0}, 0},
    {21, "inquisitorial", "审判庭", "异端先于敌人被清算。",
     {{{K::Detection, Fixed::pct(20)}, {K::ResearchRate, Fixed::pct(-10)}}}, 2, {3, 11, 0}, 2},
    {22, "slaver", "劳役制", "以强制劳动压低成本。",
     {{{K::BuildRate, Fixed::pct(18)}, {K::Stability, Fixed::pct(-16)}}}, 2, {3, 8, 0}, 2},
    {23, "ecoUtopian", "生态乌托邦", "把宜居度作为唯一的国力指标。",
     {{{K::Growth, Fixed::pct(10)}, {K::Stability, Fixed::pct(10)}}}, 2, {22, 0, 0}, 1},
};
static_assert(sizeof(kCivics) / sizeof(kCivics[0]) == static_cast<std::size_t>(kCivicsCount));

constexpr GovernmentInfo kGovernments[] = {
    {0, "democracy", "民主制", "权力按票数流转，行动点适中，合法性高。", 1, Fixed::pct(100),
     Fixed::pct(-5), Fixed::pct(75)},
    {1, "republic", "共和制", "元老院与执政官分权。", 1, Fixed::pct(95), Fixed::pct(0), Fixed::pct(70)},
    {2, "oligarchy", "寡头制", "少数家族垄断决策。", 0, Fixed::pct(85), Fixed::pct(5), Fixed::pct(55)},
    {3, "autocracy", "独裁制", "一个人的意志即国家意志。", 2, Fixed::pct(70), Fixed::pct(-8),
     Fixed::pct(45)},
    {4, "empire", "帝制", "皇权与官僚体系并存。", 1, Fixed::pct(90), Fixed::pct(3), Fixed::pct(60)},
    {5, "theocracy", "神权制", "教义即法律。", 0, Fixed::pct(80), Fixed::pct(8), Fixed::pct(65)},
    {6, "technate", "技术官僚制", "由最优解算法统治。", 2, Fixed::pct(75), Fixed::pct(-3), Fixed::pct(58)},
    {7, "corporate", "企业制", "董事会即内阁。", 1, Fixed::pct(88), Fixed::pct(-6), Fixed::pct(50)},
    {8, "junta", "军事委员会", "战争状态是常态。", 1, Fixed::pct(82), Fixed::pct(-12), Fixed::pct(52)},
    {9, "hive", "蜂群意识", "没有派系，只有分工。", 3, Fixed::pct(60), Fixed::pct(15), Fixed::pct(80)},
    {10, "tribal", "部落议会", "长者与猎手共同议事。", 0, Fixed::pct(65), Fixed::pct(2), Fixed::pct(48)},
    {11, "anarchy", "无政府", "没有中央，只有协议。", 3, Fixed::pct(40), Fixed::pct(10), Fixed::pct(30)},
    {12, "cyberRepublic", "赛博共和", "投票实时进行，民意即政策。", 2, Fixed::pct(92), Fixed::pct(-10),
     Fixed::pct(68)},
    {13, "oracle", "神谕制", "由预测机器的输出决定国策。", 2, Fixed::pct(78), Fixed::pct(4),
     Fixed::pct(62)},
    // ---- 新增：四种带机制性 buff/debuff 的政体 ----
    // 设计原则：每个政体都要有**明确的强项与明确的代价**，
    // 而不是「数值略高/略低」。差异体现在工厂效率、民生开销、
    // 正当化速度、关系改善与腐败倾向上，直接影响玩法路线。
    {14, "socialist", "社会主义",
     "工厂归全民所有：产能极高，但福利与民生开支同样高昂。", 1, Fixed::pct(85), Fixed::pct(-6),
     Fixed::pct(66), Fixed::pct(25), Fixed::pct(30), Fixed(0), Fixed::pct(8), Fixed::pct(-4)},
    {15, "fascist", "法西斯主义",
     "国家即一切：镇压机器高效，对外扩张的借口唾手可得。", 2, Fixed::pct(65), Fixed::pct(-20),
     Fixed::pct(40), Fixed::pct(12), Fixed::pct(-8), Fixed::pct(40), Fixed::pct(-25), Fixed::pct(2)},
    {16, "capitalist", "资本主义",
     "资本说了算：市场繁荣、国库充盈，但腐败深入骨髓。", 1, Fixed::pct(95), Fixed::pct(-4),
     Fixed::pct(52), Fixed::pct(5), Fixed::pct(-5), Fixed::pct(-10), Fixed::pct(10), Fixed::pct(30)},
    {17, "parliamentary", "民主主义",
     "一切取决于议会：决策迟缓，但外交信誉与关系改善能力最强。", 1, Fixed::pct(98),
     Fixed::pct(-8), Fixed::pct(72), Fixed(0), Fixed::pct(6), Fixed::pct(-15), Fixed::pct(30),
     Fixed::pct(-12)},
};
static_assert(sizeof(kGovernments) / sizeof(kGovernments[0]) == static_cast<std::size_t>(kGovernmentCount));

std::string_view kModKindNames[] = {"贸易毛利", "市场费率", "研究速率", "建造速率", "军事力量", "稳定", "民怨",
                                    "情报防御", "外交权重", "殖民成本", "人口增长", "操纵技巧", "侦测",
                                    "影响力增益", "信用评级"};

}  // namespace

const SpeciesInfo& speciesInfo(int idx) {
    if (idx < 0 || idx >= kSpeciesCount) idx = 0;
    return kSpecies[static_cast<std::size_t>(idx)];
}
const EthicInfo& ethicInfo(int idx) {
    if (idx < 0 || idx >= kEthicsCount) idx = 0;
    return kEthics[static_cast<std::size_t>(idx)];
}
const CivicInfo& civicInfo(int idx) {
    if (idx < 0 || idx >= kCivicsCount) idx = 0;
    return kCivics[static_cast<std::size_t>(idx)];
}
const GovernmentInfo& governmentInfo(int idx) {
    if (idx < 0 || idx >= kGovernmentCount) idx = 0;
    return kGovernments[static_cast<std::size_t>(idx)];
}

int speciesIndexByName(std::string_view s) {
    for (int i = 0; i < kSpeciesCount; ++i) {
        if (iequals(kSpecies[static_cast<std::size_t>(i)].idName, s) ||
            kSpecies[static_cast<std::size_t>(i)].nameZh == s)
            return i;
    }
    return -1;
}
int ethicIndexByName(std::string_view s) {
    for (int i = 0; i < kEthicsCount; ++i) {
        if (iequals(kEthics[static_cast<std::size_t>(i)].idName, s) ||
            kEthics[static_cast<std::size_t>(i)].nameZh == s)
            return i;
    }
    return -1;
}
int civicIndexByName(std::string_view s) {
    for (int i = 0; i < kCivicsCount; ++i) {
        if (iequals(kCivics[static_cast<std::size_t>(i)].idName, s) ||
            kCivics[static_cast<std::size_t>(i)].nameZh == s)
            return i;
    }
    return -1;
}
int governmentIndexByName(std::string_view s) {
    for (int i = 0; i < kGovernmentCount; ++i) {
        if (iequals(kGovernments[static_cast<std::size_t>(i)].idName, s) ||
            kGovernments[static_cast<std::size_t>(i)].nameZh == s)
            return i;
    }
    return -1;
}

std::string_view modKindName(ModKind k) {
    std::size_t i = static_cast<std::size_t>(k);
    if (i >= static_cast<std::size_t>(ModKind::Count)) return "?";
    return kModKindNames[i];
}

}  // namespace gf
