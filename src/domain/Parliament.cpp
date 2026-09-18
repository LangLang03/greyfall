#include "domain/Parliament.h"

#include <algorithm>

#include "util/TextTable.h"
#include "core/GameState.h"
#include "domain/Treaty.h"
#include "gen/EmpireGen.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {
namespace {

using F = FactionKind;
using K = ModKind;
using C = BillCategory;
using S = Stance;

constexpr std::size_t kF = static_cast<std::size_t>(FactionKind::Count);

BillEffect E(K k, Fixed v) { return BillEffect{k, v}; }

/// 便捷构造立场数组：默认 0（未定），指定若干派系的立场
std::array<i8, kF> stances(std::initializer_list<std::pair<F, int>> list) {
    std::array<i8, kF> a{};
    for (auto& p : list) a[static_cast<std::size_t>(p.first)] = static_cast<i8>(p.second);
    return a;
}

struct Seed {
    C cat;
    const char* idName;
    const char* zh;
    const char* desc;
    std::array<i8, kF> base;
    std::array<BillEffect, 3> eff;
    u8 n;
    i64 cost;
    Fixed unrest;
    bool radical;
};

const std::vector<Seed>& seeds() {
    static const std::vector<Seed> s = {
        // ================= 经济 =================
        {C::Economic, "landReform", "土地改革",
         "重新分配耕地与矿权：劳工与民粹受益，旧贵族强烈反对。",
         stances({{F::Labor, 2}, {F::Populist, 2}, {F::Nobility, -2}, {F::Merchant, -1}}),
         {E(K::Growth, Fixed::pct(12)), E(K::Unrest, Fixed::pct(-10)), E(K::Stability, Fixed::pct(-6))}, 3, 300,
         Fixed::pct(-4), true},
        {C::Economic, "tariffWall", "关税壁垒",
         "对外国商品课以重税：本国工业受益，商会与消费者受损。",
         stances({{F::Labor, 2}, {F::Military, 1}, {F::Merchant, -2}, {F::Technocrat, -1}}),
         {E(K::BuildRate, Fixed::pct(14)), E(K::TradeMargin, Fixed::pct(-16)), E(K::Stability, Fixed::pct(4))}, 3,
         250, Fixed(0), false},
        {C::Economic, "freeTradeAct", "自由贸易法案",
         "拆除关税壁垒：商会与研究受益，劳工与军部反对。",
         stances({{F::Merchant, 2}, {F::Technocrat, 2}, {F::Labor, -2}, {F::Military, -1}}),
         {E(K::TradeMargin, Fixed::pct(18)), E(K::ResearchRate, Fixed::pct(10)), E(K::Stability, Fixed::pct(-5))},
         3, 350, Fixed(0), false},
        {C::Economic, "centralBank", "中央银行法",
         "建立独立央行：信用与稳定提升，但资本权力扩大。",
         stances({{F::Merchant, 2}, {F::Technocrat, 2}, {F::Labor, -1}, {F::Populist, -1}}),
         {E(K::CreditRating, Fixed::pct(20)), E(K::TradeMargin, Fixed::pct(8)), E(K::Unrest, Fixed::pct(4))}, 3, 400,
         Fixed(0), false},
        {C::Economic, "publicWorks", "公共工程法",
         "大规模基建投资：建造与人口增长提升，财政负担加重。",
         stances({{F::Labor, 2}, {F::Populist, 1}, {F::Nobility, -1}, {F::Syndicate, -1}}),
         {E(K::BuildRate, Fixed::pct(20)), E(K::Growth, Fixed::pct(10)), E(K::TradeMargin, Fixed::pct(-8))}, 3, 300,
         Fixed::pct(-6), false},
        {C::Economic, "privatization", "私有化法案",
         "出售国有企业：国库充盈，劳工与民粹反对。",
         stances({{F::Merchant, 2}, {F::Nobility, 1}, {F::Labor, -2}, {F::Populist, -2}}),
         {E(K::TradeMargin, Fixed::pct(16)), E(K::CreditRating, Fixed::pct(10)), E(K::Unrest, Fixed::pct(10))}, 3,
         300, Fixed::pct(6), true},

        // ================= 军事 =================
        {C::Military, "universalService", "普遍兵役法",
         "所有公民须服役：军力大增，社会负担沉重。",
         stances({{F::Military, 2}, {F::Nobility, 1}, {F::Labor, -1}, {F::Populist, -2}}),
         {E(K::MilitaryPower, Fixed::pct(24)), E(K::Growth, Fixed::pct(-8)), E(K::Unrest, Fixed::pct(8))}, 3, 300,
         Fixed::pct(4), false},
        {C::Military, "veteranBenefits", "退伍军人保障法",
         "提高退伍待遇：军部与劳工满意，财政承压。",
         stances({{F::Military, 2}, {F::Labor, 2}, {F::Merchant, -1}, {F::Technocrat, -1}}),
         {E(K::Stability, Fixed::pct(10)), E(K::MilitaryPower, Fixed::pct(8)), E(K::TradeMargin, Fixed::pct(-6))}, 3,
         200, Fixed::pct(-4), false},
        {C::Military, "defenseBudgetCut", "裁军法案",
         "削减军费：财政与民生受益，军部强烈反对。",
         stances({{F::Labor, 2}, {F::Merchant, 1}, {F::Military, -2}, {F::Nobility, -1}}),
         {E(K::TradeMargin, Fixed::pct(12)), E(K::Growth, Fixed::pct(8)), E(K::MilitaryPower, Fixed::pct(-18))}, 3,
         350, Fixed(0), true},
        {C::Military, "conscriptionOfScience", "科学征兵令",
         "征召学者入伍：短期军力提升，研究受损。",
         stances({{F::Military, 2}, {F::Fundamentalist, 1}, {F::Technocrat, -2}, {F::Merchant, -1}}),
         {E(K::MilitaryPower, Fixed::pct(16)), E(K::ResearchRate, Fixed::pct(-14)), E(K::Detection, Fixed::pct(10))},
         3, 250, Fixed(0), false},
        {C::Military, "arsenalOfDemocracy", "民主兵工厂",
         "以工业产能支撑军备：建造与军力同步提升。",
         stances({{F::Military, 2}, {F::Technocrat, 2}, {F::Merchant, 1}, {F::Fundamentalist, -1}}),
         {E(K::MilitaryPower, Fixed::pct(14)), E(K::BuildRate, Fixed::pct(12)), E(K::TradeMargin, Fixed::pct(-6))},
         3, 400, Fixed(0), false},

        // ================= 社会 =================
        {C::Social, "universalSuffrage", "普选权法案",
         "扩大选举权：民粹与劳工受益，旧贵族与保守派反对。",
         stances({{F::Populist, 2}, {F::Labor, 2}, {F::Nobility, -2}, {F::Fundamentalist, -1}}),
         {E(K::InfluenceGain, Fixed::pct(16)), E(K::Unrest, Fixed::pct(-12)), E(K::Stability, Fixed::pct(-8))}, 3,
         400, Fixed::pct(-6), true},
        {C::Social, "censorshipAct", "出版审查法",
         "管制出版物：稳定与反间谍提升，研究与民意受损。",
         stances({{F::Fundamentalist, 2}, {F::Military, 1}, {F::Technocrat, -2}, {F::Merchant, -1}, {F::Populist, -1}}),
         {E(K::Stability, Fixed::pct(16)), E(K::IntelDefense, Fixed::pct(14)), E(K::ResearchRate, Fixed::pct(-12))},
         3, 250, Fixed::pct(6), false},
        {C::Social, "secularEducation", "世俗教育法",
         "教育脱离教权：研究与技术受益，原教旨强烈反对。",
         stances({{F::Technocrat, 2}, {F::Merchant, 1}, {F::Fundamentalist, -2}, {F::Nobility, -1}}),
         {E(K::ResearchRate, Fixed::pct(20)), E(K::Growth, Fixed::pct(8)), E(K::Stability, Fixed::pct(-6))}, 3, 350,
         Fixed(0), true},
        {C::Social, "laborUnions", "工会合法化",
         "承认工会权利：劳工满意，商会与辛迪加反对。",
         stances({{F::Labor, 2}, {F::Populist, 1}, {F::Merchant, -2}, {F::Syndicate, -2}}),
         {E(K::Unrest, Fixed::pct(-14)), E(K::Growth, Fixed::pct(6)), E(K::TradeMargin, Fixed::pct(-10))}, 3, 300,
         Fixed::pct(-6), false},
        {C::Social, "healthcareAct", "全民医疗法",
         "建立公共医疗：人口与稳定提升，财政压力大。",
         stances({{F::Labor, 2}, {F::Populist, 2}, {F::Nobility, -1}, {F::Syndicate, -1}}),
         {E(K::Growth, Fixed::pct(18)), E(K::Unrest, Fixed::pct(-16)), E(K::TradeMargin, Fixed::pct(-10))}, 3, 450,
         Fixed::pct(-8), false},
        {C::Social, "migrationControl", "移民管制法",
         "限制人口流动：稳定与纯净派受益，劳工与增长受损。",
         stances({{F::Fundamentalist, 2}, {F::Nobility, 1}, {F::Labor, -1}, {F::Merchant, -2}}),
         {E(K::Stability, Fixed::pct(12)), E(K::Growth, Fixed::pct(-14)), E(K::Unrest, Fixed::pct(6))}, 3, 250,
         Fixed(0), false},

        // ================= 政治 =================
        {C::Political, "termLimits", "任期限制法",
         "限制执政任期：合法性提升，既得利益集团反对。",
         stances({{F::Populist, 2}, {F::Technocrat, 1}, {F::Nobility, -2}, {F::Syndicate, -1}}),
         {E(K::Stability, Fixed::pct(10)), E(K::InfluenceGain, Fixed::pct(12)), E(K::Unrest, Fixed::pct(-6))}, 3, 400,
         Fixed(0), true},
        {C::Political, "emergencyPowers", "紧急状态法",
         "授予行政紧急权力：稳定与军力提升，合法性受损。",
         stances({{F::Military, 2}, {F::Fundamentalist, 1}, {F::Populist, -2}, {F::Labor, -1}, {F::Merchant, -1}}),
         {E(K::Stability, Fixed::pct(22)), E(K::MilitaryPower, Fixed::pct(12)), E(K::InfluenceGain, Fixed::pct(-14))},
         3, 350, Fixed::pct(8), true},
        {C::Political, "antiCorruption", "反腐法案",
         "设立独立监察：合法性提升，辛迪加与贵族受损。",
         stances({{F::Populist, 2}, {F::Technocrat, 2}, {F::Labor, 1}, {F::Syndicate, -2}, {F::Nobility, -1}}),
         {E(K::CreditRating, Fixed::pct(14)), E(K::InfluenceGain, Fixed::pct(10)), E(K::TradeMargin, Fixed::pct(-6))},
         3, 400, Fixed::pct(-6), false},
        {C::Political, "nobilityRestoration", "贵族复权法",
         "恢复世袭特权：稳定与军部受益，民粹与劳工反对。",
         stances({{F::Nobility, 2}, {F::Military, 1}, {F::Populist, -2}, {F::Labor, -2}, {F::Merchant, -1}}),
         {E(K::Stability, Fixed::pct(14)), E(K::MilitaryPower, Fixed::pct(8)), E(K::ResearchRate, Fixed::pct(-10))}, 3,
         350, Fixed::pct(6), true},
        {C::Political, "technocraticCouncil", "技术委员会法",
         "由专家委员会主导决策：研究建造提升，民意代表性下降。",
         stances({{F::Technocrat, 2}, {F::Merchant, 1}, {F::Populist, -2}, {F::Nobility, -1}}),
         {E(K::ResearchRate, Fixed::pct(18)), E(K::BuildRate, Fixed::pct(12)), E(K::InfluenceGain, Fixed::pct(-10))},
         3, 450, Fixed(0), true},
        {C::Political, "syndicateAmnesty", "辛迪加特赦法",
         "赦免灰色资本：贸易繁荣，合法性受损。",
         stances({{F::Syndicate, 2}, {F::Merchant, 1}, {F::Populist, -2}, {F::Fundamentalist, -2}}),
         {E(K::TradeMargin, Fixed::pct(20)), E(K::ManipulationSkill, Fixed::pct(14)), E(K::Stability, Fixed::pct(-12))},
         3, 300, Fixed::pct(8), true},

        // ================= 外交 =================
        {C::Diplomatic, "nonIntervention", "不干涉法案",
         "承诺不介入他国事务：稳定提升，外交权重下降。",
         stances({{F::Populist, 2}, {F::Labor, 1}, {F::Military, -2}, {F::Merchant, -1}}),
         {E(K::Stability, Fixed::pct(12)), E(K::Unrest, Fixed::pct(-8)), E(K::DiploWeight, Fixed::pct(-14))}, 3, 250,
         Fixed(0), false},
        {C::Diplomatic, "collectiveSecurity", "集体安全法案",
         "缔结集体防御体系：外交与军力提升，招致敌意。",
         stances({{F::Military, 2}, {F::Technocrat, 1}, {F::Populist, -1}, {F::Nobility, -1}}),
         {E(K::DiploWeight, Fixed::pct(20)), E(K::MilitaryPower, Fixed::pct(10)), E(K::TradeMargin, Fixed::pct(-8))},
         3, 400, Fixed(0), false},
        {C::Diplomatic, "tradeEmbargoAct", "贸易禁运法",
         "对敌国全面禁运：本国强硬派满意，商会受损。",
         stances({{F::Military, 2}, {F::Labor, 1}, {F::Merchant, -2}, {F::Technocrat, -1}}),
         {E(K::MilitaryPower, Fixed::pct(12)), E(K::TradeMargin, Fixed::pct(-16)), E(K::DiploWeight, Fixed::pct(8))},
         3, 300, Fixed(0), false},
        {C::Diplomatic, "foreignAidAct", "对外援助法",
         "以援助换取影响力：外交提升，财政承压。",
         stances({{F::Merchant, 2}, {F::Populist, 1}, {F::Military, -1}, {F::Labor, -1}}),
         {E(K::DiploWeight, Fixed::pct(18)), E(K::InfluenceGain, Fixed::pct(14)), E(K::TradeMargin, Fixed::pct(-8))},
         3, 350, Fixed(0), false},
        {C::Diplomatic, "isolationAct", "闭关法案",
         "关闭对外通道：反间谍与稳定提升，贸易外交萎缩。",
         stances({{F::Fundamentalist, 2}, {F::Nobility, 1}, {F::Merchant, -2}, {F::Technocrat, -1}}),
         {E(K::IntelDefense, Fixed::pct(24)), E(K::Stability, Fixed::pct(14)), E(K::DiploWeight, Fixed::pct(-22))},
         3, 400, Fixed(0), true},
        // ================= 补充 =================
        {C::Economic, "industrialSubsidy", "产业补贴法",
         "补贴战略产业：建造与信用提升，财政承压、商会得利。",
         stances({{F::Technocrat, 2}, {F::Merchant, 1}, {F::Populist, -1}, {F::Labor, -1}}),
         {E(K::BuildRate, Fixed::pct(16)), E(K::CreditRating, Fixed::pct(8)), E(K::Unrest, Fixed::pct(4))}, 3, 300,
         Fixed(0), false},
        {C::Economic, "wealthTax", "财富税法",
         "对巨额财富征税：民怨大降，商会与贵族激烈反对。",
         stances({{F::Labor, 2}, {F::Populist, 2}, {F::Merchant, -2}, {F::Nobility, -2}, {F::Syndicate, -1}}),
         {E(K::Unrest, Fixed::pct(-18)), E(K::Growth, Fixed::pct(6)), E(K::TradeMargin, Fixed::pct(-14))}, 3, 400,
         Fixed::pct(-8), true},
        {C::Military, "navalExpansion", "海军扩张法",
         "扩建远洋舰队：军力与外交提升，财政负担重。",
         stances({{F::Military, 2}, {F::Merchant, 1}, {F::Labor, -1}, {F::Populist, -1}}),
         {E(K::MilitaryPower, Fixed::pct(18)), E(K::DiploWeight, Fixed::pct(10)), E(K::TradeMargin, Fixed::pct(-8))},
         3, 350, Fixed(0), false},
        {C::Social, "religiousToleration", "宗教宽容法",
         "允许信仰自由：稳定与影响力提升，原教旨不满。",
         stances({{F::Merchant, 2}, {F::Technocrat, 1}, {F::Populist, 1}, {F::Fundamentalist, -2}}),
         {E(K::Stability, Fixed::pct(12)), E(K::InfluenceGain, Fixed::pct(10)), E(K::Unrest, Fixed::pct(-8))}, 3,
         300, Fixed::pct(-4), false},
        {C::Political, "decentralization", "地方分权法",
         "下放权力给地方：增长与稳定提升，中央影响力下降。",
         stances({{F::Populist, 2}, {F::Nobility, 1}, {F::Technocrat, -1}, {F::Military, -2}}),
         {E(K::Growth, Fixed::pct(12)), E(K::Stability, Fixed::pct(10)), E(K::InfluenceGain, Fixed::pct(-14))}, 3,
         350, Fixed(0), true},
        {C::Political, "secretBallot", "秘密投票法",
         "表决不记名：合法性提升，辛迪加与贵族影响下降。",
         stances({{F::Populist, 2}, {F::Labor, 2}, {F::Technocrat, 1}, {F::Syndicate, -2}, {F::Nobility, -1}}),
         {E(K::InfluenceGain, Fixed::pct(14)), E(K::Stability, Fixed::pct(8)), E(K::ManipulationSkill, Fixed::pct(-12))},
         3, 300, Fixed::pct(-4), false},
        {C::Diplomatic, "armsControl", "军备控制条约",
         "与他国互减军备：稳定与外交提升，军部强烈反对。",
         stances({{F::Merchant, 2}, {F::Populist, 2}, {F::Military, -2}, {F::Nobility, -1}}),
         {E(K::DiploWeight, Fixed::pct(16)), E(K::Stability, Fixed::pct(10)), E(K::MilitaryPower, Fixed::pct(-14))},
         3, 400, Fixed(0), true},
        {C::Diplomatic, "openBorders", "开放边境法",
         "允许人口与思想自由流动：研究增长提升，纯净派反对。",
         stances({{F::Technocrat, 2}, {F::Merchant, 2}, {F::Fundamentalist, -2}, {F::Nobility, -1}}),
         {E(K::ResearchRate, Fixed::pct(14)), E(K::Growth, Fixed::pct(12)), E(K::Stability, Fixed::pct(-8))}, 3,
         350, Fixed(0), false},
    };
    return s;
}

}  // namespace

