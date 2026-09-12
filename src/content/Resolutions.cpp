#include "domain/Resolution.h"

#include <algorithm>

#include "core/GameState.h"
#include "util/Str.h"

namespace gf {

namespace {
/// 一项改革的持续季数。
/// 4 季过短 —— 改革是国家的重大转向，应当跨越数个时代事件才见效。
/// 24 季 ≈ 一局（200~400 季）的 1/10，配合「同时只能推行一项」，
/// 使「先改什么、什么时候改」成为真正的战略决策。
constexpr int kReformTicks = 24;
}  // namespace

std::string_view resolutionKindName(ResolutionKind k) {
    switch (k) {
        case ResolutionKind::Active: return "主动决议";
        case ResolutionKind::Auto: return "自动触发";
        case ResolutionKind::Preventable: return "可阻止";
        case ResolutionKind::Countdown: return "倒计时";
        case ResolutionKind::Tradeoff: return "牺牲换利";
        case ResolutionKind::Count: break;
    }
    return "?";
}

std::string_view resTargetName(ResTarget t) {
    switch (t) {
        case ResTarget::ResResearch: return "研究速率";
        case ResTarget::ResBuild: return "建造速率";
        case ResTarget::ResTrade: return "贸易毛利";
        case ResTarget::ResGrowth: return "人口增长";
        case ResTarget::ResMilitary: return "军事力量";
        case ResTarget::ResStability: return "稳定度";
        case ResTarget::ResUnrest: return "民怨";
        case ResTarget::ResDiplo: return "外交权重";
        case ResTarget::ResCredit: return "信用评级";
        case ResTarget::ResDetection: return "侦测";
        case ResTarget::ResColony: return "殖民成本";
        case ResTarget::ResManip: return "操纵技巧";
        case ResTarget::Treasury: return "国库";
        case ResTarget::Influence: return "影响力";
        case ResTarget::Unity: return "凝聚力";
        case ResTarget::Military: return "军力";
        case ResTarget::Economy: return "经济";
        case ResTarget::Capacity: return "产能";
        case ResTarget::FleetPower: return "舰队战力";
        case ResTarget::Count: break;
    }
    return "?";
}

ModKind resTargetToMod(ResTarget t) {
    switch (t) {
        case ResTarget::ResResearch: return ModKind::ResearchRate;
        case ResTarget::ResBuild: return ModKind::BuildRate;
        case ResTarget::ResTrade: return ModKind::TradeMargin;
        case ResTarget::ResGrowth: return ModKind::Growth;
        case ResTarget::ResMilitary: return ModKind::MilitaryPower;
        case ResTarget::ResStability: return ModKind::Stability;
        case ResTarget::ResUnrest: return ModKind::Unrest;
        case ResTarget::ResDiplo: return ModKind::DiploWeight;
        case ResTarget::ResCredit: return ModKind::CreditRating;
        case ResTarget::ResDetection: return ModKind::Detection;
        case ResTarget::ResColony: return ModKind::ColonyCost;
        case ResTarget::ResManip: return ModKind::ManipulationSkill;
        default: return ModKind::Count;
    }
}

bool resEffectIsPositive(ResTarget t, Fixed value) {
    // 民怨、殖民成本、建造速率等目标的方向与其他相反
    bool inverted = (t == ResTarget::ResUnrest || t == ResTarget::ResColony);
    if (inverted) return value.rawValue() < 0;
    return value.rawValue() > 0;
}

std::string resEffectText(const ResEffect& ef, bool withSign) {
    std::string v = withSign ? fixedStrSigned(ef.value, 3) : fixedStrPlain(ef.value, 3);
    switch (ef.target) {
        case ResTarget::Treasury:
            return std::string(resTargetName(ef.target)) + " " + fixedStrSigned(ef.value, 0) + " cr";
        case ResTarget::Influence:
        case ResTarget::Unity:
        case ResTarget::Military:
        case ResTarget::Economy:
        case ResTarget::Capacity:
        case ResTarget::FleetPower:
            return std::string(resTargetName(ef.target)) + " " + fixedStrSigned(ef.value, 0) + "（一次性）";
        default:
            // 修正类：value 为比例，显示为百分比
            return std::string(resTargetName(ef.target)) + " " + fixedStrSigned(ef.value * Fixed(100), 1) + "%";
    }
}

namespace {

std::string joinCond(std::vector<std::string> parts) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) out += " 且 ";
        out += parts[i];
    }
    return out.empty() ? std::string("无条件") : out;
}

}  // namespace