const BillDef& billDef(int idx) {
    static const std::vector<BillDef> table = [] {
        std::vector<BillDef> v;
        int nextId = 0;
        for (const auto& s : seeds()) {
            BillDef b;
            b.id = static_cast<u8>(nextId++);
            b.idName = s.idName;
            b.nameZh = s.zh;
            b.desc = s.desc;
            b.category = s.cat;
            b.baseStance = s.base;
            b.effects = s.eff;
            b.effectCount = s.n;
            b.politicalCost = s.cost;
            b.unrestDelta = s.unrest;
            b.overrideThreshold = VoteThreshold::Count;
            b.radical = s.radical;
            v.push_back(b);
        }
        return v;
    }();
    if (idx < 0 || idx >= static_cast<int>(table.size())) idx = 0;
    return table[static_cast<std::size_t>(idx)];
}

int billIndexByName(std::string_view s) {
    for (int i = 0; i < kBillCount && i < static_cast<int>(seeds().size()); ++i) {
        const BillDef& b = billDef(i);
        if (b.idName == s || b.nameZh == s) return i;
    }
    if (!s.empty() && s[0] >= '0' && s[0] <= '9') {
        i64 n = parseInt(s, -1);
        if (n >= 0 && n < kBillCount) return static_cast<int>(n);
    }
    return -1;
}

std::string_view billCategoryName(BillCategory c) {
    switch (c) {
        case BillCategory::Economic: return "经济";
        case BillCategory::Military: return "军事";
        case BillCategory::Social: return "社会";
        case BillCategory::Political: return "政治";
        case BillCategory::Diplomatic: return "外交";
        case BillCategory::Count: break;
    }
    return "?";
}

std::string_view voteThresholdName(VoteThreshold t) {
    switch (t) {
        case VoteThreshold::SimpleMajority: return "简单多数（>50%）";
        case VoteThreshold::AbsoluteMajority: return "绝对多数（≥60%）";
        case VoteThreshold::SuperMajority: return "特别多数（≥2/3）";
        case VoteThreshold::Count: break;
    }
    return "?";
}

std::string_view stanceName(Stance s) {
    switch (s) {
        case Stance::Opposed: return "反对";
        case Stance::Undecided: return "未定";
        case Stance::Supportive: return "赞成";
        case Stance::Count: break;
    }
    return "?";
}

std::string billEffectText(const BillDef& b) {
    std::string out;
    for (u8 i = 0; i < b.effectCount && i < b.effects.size(); ++i) {
        if (b.effects[i].kind == ModKind::Count) continue;
        if (!out.empty()) out += "，";
        out += std::string(modKindName(b.effects[i].kind)) + " " +
               fixedStrSigned(b.effects[i].value * Fixed(100), 1) + "%";
    }
    return out.empty() ? "无修正" : out;
}