std::string resConditionText(const ResCondition& c) {
    std::vector<std::string> p;
    if (c.minTick > 0) p.push_back("回合 ≥ " + std::to_string(c.minTick));
    if (c.requireTech >= 0) p.push_back("已完成科技 " + std::to_string(c.requireTech));
    if (c.forbidTech >= 0) p.push_back("未完成科技 " + std::to_string(c.forbidTech));
    if (c.unrestAbove.rawValue() > 0)
        p.push_back("民怨 ≥ " + fixedStrPlain(c.unrestAbove * Fixed(100), 0) + "%");
    if (c.stabilityBelow.rawValue() > 0)
        p.push_back("稳定度 ≤ " + fixedStrPlain(c.stabilityBelow * Fixed(100), 0) + "%");
    if (c.legitimacyBelow.rawValue() > 0)
        p.push_back("合法性 ≤ " + fixedStrPlain(c.legitimacyBelow * Fixed(100), 0) + "%");
    if (c.treasuryBelow.rawValue() > 0)
        p.push_back("国库 ≤ " + fixedStr(c.treasuryBelow, 0));
    if (c.maxSystems > 0) p.push_back("星系数 ≤ " + std::to_string(c.maxSystems));
    if (c.minSystems > 0) p.push_back("星系数 ≥ " + std::to_string(c.minSystems));
    if (c.atWarWithAny) p.push_back("处于战争状态");
    if (c.stockBelowCommodity >= 0)
        p.push_back(std::string(commodityName(c.stockBelowCommodity)) + " 库存 ≤ " +
                    std::to_string(c.stockBelowQty));
    if (c.treasuryAbove.rawValue() > 0) p.push_back("国库 ≥ " + fixedStr(c.treasuryAbove, 0));
    if (c.unrestBelow.rawValue() > 0)
        p.push_back("民怨 ≤ " + fixedStrPlain(c.unrestBelow * Fixed(100), 0) + "%");
    if (c.stabilityAbove.rawValue() > 0)
        p.push_back("稳定度 ≥ " + fixedStrPlain(c.stabilityAbove * Fixed(100), 0) + "%");
    if (c.stockAboveCommodity >= 0)
        p.push_back(std::string(commodityName(c.stockAboveCommodity)) + " 库存 ≥ " +
                    std::to_string(c.stockAboveQty));
    return joinCond(std::move(p));
}

bool resConditionMet(const GameState& st, const Empire& e, const ResCondition& c) {
    if (c.minTick > 0 && static_cast<i64>(st.tick) < c.minTick) return false;
    if (c.requireTech >= 0 && !techCompleted(e.tech, c.requireTech)) return false;
    if (c.forbidTech >= 0 && techCompleted(e.tech, c.forbidTech)) return false;
    if (c.unrestAbove.rawValue() != 0 && e.domestic.unrest.rawValue() < c.unrestAbove.rawValue()) return false;
    if (c.stabilityBelow.rawValue() != 0 && e.stability.rawValue() > c.stabilityBelow.rawValue()) return false;
    if (c.legitimacyBelow.rawValue() != 0 &&
        e.domestic.legitimacy.rawValue() > c.legitimacyBelow.rawValue())
        return false;
    if (c.treasuryBelow.rawValue() != 0 && e.treasury.rawValue() > c.treasuryBelow.rawValue()) return false;
    if (c.maxSystems > 0 && static_cast<i64>(e.systems.size()) > c.maxSystems) return false;
    if (c.minSystems > 0 && static_cast<i64>(e.systems.size()) < c.minSystems) return false;
    if (c.atWarWithAny) {
        bool atWar = false;
        for (std::size_t i = 0; i < st.relations.size(); ++i) {
            const Relation& r = st.relations[i];
            if (!r.atWar) continue;
            if (i / kMaxEmpires == e.id) atWar = true;
        }
        if (!atWar) return false;
    }
    if (c.stockBelowCommodity >= 0) {
        Fixed have = e.stock[static_cast<std::size_t>(c.stockBelowCommodity)];
        if (have.rawValue() > Fixed(c.stockBelowQty).rawValue()) return false;
    }
    // 良性状态条件
    if (c.treasuryAbove.rawValue() != 0 && e.treasury.rawValue() < c.treasuryAbove.rawValue()) return false;
    if (c.unrestBelow.rawValue() != 0 && e.domestic.unrest.rawValue() > c.unrestBelow.rawValue()) return false;
    if (c.stabilityAbove.rawValue() != 0 && e.stability.rawValue() < c.stabilityAbove.rawValue()) return false;
    if (c.stockAboveCommodity >= 0) {
        Fixed have = e.stock[static_cast<std::size_t>(c.stockAboveCommodity)];
        if (have.rawValue() < Fixed(c.stockAboveQty).rawValue()) return false;
    }
    return true;
}

namespace {

struct Builder {
    std::vector<ResolutionDef> v;
    int n = 0;

    static ResEffect E(ResTarget t, Fixed v) {
        ResEffect e;
        e.target = t;
        e.value = v;
        return e;
    }
    static ResEffect none() { return ResEffect{ResTarget::Count, Fixed(0)}; }

    void add(std::string_view idName, std::string_view zh, std::string_view desc, ResolutionKind kind,
             ResCondition trig, ResCondition prev, ResCondition req, ResCost cost, ResEffect onAct,
             ResEffect onTick, int duration, int cd, ResEffect onDone, ResEffect onFail) {
        ResolutionDef d;
        d.id = static_cast<u8>(n);
        d.idName = idName;
        d.nameZh = zh;
        d.desc = desc;
        d.kind = kind;
        d.trigger = trig;
        d.prevent = prev;
        d.require = req;
        d.cost = cost;
        d.onActivate = onAct;
        d.onTick = onTick;
        d.duration = duration;
        d.countdownTicks = cd;
        d.onComplete = onDone;
        d.onFail = onFail;
        v.push_back(d);
        ++n;
    }
};

std::vector<ResolutionDef> build() {
    Builder b;
    ResCondition none;
    ResCost freeCost;

    // ================= 主动决议（花代价换收益） =================
    {
        ResCost c;
        c.credits = 12000;
        c.ap = 2;
        b.add("warBonds", "战时公债", "发行爱国债券：立刻获得资金，但未来数季要偿还并付息。",
              ResolutionKind::Active, none, none, none, c,
              Builder::E(ResTarget::Treasury, Fixed(25000)), Builder::E(ResTarget::ResTrade, Fixed::pct(-2)), 24, 0,
              Builder::none(), Builder::none());
    }
    {
        ResCost c;
        c.credits = 20000;
        c.ap = 2;
        b.add("researchGrant", "科研拨款", "向学界注资，短期研究速率大幅提升，但国库吃紧。",
              ResolutionKind::Active, none, none, none, c,
              Builder::E(ResTarget::ResResearch, Fixed::pct(25)), Builder::E(ResTarget::ResResearch, Fixed::pct(15)), 24,
              0, Builder::none(), Builder::none());
    }
    {
        ResCost c;
        c.influence = 300;
        c.ap = 1;
        b.add("civicFestival", "公民庆典", "举办盛大庆典安抚民心：民怨下降，合法性提升。",
              ResolutionKind::Active, none, none, none, c,
              Builder::E(ResTarget::ResUnrest, Fixed::pct(-12)), Builder::E(ResTarget::ResUnrest, Fixed::pct(-4)), 24,
              0, Builder::none(), Builder::none());
    }
    {
        ResCost c;
        c.credits = 30000;
        c.influence = 500;
        c.ap = 3;
        b.add("grandFleetReview", "大阅兵", "集结舰队示威：军力与外交权重同时提升。",
              ResolutionKind::Active, none, none, none, c,
              Builder::E(ResTarget::Military, Fixed(600)), Builder::E(ResTarget::ResMilitary, Fixed::pct(10)), 24, 0,
              Builder::none(), Builder::none());
    }
    {
        ResCost c;
        c.credits = 15000;
        c.ap = 2;
        c.commodity = static_cast<i16>(Commodity::Medicines);
        c.qty = 800;
        b.add("publicHealth", "公共卫生运动", "投入药品改善民生：人口增长与稳定度提升。",
              ResolutionKind::Active, none, none, none, c,
              Builder::E(ResTarget::ResGrowth, Fixed::pct(20)), Builder::E(ResTarget::ResGrowth, Fixed::pct(10)), 24,
              0, Builder::none(), Builder::none());
    }
    {
        ResCost c;
        c.credits = 40000;
        c.ap = 2;
        b.add("marketIntervention", "平准基金", "动用国库平抑物价：贸易毛利提升，但需持续投入。",
              ResolutionKind::Active, none, none, none, c,
              Builder::E(ResTarget::ResTrade, Fixed::pct(15)), Builder::E(ResTarget::Treasury, Fixed(-4000)), 24, 0,
              Builder::none(), Builder::none());
    }
    {
        ResCost c;
        c.unity = 400;
        c.ap = 2;
        b.add("nationalMyth", "国族叙事", "塑造共同的起源故事：凝聚力与合法性提升。",
              ResolutionKind::Active, none, none, none, c,
              Builder::E(ResTarget::Influence, Fixed(200)), Builder::E(ResTarget::ResStability, Fixed::pct(6)), 24, 0,
              Builder::none(), Builder::none());
    }
    {
        ResCost c;
        c.credits = 25000;
        c.ap = 2;
        b.add("fortifyBorders", "边境筑垒", "在边境星系构筑工事：稳定度与外敌威慑提升。",
              ResolutionKind::Active, none, none, none, c,
              Builder::E(ResTarget::ResMilitary, Fixed::pct(12)), Builder::E(ResTarget::ResStability, Fixed::pct(5)),
              24, 0, Builder::none(), Builder::none());
    }

    // ================= 自动触发（条件满足即生效） =================
    {
        ResCondition t;
        t.unrestAbove = Fixed::pct(70);
        b.add("breadRiots", "面包暴动", "民怨过高，城市出现暴动：国库被洗劫、稳定度骤降。",
              ResolutionKind::Auto, t, none, none, freeCost,
              Builder::E(ResTarget::Treasury, Fixed(-8000)), Builder::E(ResTarget::ResStability, Fixed::pct(-10)), 24,
              0, Builder::none(), Builder::none());
    }
    {
        ResCondition t;
        t.treasuryBelow = Fixed(-20000);
        b.add("defaultSpiral", "债务螺旋", "国库深度赤字，信用崩塌：借贷成本与外交权重同时恶化。",
              ResolutionKind::Auto, t, none, none, freeCost,
              Builder::E(ResTarget::ResCredit, Fixed::pct(-15)), Builder::E(ResTarget::ResDiplo, Fixed::pct(-10)), 24,
              0, Builder::none(), Builder::none());
    }
    {
        ResCondition t;
        t.stockBelowCommodity = static_cast<i16>(Commodity::Food);
        t.stockBelowQty = 200;
        b.add("famine", "饥荒", "粮食库存见底，人口开始流失。",
              ResolutionKind::Auto, t, none, none, freeCost,
              Builder::E(ResTarget::ResGrowth, Fixed::pct(-30)), Builder::E(ResTarget::ResUnrest, Fixed::pct(8)), 24,
              0, Builder::none(), Builder::none());
    }
    {
        ResCondition t;
        t.atWarWithAny = 1;
        t.minTick = 20;
        b.add("warWeariness", "厌战情绪", "长期战争引发厌战：民怨上升，军力补充放缓。",
              ResolutionKind::Auto, t, none, none, freeCost,
              Builder::E(ResTarget::ResUnrest, Fixed::pct(6)), Builder::E(ResTarget::ResMilitary, Fixed::pct(-8)), 24,
              0, Builder::none(), Builder::none());
    }
    {
        ResCondition t;
        t.minSystems = 10;
        b.add("overextension", "过度扩张", "疆域过大而治理能力不足：建造与研究效率下降。",
              ResolutionKind::Auto, t, none, none, freeCost,
              Builder::E(ResTarget::ResBuild, Fixed::pct(-12)), Builder::E(ResTarget::ResResearch, Fixed::pct(-10)),
              24, 0, Builder::none(), Builder::none());
    }
    {
        ResCondition t;
        t.minTick = 30;
        b.add("techBoom", "技术红利", "长年积累的研发投入进入收获期：研究速率提升。",
              ResolutionKind::Auto, t, none, none, freeCost,
              Builder::E(ResTarget::ResResearch, Fixed::pct(15)), Builder::E(ResTarget::ResResearch, Fixed::pct(5)), 24,
              0, Builder::none(), Builder::none());
    }

    // ================= 可阻止（条件不满足则触发减益） =================
    {
        ResCondition t;
        t.minTick = 12;
        ResCondition prev;
        prev.treasuryAbove = Fixed(30000);   // 国库 ≥ 3 万即可阻止
        b.add("currencyCrisis", "货币危机", "若在 12 季后仍未储备 3 万信用点，将爆发货币危机。",
              ResolutionKind::Preventable, t, prev, none, freeCost,
              Builder::E(ResTarget::ResCredit, Fixed::pct(-20)), Builder::E(ResTarget::ResTrade, Fixed::pct(-15)), 24,
              0, Builder::none(), Builder::none());
    }
    {
        ResCondition t;
        t.minTick = 16;
        ResCondition prev;
        prev.unrestBelow = Fixed::pct(40);   // 民怨 ≤ 40% 即可阻止
        b.add("separatistMovement", "分离主义", "若民怨在 16 季后仍高于 40%，边疆将出现分离运动。",
              ResolutionKind::Preventable, t, prev, none, freeCost,
              Builder::E(ResTarget::ResStability, Fixed::pct(-15)), Builder::E(ResTarget::ResColony, Fixed::pct(25)), 24,
              0, Builder::none(), Builder::none());
    }
    {
        ResCondition t;
        t.minTick = 24;
        ResCondition prev;
        prev.stabilityAbove = Fixed::pct(45);   // 稳定度 ≥ 45% 即可阻止
        b.add("militaryPlot", "军部异动", "若稳定度在 24 季后仍低于 45%，军部将自行其是。",
              ResolutionKind::Preventable, t, prev, none, freeCost,
              Builder::E(ResTarget::ResMilitary, Fixed::pct(-12)), Builder::E(ResTarget::ResStability, Fixed::pct(-8)),
              24, 0, Builder::none(), Builder::none());
    }
    {
        ResCondition t;
        t.minTick = 20;
        ResCondition prev;
        prev.stockAboveCommodity = static_cast<i16>(Commodity::Medicines);
        prev.stockAboveQty = 1500;   // 药品 ≥ 1500 即可阻止
        b.add("plague", "瘟疫", "若药品储备不足 1500，20 季后将爆发瘟疫。",
              ResolutionKind::Preventable, t, prev, none, freeCost,
              Builder::E(ResTarget::ResGrowth, Fixed::pct(-25)), Builder::E(ResTarget::ResUnrest, Fixed::pct(10)), 24,
              0, Builder::none(), Builder::none());
    }

    // ================= 倒计时（期限内完成 → 增益，超时 → 减益） =================
    {
        ResCondition t;
        t.minTick = 10;
        ResCost c;
        c.credits = 6000;
        b.add("infrastructureDrive", "基建攻坚", "启动基建攻坚：须在 8 季内把稳定度提到 65%，成功则长期受益。",
              ResolutionKind::Countdown, t, none, none, c,
              Builder::E(ResTarget::ResBuild, Fixed::pct(5)), Builder::none(), 0, 8,
              Builder::E(ResTarget::ResStability, Fixed::pct(65)), Builder::E(ResTarget::Treasury, Fixed(-12000)));
    }
    {
        ResCondition t;
        t.minTick = 14;
        ResCost c;
        c.credits = 10000;
        b.add("armamentProgram", "扩军计划", "启动扩军：须在 8 季内把军力提到 1500，成功则军威大振。",
              ResolutionKind::Countdown, t, none, none, c,
              Builder::E(ResTarget::Military, Fixed(200)), Builder::none(), 0, 8,
              Builder::E(ResTarget::Military, Fixed(1500)), Builder::E(ResTarget::ResMilitary, Fixed::pct(-12)));
    }
    {
        ResCondition t;
        t.minTick = 18;
        t.unrestAbove = Fixed::pct(45);
        ResCost c;
        c.influence = 400;
        b.add("reconciliation", "民族和解", "推动和解进程：须在 10 季内把民怨压到 25% 以下，成功则长治久安。",
              ResolutionKind::Countdown, t, none, none, c,
              Builder::E(ResTarget::ResUnrest, Fixed::pct(-6)), Builder::none(), 0, 10,
              Builder::E(ResTarget::ResUnrest, Fixed::pct(25)), Builder::E(ResTarget::ResStability, Fixed::pct(-18)));
    }
    {
        ResCondition t;
        t.minTick = 22;
        ResCost c;
        c.credits = 15000;
        b.add("tradeSummit", "贸易峰会", "筹办峰会：须在 8 季内把贸易毛利修正提到 +10%，成功则打开长期商路。",
              ResolutionKind::Countdown, t, none, none, c,
              Builder::E(ResTarget::ResTrade, Fixed::pct(4)), Builder::none(), 0, 8,
              Builder::E(ResTarget::ResTrade, Fixed::pct(10)), Builder::E(ResTarget::ResTrade, Fixed::pct(-15)));
    }

    // ================= 牺牲换利（永久代价） =================
    {
        ResCost c;
        c.credits = 20000;
        c.ap = 2;
        c.sacrificeTarget = ResTarget::ResGrowth;
        c.sacrificeValue = Fixed::pct(-15);
        b.add("laborConscription", "劳动征召", "强制征召劳动力：建造速率永久提升，但人口增长永久受损。",
              ResolutionKind::Tradeoff, none, none, none, c,
              Builder::E(ResTarget::ResBuild, Fixed::pct(25)), Builder::none(), 0, 0, Builder::none(),
              Builder::none());
    }
    {
        ResCost c;
        c.influence = 600;
        c.ap = 2;
        c.sacrificeTarget = ResTarget::ResTrade;
        c.sacrificeValue = Fixed::pct(-18);
        b.add("warEconomy", "战时经济", "全面转入战时体制：军力永久提升，贸易毛利永久下降。",
              ResolutionKind::Tradeoff, none, none, none, c,
              Builder::E(ResTarget::ResMilitary, Fixed::pct(30)), Builder::none(), 0, 0, Builder::none(),
              Builder::none());
    }
    {
        ResCost c;
        c.unity = 800;
        c.ap = 3;
        c.sacrificeTarget = ResTarget::ResDiplo;
        c.sacrificeValue = Fixed::pct(-25);
        b.add("isolationDoctrine", "孤立主义", "关闭边境、退出外交：稳定度与研究速率永久提升，外交权重永久下降。",
              ResolutionKind::Tradeoff, none, none, none, c,
              Builder::E(ResTarget::ResStability, Fixed::pct(20)),Builder::none(), 0, 0, Builder::none(),
              Builder::none());
    }
    {
        ResCost c;
        c.credits = 35000;
        c.ap = 2;
        c.sacrificeTarget = ResTarget::ResStability;
        c.sacrificeValue = Fixed::pct(-10);
        b.add("austerity", "紧缩财政", "削减福利与公共开支：国库与信用永久改善，稳定度永久受损。",
              ResolutionKind::Tradeoff, none, none, none, c,
              Builder::E(ResTarget::ResCredit, Fixed::pct(20)), Builder::none(), 0, 0, Builder::none(),
              Builder::none());
    }
    {
        ResCost c;
        c.ap = 3;
        c.influence = 900;
        c.sacrificeTarget = ResTarget::ResUnrest;
        c.sacrificeValue = Fixed::pct(15);   // 民怨永久上升
        b.add("secretPolice", "秘密警察", "建立秘密警察体系：侦测与操纵永久提升，民怨永久上升。",
              ResolutionKind::Tradeoff, none, none, none, c,
              Builder::E(ResTarget::ResDetection, Fixed::pct(30)), Builder::none(), 0, 0, Builder::none(),
              Builder::none());
    }

    // ================= 补足到 kResolutionCount（程序化生成的通用决议） =================
    const char* genIds[] = {"adminReform", "taxAmnesty", "portExpansion", "academyEndowment",
                            "veteranPensions", "youthService", "ruralDevelopment", "urbanRenewal",
                            "energySubsidy", "miningCharter", "shippingLane", "borderTreaty",
                            "culturalExchange", "archiveProject", "censusReform", "judicialReform"};
    const char* genZh[] = {"行政改革", "税收赦免", "港口扩建", "学院捐赠",
                           "老兵抚恤", "青年服役", "乡村发展", "城市更新",
                           "能源补贴", "采矿特许", "航道开辟", "边境条约",
                           "文化交流", "档案工程", "人口普查", "司法改革"};
    const ResTarget genTarget[] = {ResTarget::ResBuild, ResTarget::Treasury, ResTarget::ResTrade,
                                   ResTarget::ResResearch, ResTarget::ResStability, ResTarget::ResMilitary,
                                   ResTarget::ResGrowth, ResTarget::ResBuild, ResTarget::Capacity,
                                   ResTarget::ResColony, ResTarget::ResTrade, ResTarget::ResDiplo,
                                   ResTarget::Influence, ResTarget::ResResearch, ResTarget::ResUnrest,
                                   ResTarget::ResStability};
    // 生成项的 idName 必须唯一（否则按名字检索会命中错误条目）
    static std::vector<std::string> genIds2;
    static std::vector<std::string> genZh2;
    genIds2.clear();
    genZh2.clear();
    for (int round = 0; static_cast<int>(genIds2.size()) < kResolutionCount; ++round) {
        for (int g = 0; g < 16; ++g) {
            std::string suffix = round == 0 ? "" : ("-" + std::to_string(round + 1));
            genIds2.push_back(std::string(genIds[g]) + suffix);
            genZh2.push_back(std::string(genZh[g]) + suffix);
        }
    }
    for (int i = 0; static_cast<int>(b.v.size()) < kResolutionCount; ++i) {
        int g = i % 16;
        ResCost c;
        // 定价必须与 AI 的**实际可支付额度**匹配。
        // 实测 AI 国库长期在 1,300~2,500（净收入 360~1,290/季，几乎每季花光），
        // 而原定价 2,500~11,500 让 AI「缺钱」的候选决议长期有 20~31 项、
        // 生效决议停在 2~3 项（互斥组上限为 6）—— 决议系统对 AI 近乎不可用。
        // 重标定到 800~3,200：AI 攒 1~3 季即可发动，玩家也更易触及。
        c.credits = 800 + g * 150;
        c.ap = 1 + (g % 2);
        ResCondition req;
        req.minTick = 4 + g * 2;
        b.add(genIds2[static_cast<std::size_t>(i)], genZh2[static_cast<std::size_t>(i)],
              "常规行政决议：投入资源换取持续收益。", ResolutionKind::Active, none, none, req, c,
              Builder::E(genTarget[g], Fixed::pct(8)), Builder::E(genTarget[g], Fixed::pct(4)), 24, 0, Builder::none(),
              Builder::none());
    }
    // ================= 互斥组与依赖链 =================
    // 统一用「按 idName 查表」的后置方式分配，而不是改动 30 多处 add() 调用：
    // 关系定义集中在一处，便于阅读与调整。
    {
        auto find = [&](std::string_view name) -> int {
            for (std::size_t i = 0; i < b.v.size(); ++i)
                if (b.v[i].idName == name) return static_cast<int>(i);
            return -1;
        };
        auto setGroup = [&](std::string_view name, u8 group) {
            int i = find(name);
            if (i >= 0) b.v[static_cast<std::size_t>(i)].exclusionGroup = group;
        };
        auto setReq = [&](std::string_view name, std::string_view req) {
            int i = find(name), r = find(req);
            if (i >= 0 && r >= 0 && b.v[static_cast<std::size_t>(i)].requireCount < 2) {
                auto& d = b.v[static_cast<std::size_t>(i)];
                d.requiresRes[d.requireCount++] = static_cast<u8>(r);
            }
        };
        auto setBlocks = [&](std::string_view name, std::string_view target) {
            int i = find(name), t = find(target);
            if (i >= 0 && t >= 0 && b.v[static_cast<std::size_t>(i)].blockCount < 2) {
                auto& d = b.v[static_cast<std::size_t>(i)];
                d.blocksRes[d.blockCount++] = static_cast<u8>(t);
            }
        };

        // --- 互斥组 1：经济路线（战时统制 / 紧缩 / 市场平准 三者互斥）---
        setGroup("warEconomy", 1);
        setGroup("austerity", 1);
        setGroup("marketIntervention", 1);
        setGroup("researchGrant", 1);
        // --- 互斥组 2：社会控制手段 ---
        setGroup("secretPolice", 2);
        setGroup("civicFestival", 2);
        setGroup("publicHealth", 2);
        setGroup("nationalMyth", 2);
        // --- 互斥组 3：对外姿态 ---
        setGroup("isolationDoctrine", 3);
        setGroup("grandFleetReview", 3);
        setGroup("fortifyBorders", 3);
        // --- 互斥组 4：劳动与人力政策 ---
        setGroup("laborConscription", 4);
        setGroup("youthService", 4);
        setGroup("veteranPensions", 4);
        // --- 互斥组 5：知识投入方向 ---
        setGroup("academyEndowment", 5);
        setGroup("archiveProject", 5);
        setGroup("culturalExchange", 5);
        // --- 互斥组 6：财政手段 ---
        setGroup("warBonds", 6);
        setGroup("taxAmnesty", 6);

        // --- 依赖链：必须先完成前置决议 ---
        setReq("warEconomy", "warBonds");            // 先发债，再转战时经济
        setReq("laborConscription", "warEconomy");   // 战时经济后才谈得上劳动征召
        setReq("nationalMyth", "civicFestival");     // 先有共同庆典，才谈国族叙事
        setReq("grandFleetReview", "fortifyBorders");// 先筑垒，再阅兵
        setReq("isolationDoctrine", "fortifyBorders");// 先固边，再孤立
        setReq("secretPolice", "fortifyBorders");    // 先有边境控制，才建秘密警察
        setReq("publicHealth", "urbanRenewal");      // 先有城市更新，才推公共卫生
        setReq("marketIntervention", "shippingLane");// 先有航道，才谈平准

        // --- 阻止关系：本决议生效期间，目标决议无法触发 ---
        // 注意：不要再为「同组互斥」的组合添加阻止边 —— 互斥判定先执行，
        // 那条阻止边永远不可达（属于冗余配置）。
        // 秘密警察与公民庆典已同属「社会控制」组，故此处只声明跨组的阻止。
        setBlocks("secretPolice", "culturalExchange");
        setBlocks("isolationDoctrine", "culturalExchange");
        setBlocks("warEconomy", "publicHealth");     // 战时体制挤压公共卫生
        setBlocks("austerity", "publicHealth");
        setBlocks("austerity", "urbanRenewal");
    }
    return b.v;
}

}  // namespace