void parliamentInit(GameState& st, u32 empire) {
    Empire* e = st.empire(empire);
    if (e == nullptr) return;
    Parliament& p = e->parliament;
    // 席位由派系影响力决定，总量固定为 100 席
    Fixed totalInf = Fixed(0);
    for (const auto& f : e->domestic.factions) totalInf += f.influence;
    if (totalInf.rawValue() <= 0) totalInf = Fixed(1);
    int assigned = 0;
    for (std::size_t i = 0; i < e->domestic.factions.size() && i < kF; ++i) {
        const Faction& f = e->domestic.factions[i];
        int seats = static_cast<int>((f.influence / totalInf * Fixed(100)).rawValue() / FIX);
        if (seats < 1) seats = 1;
        p.seats[static_cast<std::size_t>(f.kind)] = seats;
        assigned += seats;
    }
    p.totalSeats = assigned > 0 ? assigned : 1;

    // 门槛由政体决定：民主 → 简单多数；神权/独裁 → 无需议会（但仍保留机制，用绝对多数）；
    // 蜂群/无政府 → 特别多数（共识决策）
    // 门槛由政体决定
    switch (e->government) {
        case 9:   // 蜂群意识
        case 11:  // 无政府
            p.threshold = VoteThreshold::SuperMajority;
            break;
        case 2:   // 寡头制
        case 3:   // 独裁制
        case 5:   // 神权制
        case 8:   // 军事委员会
            p.threshold = VoteThreshold::AbsoluteMajority;
            break;
        default:
            p.threshold = VoteThreshold::SimpleMajority;
            break;
    }
    p.capital = Fixed::pct(50);
}

namespace {

Fixed thresholdRatio(VoteThreshold t) {
    switch (t) {
        case VoteThreshold::SimpleMajority: return Fixed::pct(50);
        case VoteThreshold::AbsoluteMajority: return Fixed::pct(60);
        case VoteThreshold::SuperMajority: return Fixed::raw(667);
        case VoteThreshold::Count: break;
    }
    return Fixed::pct(50);
}

/// 计算某派系对某法案的立场
Stance computeStance(const Empire& e, const Faction& f, const BillDef& b) {
    (void)e;   // 立场只取决于派系本身与法案性质；帝国参数保留以便未来接入政体修正
    i8 base = b.baseStance[static_cast<std::size_t>(f.kind)];
    // 立场强度受满意度与政体调节
    // 满意度高 → 更容易接受现状（保守），满意度低 → 更愿意支持改革
    i64 score = base * 10;
    if (b.radical) {
        // 激进改革：不满者更支持，满意者更反对
        score += (Fixed::pct(50).rawValue() - f.satisfaction.rawValue()) / 100;
    }
    // 影响力大的派系立场更坚定
    score += (f.influence.rawValue() - Fixed::pct(10).rawValue()) / 200;
    if (score >= 6) return Stance::Supportive;
    if (score <= -6) return Stance::Opposed;
    return Stance::Undecided;
}

/// 未定席位的分配：按各派系的满意度倾向**比例**投向赞成/反对。
///
/// 早期实现是「倾向 > 0 才分给赞成，否则全部归反对」，导致倾向接近 0 时
/// 所有未定席位一边倒进反对阵营 —— 任何法案都几乎不可能通过
/// （实测赞成比恒在 0.32~0.36）。真实议会里未定派系应当大致对半分。
void allocateUndecided(const Empire& e, const BillDef& b, int undecided, int& yes, int& no) {
    (void)e;   // 席位与满意度均取自 e.parliament / e.domestic，参数保留以便未来扩展
    if (undecided <= 0) return;
    // 计算加权倾向：每席的赞成概率
    Fixed weightedYes = Fixed(0);
    int totalSeats = 0;
    for (const auto& f : e.domestic.factions) {
        if (computeStance(e, f, b) != Stance::Undecided) continue;
        int seats = e.parliament.seats[static_cast<std::size_t>(f.kind)];
        if (seats <= 0) continue;
        // 基准 50%，满意度偏离 50% 时按方向偏移；
        // 激进改革对不满者更有吸引力，温和法案对满意者更有吸引力。
        Fixed sat = f.satisfaction;
        Fixed deviation = b.radical ? (Fixed::pct(50) - sat) : (sat - Fixed::pct(50));
        Fixed prob = Fixed::pct(50) + deviation;          // 0% ~ 100%
        prob = fxClamp(prob, Fixed::pct(15), Fixed::pct(85));
        weightedYes += prob * Fixed(seats);
        totalSeats += seats;
    }
    if (totalSeats <= 0) {
        // 没有可识别的未定派系：按半数处理（弃权视为中立）
        yes += undecided / 2;
        no += undecided - undecided / 2;
        return;
    }
    Fixed avgProb = weightedYes / Fixed(totalSeats);
    int yesSeats = static_cast<int>((avgProb * Fixed(undecided)).rawValue() / FIX);
    if (yesSeats < 0) yesSeats = 0;
    if (yesSeats > undecided) yesSeats = undecided;
    yes += yesSeats;
    no += undecided - yesSeats;
}

}  // namespace

bool billPropose(GameState& st, u32 empire, u16 billId, std::string* err) {
    Empire* e = st.empire(empire);
    if (e == nullptr) {
        if (err) *err = "非法主体";
        return false;
    }
    if (billId >= kBillCount) {
        if (err) *err = "非法法案编号";
        return false;
    }
    if (e->parliament.session.active) {
        if (err) *err = "议会正在审议另一项法案（可先投票或撤回）";
        return false;
    }
    if (std::find(e->parliament.passed.begin(), e->parliament.passed.end(), billId) !=
        e->parliament.passed.end()) {
        if (err) *err = "该法案已经通过";
        return false;
    }
    const BillDef& b = billDef(billId);
    BillSession& s = e->parliament.session;
    s.active = true;
    s.billId = billId;
    s.startTick = st.tick;
    s.rounds = 0;
    s.resolved = false;
    s.passed = false;
    s.lastResult.clear();
    s.thresholdKind = (b.overrideThreshold != VoteThreshold::Count) ? b.overrideThreshold : e->parliament.threshold;
    s.threshold = thresholdRatio(s.thresholdKind);
    // 计算各派系席位与立场
    s.seats.clear();
    for (const auto& f : e->domestic.factions) {
        Seat seat;
        seat.faction = f.kind;
        seat.seats = e->parliament.seats[static_cast<std::size_t>(f.kind)];
        seat.influence = f.influence;
        seat.satisfaction = f.satisfaction;
        seat.stance = computeStance(*e, f, b);
        s.seats.push_back(std::move(seat));
    }
    st.logEvent(LogPhase::Domestic, "bill.proposed",
                e->name + " 议会开始审议【" + std::string(b.nameZh) + "】", empire);
    return true;
}

bool billPersuade(GameState& st, u32 empire, FactionKind fk, std::string* err) {
    Empire* e = st.empire(empire);
    if (e == nullptr) {
        if (err) *err = "非法主体";
        return false;
    }
    BillSession& s = e->parliament.session;
    if (!s.active) {
        if (err) *err = "当前没有正在审议的法案";
        return false;
    }
    // 找到该派系的席位
    Seat* seat = nullptr;
    for (auto& x : s.seats)
        if (x.faction == fk) seat = &x;
    if (seat == nullptr) {
        if (err) *err = "议会中没有该派系";
        return false;
    }
    if (seat->stance == Stance::Supportive) {
        if (err) *err = std::string(factionKindName(fk)) + " 已经支持该法案";
        return false;
    }
    // 拉票代价：政治资本 + 影响力 + 国库
    const BillDef& b = billDef(s.billId);
    Fixed capitalCost = Fixed::pct(8) + (b.radical ? Fixed::pct(6) : Fixed(0));
    if (e->parliament.capital.rawValue() < capitalCost.rawValue()) {
        if (err)
            *err = "政治资本不足：需要 " + fixedStrPlain(capitalCost * Fixed(100), 0) + "%，当前 " +
                   fixedStrPlain(e->parliament.capital * Fixed(100), 0) + "%";
        return false;
    }
    Fixed infCost = Fixed(150);
    i64 creditCost = 12000;
    if (e->influence.rawValue() < infCost.rawValue()) {
        if (err) *err = "影响力不足：需要 " + fixedStr(infCost, 0);
        return false;
    }
    if (e->treasury.rawValue() < Fixed(creditCost).rawValue()) {
        if (err) *err = "国库不足：拉票需要 " + fixedStr(Fixed(creditCost), 0);
        return false;
    }
    e->parliament.capital -= capitalCost;
    e->influence -= infCost;
    e->treasury -= Fixed(creditCost);
    if (empire == kPlayerId) st.market.margin.cash = e->treasury;

    // 提升一格立场：反对 → 未定 → 赞成
    if (seat->stance == Stance::Opposed) {
        seat->stance = Stance::Undecided;
        seat->persuasion = "以让步换取其弃权";
    } else {
        seat->stance = Stance::Supportive;
        seat->persuasion = "以利益交换其支持";
    }
    seat->promised += Fixed::pct(6);
    seat->satisfaction = fxClamp(seat->satisfaction + Fixed::pct(5), Fixed(0), Fixed(1));
    // 同步回派系本身
    for (auto& f : e->domestic.factions)
        if (f.kind == fk) f.satisfaction = seat->satisfaction;

    st.logEvent(LogPhase::Domestic, "bill.persuade",
                e->name + " 拉拢【" + std::string(factionKindName(fk)) + "】支持【" +
                    std::string(billDef(s.billId).nameZh) + "】",
                empire);
    return true;
}

bool billVote(GameState& st, u32 empire, std::string* err) {
    Empire* e = st.empire(empire);
    if (e == nullptr) {
        if (err) *err = "非法主体";
        return false;
    }
    BillSession& s = e->parliament.session;
    if (!s.active) {
        if (err) *err = "当前没有正在审议的法案";
        return false;
    }
    const BillDef& b = billDef(s.billId);
    ++s.rounds;

    // 计票
    int yes = 0, no = 0, undecided = 0;
    for (const auto& seat : s.seats) {
        switch (seat.stance) {
            case Stance::Supportive: yes += seat.seats; break;
            case Stance::Opposed: no += seat.seats; break;
            default: undecided += seat.seats; break;
        }
    }
    s.yesSeats = yes;
    s.noSeats = no;
    s.undecidedSeats = undecided;
    int decided = yes + no;
    Fixed ratio = (decided > 0) ? Fixed::raw(mulDivSat(yes, FIX, decided)) : Fixed(0);

    // 未定席位按比例分配
    if (undecided > 0) {
        allocateUndecided(*e, b, undecided, yes, no);
        decided = yes + no;
        ratio = (decided > 0) ? Fixed::raw(mulDivSat(yes, FIX, decided)) : Fixed(0);
        // 回写：未定席位已分配，session 票数必须反映最终结果，
        // 否则 UI 与判定会显示不一致的数字。
        s.yesSeats = yes;
        s.noSeats = no;
        s.undecidedSeats = 0;
    }

    bool passed = ratio.rawValue() >= s.threshold.rawValue();
    s.resolved = true;
    s.passed = passed;
    s.active = false;

    if (passed) {
        e->parliament.passed.push_back(s.billId);
        ++e->parliament.legislationCount;
        // 法案效果不复制到 ActiveEffect —— 直接由 passed 列表聚合，
        // 避免两处存储导致重复计数。
        e->parliament.capital = fxClamp(e->parliament.capital + Fixed::pct(10), Fixed(0), Fixed(1));
        e->domestic.unrest = fxClamp(e->domestic.unrest + b.unrestDelta, Fixed(0), Fixed(1));
        // 支持者满意、反对者不满
        for (auto& f : e->domestic.factions) {
            i8 base = b.baseStance[static_cast<std::size_t>(f.kind)];
            if (base > 0) f.satisfaction = fxClamp(f.satisfaction + Fixed::pct(8), Fixed(0), Fixed(1));
            else if (base < 0) f.satisfaction = fxClamp(f.satisfaction - Fixed::pct(10), Fixed(0), Fixed(1));
        }
        s.lastResult = "通过（赞成 " + fixedStrPlain(ratio * Fixed(100), 0) + "% ≥ 门槛 " +
                       fixedStrPlain(s.threshold * Fixed(100), 0) + "%）";
        st.logEvent(LogPhase::Domestic, "bill.passed",
                    e->name + " 议会通过【" + std::string(b.nameZh) + "】→ " + billEffectText(b), empire);
    } else {
        e->parliament.rejected.push_back(s.billId);
        e->parliament.capital = fxClamp(e->parliament.capital - Fixed::pct(10), Fixed(0), Fixed(1));
        e->domestic.unrest = fxClamp(e->domestic.unrest + Fixed::pct(3), Fixed(0), Fixed(1));
        s.lastResult = "否决（赞成 " + fixedStrPlain(ratio * Fixed(100), 0) + "% < 门槛 " +
                       fixedStrPlain(s.threshold * Fixed(100), 0) + "%）";
        st.logEvent(LogPhase::Domestic, "bill.rejected",
                    e->name + " 议会否决【" + std::string(b.nameZh) + "】", empire);
    }
    return passed;
}