const ResolutionDef& resolutionDef(int idx) {
    static const std::vector<ResolutionDef> table = build();
    if (idx < 0 || idx >= static_cast<int>(table.size())) idx = 0;
    return table[static_cast<std::size_t>(idx)];
}

int resolutionIndexByName(std::string_view s) {
    for (int i = 0; i < kResolutionCount; ++i) {
        const ResolutionDef& d = resolutionDef(i);
        if (d.idName == s || d.nameZh == s) return i;
    }
    if (!s.empty() && s[0] >= '0' && s[0] <= '9') {
        i64 n = parseInt(s, -1);
        if (n >= 0 && n < kResolutionCount) return static_cast<int>(n);
    }
    return -1;
}

}  // namespace gf

// ===========================================================================
// 互斥与依赖链判定
// ===========================================================================
namespace gf {

std::string_view resExclusionGroupName(u8 g) {
    switch (g) {
        case 0: return "无";
        case 1: return "经济路线";
        case 2: return "社会控制";
        case 3: return "对外姿态";
        case 4: return "劳动与人力";
        case 5: return "知识投入";
        case 6: return "财政手段";
        default: return "其他";
    }
}

bool resCanActivate(const GameState& st, u32 empire, int defIdx, std::string* reason) {
    const Empire* e = st.empire(empire);
    if (e == nullptr || defIdx < 0 || defIdx >= kResolutionCount) {
        if (reason) *reason = "非法主体或决议";
        return false;
    }
    const ResolutionDef& d = resolutionDef(defIdx);
    const auto& res = e->resolutions;

    // 1) 前置决议必须已经生效过（触发过或已完成）
    for (u8 i = 0; i < d.requireCount && i < d.requiresRes.size(); ++i) {
        u16 req = d.requiresRes[i];
        bool done = std::find(res.triggered.begin(), res.triggered.end(), req) != res.triggered.end() ||
                    std::find(res.completed.begin(), res.completed.end(), req) != res.completed.end();
        if (!done) {
            if (reason)
                *reason = "需要先完成【" + std::string(resolutionDef(req).nameZh) + "】";
            return false;
        }
    }

    // 2) **同时只能推行一项改革**。
    // 改革是国内的一次重大转向，同时推进多项会让「取舍」消失 ——
    // 早期按 6 个互斥组各允许一项，实测 AI 常态同时挂着 2~3 项，
    // 玩家也可以把有利效果全部叠上。现在无论分组如何，全局只有一个槽位。
    // 例外：倒计时型（Countdown）是「正在推进中的议程」，不占改革槽位，
    // 它们占用的是时间与风险，而非政策方向。
    if (d.kind == ResolutionKind::Active || d.kind == ResolutionKind::Tradeoff) {
        for (const auto& a : res.active) {
            if (a.ticksLeft == 0) continue;
            const ResolutionDef& other = resolutionDef(static_cast<int>(a.defId));
            if (other.id == d.id) continue;
            if (other.kind != ResolutionKind::Active && other.kind != ResolutionKind::Tradeoff)
                continue;
            if (reason)
                *reason = "正在推行【" + std::string(other.nameZh) +
                          "】，改革同时只能有一项（需等其结束或倒计时走完）";
            return false;
        }
    }

    // 2b) 互斥组：同组已有生效中的决议则冲突（组内细则，保留以便给出更具体的理由）
    if (d.exclusionGroup != 0) {
        for (const auto& a : res.active) {
            if (a.ticksLeft == 0) continue;
            const ResolutionDef& other = resolutionDef(static_cast<int>(a.defId));
            if (other.exclusionGroup != d.exclusionGroup) continue;
            if (other.id == d.id) continue;
            if (reason)
                *reason = "与生效中的【" + std::string(other.nameZh) + "】互斥（同属「" +
                          std::string(resExclusionGroupName(d.exclusionGroup)) + "」）";
            return false;
        }
    }

    // 3) 被其他生效中的决议阻止
    for (const auto& a : res.active) {
        if (a.ticksLeft == 0) continue;
        const ResolutionDef& other = resolutionDef(static_cast<int>(a.defId));
        for (u8 i = 0; i < other.blockCount && i < other.blocksRes.size(); ++i) {
            if (other.blocksRes[i] != d.id) continue;
            if (reason)
                *reason = "被生效中的【" + std::string(other.nameZh) + "】阻止";
            return false;
        }
    }
    return true;
}

}  // namespace gf