bool billForcePass(GameState& st, u32 empire, std::string* err) {
    Empire* e = st.empire(empire);
    if (e == nullptr) {
        if (err) *err = "非法主体";
        return false;
    }
    BillSession& s = e->parliament.session;
    if (!s.active) {
        if (err) *err = "当前没有正在审议的法案";
        return false;
    }
    // 强行通过：不需要票数，但代价极高
    Fixed capitalCost = Fixed::pct(35);
    i64 unrestCost = 0;
    if (e->parliament.capital.rawValue() < capitalCost.rawValue()) {
        if (err)
            *err = "政治资本不足：强行通过需要 " + fixedStrPlain(capitalCost * Fixed(100), 0) + "%";
        return false;
    }
    e->parliament.capital -= capitalCost;
    const BillDef& b = billDef(s.billId);
    s.resolved = true;
    s.passed = true;
    s.active = false;
    e->parliament.passed.push_back(s.billId);
    ++e->parliament.legislationCount;
    // 强行通过：民怨大涨、全体派系不满、合法性下降
    unrestCost = 12;
    e->domestic.unrest = fxClamp(e->domestic.unrest + Fixed::pct(unrestCost), Fixed(0), Fixed(1));
    e->domestic.legitimacy = fxClamp(e->domestic.legitimacy - Fixed::pct(15), Fixed(0), Fixed(1));
    for (auto& f : e->domestic.factions) {
        i8 base = b.baseStance[static_cast<std::size_t>(f.kind)];
        if (base < 0) f.satisfaction = fxClamp(f.satisfaction - Fixed::pct(20), Fixed(0), Fixed(1));
        else f.satisfaction = fxClamp(f.satisfaction - Fixed::pct(5), Fixed(0), Fixed(1));
    }
    s.lastResult = "强行通过（绕开议会：民怨 +12%，合法性 -15%，反对派满意度 -20%）";
    st.logEvent(LogPhase::Domestic, "bill.forced",
                e->name + " 强行通过【" + std::string(b.nameZh) + "】（绕开议会，代价高昂）", empire);
    (void)unrestCost;
    return true;
}

bool billWithdraw(GameState& st, u32 empire, std::string* err) {
    Empire* e = st.empire(empire);
    if (e == nullptr) {
        if (err) *err = "非法主体";
        return false;
    }
    if (!e->parliament.session.active) {
        if (err) *err = "当前没有正在审议的法案";
        return false;
    }
    e->parliament.session.active = false;
    e->parliament.session.lastResult = "已撤回";
    e->parliament.capital = fxClamp(e->parliament.capital - Fixed::pct(5), Fixed(0), Fixed(1));
    st.logEvent(LogPhase::Domestic, "bill.withdrawn", e->name + " 撤回了法案", empire);
    return true;
}

Fixed parliamentModifier(const GameState& st, u32 empire, ModKind kind) {
    const Empire* e = st.empire(empire);
    if (e == nullptr || kind == ModKind::Count) return Fixed(0);
    Fixed acc = Fixed(0);
    for (u16 id : e->parliament.passed) {
        if (id >= kBillCount) continue;
        const BillDef& b = billDef(id);
        for (u8 i = 0; i < b.effectCount && i < b.effects.size(); ++i)
            if (b.effects[i].kind == kind) acc += b.effects[i].value;
    }
    return acc;
}

Fixed parliamentUnrestTarget(const GameState& st, u32 empire) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return Fixed(0);
    Fixed acc = Fixed(0);
    for (u16 id : e->parliament.passed) {
        if (id >= kBillCount) continue;
        acc += billDef(id).unrestDelta;
    }
    return acc;
}

Fixed parliamentLegitimacyTarget(const GameState& /*st*/, u32 /*empire*/) {
    // 法案暂不直接影响合法性目标（保留接口以便扩展）
    return Fixed(0);
}

void parliamentPhase(GameState& st) {
    for (auto& e : st.empires) {
        if (!e.alive) continue;
        // 政治资本每季自然恢复
        e.parliament.capital = fxClamp(e.parliament.capital + Fixed::pct(2), Fixed(0), Fixed(1));
        // 法案带来的民意影响：作用在**均衡值**上（见 parliamentUnrestTarget），
        // 此处不再按季累加 —— 线性累加会让民意在长局中必然饱和。
        for (u16 id : e.parliament.passed) {
            if (id >= kBillCount) continue;
            const BillDef& b = billDef(id);
            // 已通过法案持续影响派系关系
            for (auto& f : e.domestic.factions) {
                i8 base = b.baseStance[static_cast<std::size_t>(f.kind)];
                if (base > 0) f.satisfaction = fxClamp(f.satisfaction + Fixed::bp(5), Fixed(0), Fixed(1));
                else if (base < 0) f.satisfaction = fxClamp(f.satisfaction - Fixed::bp(8), Fixed(0), Fixed(1));
            }
        }
        // 席位随影响力变化而重算
        Fixed totalInf = Fixed(0);
        for (const auto& f : e.domestic.factions) totalInf += f.influence;
        if (totalInf.rawValue() > 0) {
            int assigned = 0;
            for (std::size_t i = 0; i < e.domestic.factions.size() && i < kF; ++i) {
                const Faction& f = e.domestic.factions[i];
                int seats = static_cast<int>((f.influence / totalInf * Fixed(100)).rawValue() / FIX);
                if (seats < 1) seats = 1;
                e.parliament.seats[static_cast<std::size_t>(f.kind)] = seats;
                assigned += seats;
            }
            e.parliament.totalSeats = assigned > 0 ? assigned : 1;
        }
    }
}

std::string parliamentText(const GameState& st, u32 empireId) {
    const Empire* e = st.empire(empireId);
    if (e == nullptr) return "非法主体\n";
    const Parliament& p = e->parliament;
    std::string out;
    out += "  总席位 " + std::to_string(p.totalSeats) + "   通过门槛：" +
           std::string(voteThresholdName(p.threshold)) + "   政治资本 " +
           fixedStrPlain(p.capital * Fixed(100), 0) + "%\n";
    out += "  已通过法案 " + std::to_string(p.passed.size()) + " 项   被否决 " +
           std::to_string(p.rejected.size()) + " 项\n\n";

    out += style("议会席位", Style::Sub) + "\n";
    TextTable t;
    t.header({"派系", "席位", "占比", "影响力", "满意度"}, {Align::Left, Align::Right, Align::Right, Align::Right,
                                                            Align::Right});
    for (int i = 0; i < static_cast<int>(FactionKind::Count); ++i) {
        auto fk = static_cast<FactionKind>(i);
        int seats = p.seats[static_cast<std::size_t>(i)];
        if (seats <= 0) continue;
        const Faction* f = nullptr;
        for (const auto& x : e->domestic.factions)
            if (x.kind == fk) f = &x;
        Fixed share = Fixed::raw(mulDivSat(seats, FIX, p.totalSeats > 0 ? p.totalSeats : 1));
        t.row({std::string(factionKindName(fk)), std::to_string(seats),
               fixedStrPlain(share * Fixed(100), 0) + "%",
               f != nullptr ? fixedStrPlain(f->influence, 2) : "—",
               f != nullptr ? fixedStrPlain(f->satisfaction, 2) : "—"});
    }
    out += t.render();

    if (p.session.active) {
        const BillDef& b = billDef(p.session.billId);
        out += "\n" + style("正在审议：" + std::string(b.nameZh), Style::Warn) + "\n";
        out += "  类别：" + std::string(billCategoryName(b.category)) +
               (b.radical ? style("  【激进改革】", Style::Bad) : "") + "\n";
        out += "  效果：" + billEffectText(b) + "\n";
        out += "  门槛：" + std::string(voteThresholdName(p.session.thresholdKind)) + "\n\n";
        TextTable vt;
        vt.header({"派系", "席位", "立场", "拉票记录"}, {Align::Left, Align::Right, Align::Left, Align::Left});
        for (const auto& s : p.session.seats) {
            std::string st2 = s.stance == Stance::Supportive ? style("赞成", Style::Good)
                              : s.stance == Stance::Opposed  ? style("反对", Style::Bad)
                                                             : "未定";
            vt.row({std::string(factionKindName(s.faction)), std::to_string(s.seats), st2, s.persuasion});
        }
        out += vt.render();
        out += "\n  当前票数：赞成 " + std::to_string(p.session.yesSeats) + " / 反对 " +
               std::to_string(p.session.noSeats) + " / 未定 " + std::to_string(p.session.undecidedSeats) + "\n";
        out += "  操作：greyfall parliament --persuade <派系名>   拉票\n";
        out += "        greyfall parliament --vote                  表决\n";
        out += "        greyfall parliament --force                 强行通过（代价高昂）\n";
        out += "        greyfall parliament --withdraw              撤回\n";
    }
    if (!p.session.lastResult.empty()) {
        out += "\n  上次表决：" + p.session.lastResult + "\n";
    }
    return out;
}

namespace {

/// AI 对法案的偏好分：由伦理、政体与当前处境决定
i64 aiBillPreference(const Empire& e, const BillDef& b) {
    i64 score = 0;
    for (u8 eth : e.ethics) {
        switch (static_cast<EthicAxis>(eth)) {
            case EthicAxis::Militarism:
                if (b.category == BillCategory::Military) score += 30;
                if (b.idName == "defenseBudgetCut" || b.idName == "armsControl") score -= 40;
                break;
            case EthicAxis::Commerce:
                if (b.idName == "freeTradeAct" || b.idName == "centralBank") score += 35;
                if (b.idName == "tariffWall" || b.idName == "tradeEmbargoAct") score -= 35;
                break;
            case EthicAxis::Science:
                if (b.idName == "secularEducation" || b.idName == "technocraticCouncil") score += 35;
                if (b.idName == "censorshipAct" || b.idName == "conscriptionOfScience") score -= 35;
                break;
            case EthicAxis::Faith:
                if (b.idName == "nobilityRestoration" || b.idName == "migrationControl") score += 30;
                if (b.idName == "secularEducation" || b.idName == "religiousToleration") score -= 40;
                break;
            case EthicAxis::Liberty:
                if (b.idName == "universalSuffrage" || b.idName == "secretBallot") score += 35;
                if (b.idName == "emergencyPowers" || b.idName == "censorshipAct") score -= 40;
                break;
            case EthicAxis::Order:
                if (b.idName == "emergencyPowers" || b.idName == "antiCorruption") score += 30;
                if (b.idName == "decentralization") score -= 25;
                break;
            case EthicAxis::Ecology:
                if (b.idName == "healthcareAct" || b.idName == "landReform") score += 25;
                break;
            case EthicAxis::Expansion:
                if (b.category == BillCategory::Military) score += 15;
                break;
            case EthicAxis::Isolation:
                if (b.idName == "isolationAct" || b.idName == "nonIntervention") score += 35;
                if (b.idName == "collectiveSecurity" || b.idName == "foreignAidAct") score -= 30;
                break;
            case EthicAxis::Collectivism:
                if (b.idName == "laborUnions" || b.idName == "publicWorks") score += 25;
                break;
            case EthicAxis::Individualism:
                if (b.idName == "privatization" || b.idName == "freeTradeAct") score += 25;
                break;
            case EthicAxis::Purity:
                if (b.idName == "migrationControl") score += 30;
                break;
            default: break;
        }
    }
    // 处境修正：民怨高则优先安抚类法案
    if (e.domestic.unrest.rawValue() > Fixed::pct(45).rawValue()) {
        if (b.unrestDelta.rawValue() < 0) score += 40;
        if (b.unrestDelta.rawValue() > 0) score -= 30;
    }
    // 国库空虚时反对高成本法案
    if (e.treasury.rawValue() < Fixed(20000).rawValue() && b.politicalCost > 350) score -= 20;
    return score;
}

}  // namespace

VoteEstimate estimateVotes(const GameState& st, u32 empire, u16 billId) {
    VoteEstimate est;
    const Empire* e = st.empire(empire);
    if (e == nullptr || billId >= kBillCount) return est;
    const BillDef& b = billDef(billId);
    for (const auto& f : e->domestic.factions) {
        int seats = e->parliament.seats[static_cast<std::size_t>(f.kind)];
        switch (computeStance(*e, f, b)) {
            case Stance::Supportive: est.yes += seats; break;
            case Stance::Opposed: est.no += seats; break;
            default: est.undecided += seats; break;
        }
    }
    // 未定席位按比例分配（与正式表决共用同一套规则）
    if (est.undecided > 0) {
        allocateUndecided(*e, b, est.undecided, est.yes, est.no);
        est.undecided = 0;
    }
    int decided = est.yes + est.no;
    est.ratio = (decided > 0) ? Fixed::raw(mulDivSat(est.yes, FIX, decided)) : Fixed(0);
    VoteThreshold th = (b.overrideThreshold != VoteThreshold::Count) ? b.overrideThreshold
                                                                    : e->parliament.threshold;
    est.wouldPass = est.ratio.rawValue() >= thresholdRatio(th).rawValue();
    return est;
}

void parliamentAiPhase(GameState& st) {
    for (auto& e : st.empires) {
        if (e.isPlayer || !e.alive) continue;
        // 政治资本充裕才考虑立法
        if (e.parliament.capital.rawValue() < Fixed::pct(60).rawValue()) continue;
        // 错开各帝国的提案节奏，避免每季齐刷刷提案
        if ((st.tick + e.id) % 6 != 0) continue;

        // 正在审议：先拉票再表决。
        // 优先拉「最容易翻转」的派系：未定优先（一次拉票即支持），
        // 其次是小党（席位少、代价相同但收益/代价比更高）。
        if (e.parliament.session.active) {
            for (int k = 0; k < 4; ++k) {
                const Seat* pick = nullptr;
                for (const auto& seat : e.parliament.session.seats) {
                    if (seat.stance == Stance::Supportive) continue;
                    if (pick == nullptr) {
                        pick = &seat;
                        continue;
                    }
                    int rankA = (seat.stance == Stance::Undecided) ? 0 : 1;
                    int rankB = (pick->stance == Stance::Undecided) ? 0 : 1;
                    if (rankA < rankB || (rankA == rankB && seat.seats < pick->seats)) pick = &seat;
                }
                if (pick == nullptr) break;
                if (!billPersuade(st, e.id, pick->faction, nullptr)) break;
            }
            (void)billVote(st, e.id, nullptr);
            continue;
        }

        // 选出偏好最高且**有可能通过**的法案。
        // 早期实现只按偏好选案、且不排除已被否决的项，导致 AI 反复提交
        // 同一项必败法案（实测 120 季内 10 次否决、0 次通过）。
        int best = -1;
        i64 bestScore = 0;
        for (int i = 0; i < kBillCount; ++i) {
            u16 bid = static_cast<u16>(i);
            if (std::find(e.parliament.passed.begin(), e.parliament.passed.end(), bid) !=
                e.parliament.passed.end())
                continue;
            // 已被否决过的不再重复提交
            if (std::find(e.parliament.rejected.begin(), e.parliament.rejected.end(), bid) !=
                e.parliament.rejected.end())
                continue;
            VoteEstimate est = estimateVotes(st, e.id, bid);
            // 允许「靠拉票可补足」的法案：门槛内差距不超过约 30 个百分点
            VoteThreshold th = (billDef(i).overrideThreshold != VoteThreshold::Count)
                                   ? billDef(i).overrideThreshold
                                   : e.parliament.threshold;
            Fixed thRatio = thresholdRatio(th);
            bool reachable = est.ratio.rawValue() + Fixed::pct(30).rawValue() >= thRatio.rawValue();
            if (!reachable) continue;
            i64 sc = aiBillPreference(e, billDef(i));
            // 能直接通过的优先；越接近门槛越优先
            if (est.wouldPass) sc += 30;
            sc += (est.ratio.rawValue() / 100);
            if (sc > bestScore) {
                bestScore = sc;
                best = i;
            }
        }
        if (best < 0 || bestScore < 20) continue;
        (void)billPropose(st, e.id, static_cast<u16>(best), nullptr);
    }
}

std::string billDetailText(const GameState& st, u32 empireId, u16 billId) {
    if (billId >= kBillCount) return "非法法案\n";
    const BillDef& b = billDef(billId);
    const Empire* e = st.empire(empireId);
    std::string out;
    out += style("【" + std::string(b.nameZh) + "】", Style::Heading) + "  " +
           std::string(billCategoryName(b.category)) + "\n";
    out += wrapJoin(b.desc, 86, "  ") + "\n\n";
    out += "  效果：" + billEffectText(b) + "\n";
    out += "  政治成本：" + groupDigits(b.politicalCost) + "（通过后扣减政治资本）\n";
    if (b.unrestDelta.rawValue() != 0)
        out += "  民意影响：民怨 " + fixedStrSigned(b.unrestDelta * Fixed(100), 1) + "%\n";
    if (b.radical) out += style("  激进改革：保守派强烈反对，但不满者更愿支持\n", Style::Warn);
    out += "\n  各派系天然立场：\n";
    for (int i = 0; i < static_cast<int>(FactionKind::Count); ++i) {
        i8 base = b.baseStance[static_cast<std::size_t>(i)];
        if (base == 0) continue;
        std::string s = base > 0 ? style("赞成", Style::Good) : style("反对", Style::Bad);
        out += "    " + padRight(std::string(factionKindName(static_cast<FactionKind>(i))), 10) + s +
               "（强度 " + std::to_string(static_cast<int>(base)) + "）\n";
    }
    if (e != nullptr) {
        bool done = std::find(e->parliament.passed.begin(), e->parliament.passed.end(), billId) !=
                    e->parliament.passed.end();
        bool rejected = std::find(e->parliament.rejected.begin(), e->parliament.rejected.end(), billId) !=
                        e->parliament.rejected.end();
        out += std::string("\n  状态：") + (done ? style("已通过", Style::Good)
                                          : rejected ? style("曾被否决", Style::Warn)
                                                     : "未审议") +
               "\n";
    }
    return out;
}

}  // namespace gf
