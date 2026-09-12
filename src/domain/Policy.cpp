#include "domain/Policy.h"

#include <algorithm>

#include "core/GameState.h"
#include "gen/EmpireGen.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {
namespace {

using G = PolicyGroup;
using K = ModKind;
using F = FactionKind;

struct Seed {
    G group;
    const char* idName;
    const char* zh;
    const char* desc;
    i64 influence;
    i64 upkeep;
    int transition;
    std::array<PolicyEffect, 3> eff;
    u8 n;
    F favored;
    F harmed;
    Fixed fdelta;
};

PolicyEffect E(K k, Fixed v) { return PolicyEffect{k, v}; }

const std::vector<Seed>& seeds() {
    static const std::vector<Seed> s = {
        // ================= 经济体制 =================
        {G::Economic, "freeMarket", "自由市场",
         "放手让资本流动：贸易与研究活跃，但贫富差距拉大、劳工不满。", 0, 0, 1,
         {E(K::TradeMargin, Fixed::pct(20)), E(K::ResearchRate, Fixed::pct(10)), E(K::Unrest, Fixed::pct(8))}, 3,
         F::Count, F::Count, Fixed(0)},   // 默认项：中立，代表现状
        {G::Economic, "stateDirected", "国家主导",
         "国家统筹关键产业：建造与稳定提升，贸易活力下降。", 200, 300, 2,
         {E(K::BuildRate, Fixed::pct(22)), E(K::Stability, Fixed::pct(12)), E(K::TradeMargin, Fixed::pct(-12))}, 3,
         F::Technocrat, F::Merchant, Fixed::pct(10)},
        {G::Economic, "warEconomyPolicy", "战时统制",
         "全面转入战时生产：军力与建造大增，民生与贸易严重受损。", 400, 600, 3,
         {E(K::MilitaryPower, Fixed::pct(30)), E(K::BuildRate, Fixed::pct(15)), E(K::Growth, Fixed::pct(-20))}, 3,
         F::Military, F::Labor, Fixed::pct(16)},
        {G::Economic, "ecological", "生态经济",
         "把宜居度置于增长之上：人口与稳定提升，产能受限。", 250, 200, 2,
         {E(K::Growth, Fixed::pct(18)), E(K::Stability, Fixed::pct(15)), E(K::BuildRate, Fixed::pct(-10))}, 3,
         F::Populist, F::Syndicate, Fixed::pct(12)},

        // ================= 军事体制 =================
        {G::Military, "volunteerArmy", "志愿兵役",
         "职业化军队：战力精锐但规模有限。", 0, 0, 1,
         {E(K::MilitaryPower, Fixed::pct(12)), E(K::Stability, Fixed::pct(5)), E(K::Growth, Fixed::pct(-3))}, 3,
         F::Count, F::Count, Fixed(0)},   // 默认项：中立
        {G::Military, "conscription", "义务兵役",
         "全民皆兵：军力与人力大增，社会负担沉重。", 300, 400, 2,
         {E(K::MilitaryPower, Fixed::pct(25)), E(K::Growth, Fixed::pct(6)), E(K::Unrest, Fixed::pct(10))}, 3,
         F::Military, F::Populist, Fixed::pct(10)},
        {G::Military, "fortressDoctrine", "要塞主义",
         "以防御为核心：稳定与侦测提升，进攻能力下降。", 250, 350, 2,
         {E(K::Stability, Fixed::pct(18)), E(K::Detection, Fixed::pct(20)), E(K::MilitaryPower, Fixed::pct(-8))}, 3,
         F::Nobility, F::Military, Fixed::pct(6)},
        {G::Military, "mercenary", "雇佣军团",
         "金钱换刀锋：短期军力强，但财力消耗巨大且忠诚存疑。", 200, 800, 1,
         {E(K::MilitaryPower, Fixed::pct(20)), E(K::CreditRating, Fixed::pct(-10)), E(K::Stability, Fixed::pct(-6))},
         3, F::Syndicate, F::Military, Fixed::pct(8)},

        // ================= 社会体制 =================
        {G::Social, "openSociety", "开放社会",
         "信息自由流动：研究与影响力提升，但更易被渗透。", 0, 0, 1,
         {E(K::ResearchRate, Fixed::pct(15)), E(K::InfluenceGain, Fixed::pct(10)), E(K::IntelDefense, Fixed::pct(-12))},
         3, F::Count, F::Count, Fixed(0)},   // 默认项：中立
        {G::Social, "controlledSociety", "管制社会",
         "言论与流动受控：稳定与反间谍提升，研究受限。", 300, 250, 2,
         {E(K::Stability, Fixed::pct(20)), E(K::IntelDefense, Fixed::pct(18)), E(K::ResearchRate, Fixed::pct(-10))}, 3,
         F::Fundamentalist, F::Merchant, Fixed::pct(12)},
        {G::Social, "welfareState", "福利国家",
         "高税收高保障：民心与增长提升，财政吃紧。", 350, 900, 3,
         {E(K::Growth, Fixed::pct(20)), E(K::Unrest, Fixed::pct(-18)), E(K::TradeMargin, Fixed::pct(-8))}, 3,
         F::Labor, F::Nobility, Fixed::pct(14)},
        {G::Social, "meritocracy", "精英择优",
         "以能力分配地位：研究与建造提升，旧贵族失势。", 300, 300, 2,
         {E(K::ResearchRate, Fixed::pct(18)), E(K::BuildRate, Fixed::pct(10)), E(K::Stability, Fixed::pct(-8))}, 3,
         F::Technocrat, F::Nobility, Fixed::pct(12)},

        // ================= 外交路线 =================
        {G::Diplomatic, "openDiplomacy", "开放外交",
         "广结善缘：外交权重与贸易提升，但立场易被牵制。", 0, 200, 1,
         {E(K::DiploWeight, Fixed::pct(20)), E(K::TradeMargin, Fixed::pct(10)), E(K::IntelDefense, Fixed::pct(-6))}, 3,
         F::Count, F::Count, Fixed(0)},   // 默认项：中立
        {G::Diplomatic, "isolation", "孤立路线",
         "关起门来：稳定与反间谍提升，外交与贸易萎缩。", 250, 100, 2,
         {E(K::Stability, Fixed::pct(18)), E(K::IntelDefense, Fixed::pct(22)), E(K::DiploWeight, Fixed::pct(-20))}, 3,
         F::Fundamentalist, F::Merchant, Fixed::pct(12)},
        {G::Diplomatic, "hegemony", "霸权路线",
         "以实力定规则：军力与外交权重提升，招致普遍恐惧。", 500, 700, 3,
         {E(K::MilitaryPower, Fixed::pct(18)), E(K::DiploWeight, Fixed::pct(15)), E(K::TradeMargin, Fixed::pct(-10))}, 3,
         F::Military, F::Populist, Fixed::pct(10)},
        {G::Diplomatic, "vassalNetwork", "藩属网络",
         "以附庸换影响力：影响力与殖民效率提升。", 400, 400, 2,
         {E(K::InfluenceGain, Fixed::pct(22)), E(K::ColonyCost, Fixed::pct(-25)), E(K::DiploWeight, Fixed::pct(-8))}, 3,
         F::Nobility, F::Populist, Fixed::pct(10)},

        // ================= 情报体制 =================
        {G::Intelligence, "transparentIntel", "透明情报",
         "情报公开共享：研究与合作顺畅，但反渗透薄弱。", 0, 0, 1,
         {E(K::ResearchRate, Fixed::pct(10)), E(K::DiploWeight, Fixed::pct(8)), E(K::ManipulationSkill, Fixed::pct(-15))},
         3, F::Count, F::Count, Fixed(0)},   // 默认项：中立
        {G::Intelligence, "secretService", "秘密机构",
         "professional 情报体系：侦测与操纵提升，民怨上升。", 350, 500, 2,
         {E(K::Detection, Fixed::pct(25)), E(K::ManipulationSkill, Fixed::pct(22)), E(K::Unrest, Fixed::pct(10))}, 3,
         F::Syndicate, F::Populist, Fixed::pct(10)},
        {G::Intelligence, "psionicCorps", "灵能军团",
         "以心灵屏障与读心为核心：反间谍极强，招致他国敌意。", 450, 550, 3,
         {E(K::IntelDefense, Fixed::pct(30)), E(K::DiploWeight, Fixed::pct(-12)), E(K::ManipulationSkill, Fixed::pct(10))},
         3, F::Fundamentalist, F::Merchant, Fixed::pct(12)},
    };
    return s;
}

}  // namespace

std::size_t policyOptionCount() { return seeds().size(); }

const PolicyOption& policyOption(std::size_t idx) {
    static const std::vector<PolicyOption> table = [] {
        std::vector<PolicyOption> v;
        int nextId = 0;
        for (const auto& s : seeds()) {
            PolicyOption p;
            p.id = static_cast<u8>(nextId++);
            p.group = s.group;
            p.idName = s.idName;
            p.nameZh = s.zh;
            p.desc = s.desc;
            p.influenceCost = s.influence;
            p.upkeep = s.upkeep;
            p.transition = s.transition;
            p.effects = s.eff;
            p.effectCount = s.n;
            p.favored = s.favored;
            p.harmed = s.harmed;
            p.factionDelta = s.fdelta;
            v.push_back(p);
        }
        return v;
    }();
    if (idx >= table.size()) idx = 0;
    return table[idx];
}

const PolicyOption* policyOptionById(u8 id) {
    for (std::size_t i = 0; i < policyOptionCount(); ++i)
        if (policyOption(i).id == id) return &policyOption(i);
    return nullptr;
}

std::string_view policyGroupName(PolicyGroup g) {
    switch (g) {
        case PolicyGroup::Economic: return "经济体制";
        case PolicyGroup::Military: return "军事体制";
        case PolicyGroup::Social: return "社会体制";
        case PolicyGroup::Diplomatic: return "外交路线";
        case PolicyGroup::Intelligence: return "情报体制";
        case PolicyGroup::Count: break;
    }
    return "?";
}

PolicyGroup policyGroupFromName(std::string_view s) {
    for (int i = 0; i < static_cast<int>(PolicyGroup::Count); ++i)
        if (policyGroupName(static_cast<PolicyGroup>(i)) == s) return static_cast<PolicyGroup>(i);
    if (iequals(s, "economic")) return PolicyGroup::Economic;
    if (iequals(s, "military")) return PolicyGroup::Military;
    if (iequals(s, "social")) return PolicyGroup::Social;
    if (iequals(s, "diplomatic")) return PolicyGroup::Diplomatic;
    if (iequals(s, "intelligence")) return PolicyGroup::Intelligence;
    return PolicyGroup::Count;
}

std::vector<const PolicyOption*> policyOptionsIn(PolicyGroup g) {
    std::vector<const PolicyOption*> out;
    for (std::size_t i = 0; i < policyOptionCount(); ++i)
        if (policyOption(i).group == g) out.push_back(&policyOption(i));
    return out;
}

const PolicyOption* policyFind(std::string_view key) {
    for (std::size_t i = 0; i < policyOptionCount(); ++i) {
        const PolicyOption& p = policyOption(i);
        if (p.idName == key || p.nameZh == key) return &p;
    }
    // 允许按「组名」查找该组默认（第一项）
    PolicyGroup g = policyGroupFromName(key);
    if (g != PolicyGroup::Count) {
        auto v = policyOptionsIn(g);
        if (!v.empty()) return v.front();
    }
    return nullptr;
}

std::string policyEffectText(const PolicyOption& opt) {
    std::string out;
    for (u8 i = 0; i < opt.effectCount && i < opt.effects.size(); ++i) {
        if (opt.effects[i].kind == ModKind::Count) continue;
        if (!out.empty()) out += "，";
        out += std::string(modKindName(opt.effects[i].kind)) + " " +
               fixedStrSigned(opt.effects[i].value * Fixed(100), 1) + "%";
    }
    return out.empty() ? "无修正" : out;
}

void policyInitDefaults(GameState& st, u32 empire) {
    Empire* e = st.empire(empire);
    if (e == nullptr) return;
    for (int g = 0; g < kPolicyGroupCount; ++g) {
        auto opts = policyOptionsIn(static_cast<PolicyGroup>(g));
        if (opts.empty()) continue;
        e->policies.active[static_cast<std::size_t>(g)] = opts.front()->id;
        e->policies.pending[static_cast<std::size_t>(g)] = kNoPolicy;
        e->policies.transitionLeft[static_cast<std::size_t>(g)] = 0;
    }
}

Fixed policyModifier(const GameState& st, u32 empire, ModKind kind) {
    const Empire* e = st.empire(empire);
    if (e == nullptr || kind == ModKind::Count) return Fixed(0);
    Fixed acc = Fixed(0);
    for (int g = 0; g < kPolicyGroupCount; ++g) {
        std::size_t gi = static_cast<std::size_t>(g);
        u8 activeId = e->policies.active[gi];
        if (activeId == kNoPolicy) continue;
        const PolicyOption* p = policyOptionById(activeId);
        if (p == nullptr) continue;
        // 过渡期内效果按进度线性生效
        Fixed scale = Fixed(1);
        u8 pendingId = e->policies.pending[gi];
        if (pendingId != kNoPolicy) {
            const PolicyOption* np = policyOptionById(pendingId);
            int total = (np != nullptr && np->transition > 0) ? np->transition : 1;
            int left = e->policies.transitionLeft[gi];
            Fixed progress = Fixed(1) - Fixed(left) / Fixed(total);
            scale = fxClamp(progress, Fixed(0), Fixed(1));
            // 过渡中：旧政策效果按 (1-progress) 衰减，新政策按 progress 生效
            for (u8 i = 0; i < p->effectCount && i < p->effects.size(); ++i)
                if (p->effects[i].kind == kind) acc += p->effects[i].value * (Fixed(1) - scale);
            if (np != nullptr) {
                for (u8 i = 0; i < np->effectCount && i < np->effects.size(); ++i)
                    if (np->effects[i].kind == kind) acc += np->effects[i].value * scale;
            }
            continue;
        }
        for (u8 i = 0; i < p->effectCount && i < p->effects.size(); ++i)
            if (p->effects[i].kind == kind) acc += p->effects[i].value;
    }
    return acc;
}

Fixed policyFactionTarget(const GameState& st, u32 empire, FactionKind f) {
    // 基准 50%，政策把它拉向有利或不利的方向；限制在 10%~90%，
    // 保留向任一方向变化的余地（否则政策一改就无法体现）。
    Fixed base = Fixed::pct(50);
    Fixed impact = policyFactionImpact(st, empire, f);
    return fxClamp(base + impact, Fixed::pct(10), Fixed::pct(90));
}

Fixed policyFactionImpact(const GameState& st, u32 empire, FactionKind f) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return Fixed(0);
    Fixed acc = Fixed(0);
    for (int g = 0; g < kPolicyGroupCount; ++g) {
        u8 id = e->policies.active[static_cast<std::size_t>(g)];
        if (id == kNoPolicy) continue;
        const PolicyOption* p = policyOptionById(id);
        if (p == nullptr) continue;
        if (p->favored == f) acc += p->factionDelta;
        if (p->harmed == f) acc -= p->factionDelta;
    }
    return acc;
}

i64 policyUpkeep(const GameState& st, u32 empire) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return 0;
    i64 sum = 0;
    for (int g = 0; g < kPolicyGroupCount; ++g) {
        u8 id = e->policies.active[static_cast<std::size_t>(g)];
        if (id == kNoPolicy) continue;
        const PolicyOption* p = policyOptionById(id);
        if (p != nullptr) sum += p->upkeep;
    }
    return sum;
}

bool policyEnact(GameState& st, u32 empire, const PolicyOption& opt, std::string* err) {
    Empire* e = st.empire(empire);
    if (e == nullptr) {
        if (err) *err = "非法主体";
        return false;
    }
    std::size_t gi = static_cast<std::size_t>(opt.group);
    if (e->policies.active[gi] == opt.id && e->policies.pending[gi] == kNoPolicy) {
        if (err) *err = "【" + std::string(opt.nameZh) + "】已经是当前政策";
        return false;
    }
    if (e->policies.pending[gi] != kNoPolicy) {
        if (err)
            *err = "该政策组正在推行另一项政策（剩余 " +
                   std::to_string(e->policies.transitionLeft[gi]) + " 季）";
        return false;
    }
    if (e->influence.rawValue() < Fixed(opt.influenceCost).rawValue()) {
        if (err)
            *err = "影响力不足：推行【" + std::string(opt.nameZh) + "】需要 " +
                   groupDigits(opt.influenceCost);
        return false;
    }
    e->influence -= Fixed(opt.influenceCost);
    e->policies.pending[gi] = opt.id;
    e->policies.transitionLeft[gi] = static_cast<u8>(std::max(1, opt.transition));
    // 立刻施加派系反应
    for (auto& f : e->domestic.factions) {
        if (f.kind == opt.favored) f.satisfaction = fxClamp(f.satisfaction + opt.factionDelta, Fixed(0), Fixed(1));
        if (f.kind == opt.harmed) f.satisfaction = fxClamp(f.satisfaction - opt.factionDelta, Fixed(0), Fixed(1));
    }
    ++e->policies.enactCount;
    st.logEvent(LogPhase::Domestic, "policy.enact",
                e->name + " 开始推行【" + std::string(opt.nameZh) + "】（过渡 " +
                    std::to_string(opt.transition) + " 季）",
                empire);
    return true;
}

void policyPhase(GameState& st) {
    for (auto& e : st.empires) {
        if (!e.alive) continue;
        for (int g = 0; g < kPolicyGroupCount; ++g) {
            std::size_t gi = static_cast<std::size_t>(g);
            if (e.policies.pending[gi] == kNoPolicy) continue;
            if (e.policies.transitionLeft[gi] > 0) --e.policies.transitionLeft[gi];
            if (e.policies.transitionLeft[gi] > 0) continue;
            // 过渡完成
            u8 newId = e.policies.pending[gi];
            e.policies.active[gi] = newId;
            e.policies.pending[gi] = kNoPolicy;
            const PolicyOption* p = policyOptionById(newId);
            if (p != nullptr) {
                st.logEvent(LogPhase::Domestic, "policy.active",
                            e.name + " 的【" + std::string(policyGroupName(p->group)) + "】已切换为【" +
                                std::string(p->nameZh) + "】",
                            e.id);
            }
        }
    }
}

namespace {

/// AI 对某项政策的偏好分：由伦理、政体与当前处境决定
i64 aiPolicyPreference(const GameState& st, const Empire& e, const PolicyOption& p) {
    i64 score = 0;
    const std::string_view id = p.idName;

    // ---- 伦理取向 ----
    for (u8 eth : e.ethics) {
        switch (static_cast<EthicAxis>(eth)) {
            case EthicAxis::Militarism:
                if (id == "warEconomyPolicy" || id == "conscription" || id == "hegemony") score += 40;
                if (id == "ecological" || id == "welfareState") score -= 25;
                break;
            case EthicAxis::Commerce:
                if (id == "freeMarket" || id == "centralBank") score += 40;
                if (id == "warEconomyPolicy" || id == "isolation") score -= 35;
                break;
            case EthicAxis::Science:
                if (id == "meritocracy" || id == "openSociety") score += 35;
                if (id == "controlledSociety" || id == "isolation") score -= 35;
                break;
            case EthicAxis::Faith:
                if (id == "controlledSociety" || id == "isolation") score += 35;
                if (id == "openSociety" || id == "meritocracy") score -= 35;
                break;
            case EthicAxis::Liberty:
                if (id == "openSociety" || id == "welfareState") score += 35;
                if (id == "controlledSociety" || id == "secretService") score -= 40;
                break;
            case EthicAxis::Order:
                if (id == "controlledSociety" || id == "secretService") score += 32;
                if (id == "openSociety") score -= 25;
                break;
            case EthicAxis::Ecology:
                if (id == "ecological") score += 45;
                if (id == "warEconomyPolicy") score -= 30;
                break;
            case EthicAxis::Isolation:
                if (id == "isolation") score += 45;
                if (id == "openDiplomacy" || id == "hegemony") score -= 35;
                break;
            case EthicAxis::Expansion:
                if (id == "hegemony" || id == "conscription") score += 30;
                break;
            case EthicAxis::Collectivism:
                if (id == "stateDirected" || id == "welfareState") score += 30;
                break;
            case EthicAxis::Individualism:
                if (id == "freeMarket" || id == "mercenary") score += 30;
                break;
            case EthicAxis::Purity:
                if (id == "isolation" || id == "controlledSociety") score += 25;
                break;
            default: break;
        }
    }

    // ---- 处境 ----
    // 战争中偏好军事与战时经济
    bool atWar = false;
    for (const auto& r : st.relations) {
        std::size_t idx = static_cast<std::size_t>(&r - st.relations.data());
        if (idx / kMaxEmpires == e.id && r.atWar) atWar = true;
    }
    if (atWar) {
        if (p.group == PolicyGroup::Military) score += 35;
        if (id == "warEconomyPolicy") score += 30;
        if (id == "freeMarket") score -= 20;
    }
    // 民怨高 ⇒ 安抚型政策
    if (e.domestic.unrest.rawValue() > Fixed::pct(40).rawValue()) {
        if (id == "welfareState" || id == "ecological") score += 35;
        if (id == "warEconomyPolicy" || id == "mercenary") score -= 25;
    }
    // 国库空虚 ⇒ 节流
    if (e.treasury.rawValue() < Fixed(10000).rawValue()) {
        if (p.upkeep > 500) score -= 30;
        if (p.upkeep == 0) score += 15;
    }
    // 政变风险高 ⇒ 避免激怒强势派系
    Fixed risk = Fixed(0);
    {
        Fixed dissat = Fixed(0), infl = Fixed(0);
        for (const auto& f : e.domestic.factions) {
            dissat += (Fixed(1) - f.satisfaction) * f.influence;
            infl += f.influence;
        }
        if (infl.rawValue() > 0) risk = dissat / infl;
    }
    if (risk.rawValue() > Fixed::pct(55).rawValue()) {
        // 高压时期优先选择不伤害任何派系的政策
        if (p.favored == FactionKind::Count && p.harmed == FactionKind::Count) score += 25;
        else score -= 20;
    }
    // 科技取向：科研型帝国偏好研究加成
    if (empireModifier(e, ModKind::ResearchRate).rawValue() > Fixed::pct(20).rawValue() &&
        id == "meritocracy")
        score += 20;
    return score;
}

}  // namespace

void policyAiPhase(GameState& st) {
    for (auto& e : st.empires) {
        if (e.isPlayer || !e.alive) continue;
        // 错开各帝国的决策节奏
        if ((st.tick + e.id) % 8 != 0) continue;
        // 每次只在一个组里做决定，避免一季内全部换血
        std::size_t gi = static_cast<std::size_t>((st.tick / 8 + e.id) % kPolicyGroupCount);
        // 该组正在过渡则跳过
        if (e.policies.pending[gi] != kNoPolicy) continue;

        const PolicyOption* current = policyOptionById(e.policies.active[gi]);
        const PolicyOption* best = nullptr;
        i64 bestScore = 0;
        for (const PolicyOption* p : policyOptionsIn(static_cast<PolicyGroup>(gi))) {
            if (current != nullptr && p->id == current->id) continue;
            // 影响力必须够
            if (e.influence.rawValue() < Fixed(p->influenceCost).rawValue()) continue;
            i64 sc = aiPolicyPreference(st, e, *p);
            if (sc > bestScore) {
                bestScore = sc;
                best = p;
            }
        }
        // 只有明显更优才切换（避免反复摇摆）
        if (best == nullptr || bestScore < 30) continue;
        (void)policyEnact(st, e.id, *best, nullptr);
    }
}

std::string policyText(const GameState& st, u32 empireId) {
    const Empire* e = st.empire(empireId);
    if (e == nullptr) return "非法主体";
    std::string out;
    for (int g = 0; g < kPolicyGroupCount; ++g) {
        auto grp = static_cast<PolicyGroup>(g);
        std::size_t gi = static_cast<std::size_t>(g);
        out += "\n" + style("── " + std::string(policyGroupName(grp)) + " ──", Style::Sub) + "\n";
        const PolicyOption* cur = (e->policies.active[gi] != kNoPolicy)
                                      ? policyOptionById(e->policies.active[gi])
                                      : nullptr;
        const PolicyOption* pend = (e->policies.pending[gi] != kNoPolicy)
                                       ? policyOptionById(e->policies.pending[gi])
                                       : nullptr;
        for (const PolicyOption* p : policyOptionsIn(grp)) {
            bool isCur = (cur != nullptr && cur->id == p->id);
            bool isPend = (pend != nullptr && pend->id == p->id);
            std::string mark = isCur ? style("[生效中]", Style::Good)
                                     : (isPend ? style("[推行中]", Style::Warn) : "        ");
            out += "  " + padRight(std::string(p->idName), 18) + padRight(std::string(p->nameZh), 12) + mark +
                   "  影响 " + fixedStr(p->influenceCost, 0) + "  维护 " + std::to_string(p->upkeep) + "/季\n";
            out += "      " + wrapJoin(p->desc, 76, "      ") + "\n";
            out += "      效果：" + policyEffectText(*p) + "\n";
        }
        if (pend != nullptr) {
            out += "  " + style("正在推行【" + std::string(pend->nameZh) + "】，剩余 " +
                                    std::to_string(e->policies.transitionLeft[gi]) + " 季",
                                Style::Warn) +
                   "\n";
        }
    }
    out += "\n本季政策维护合计：" + groupDigits(policyUpkeep(st, empireId)) + " cr\n";
    out += "用法：greyfall policy --enact <政策名>\n";
    out += "      greyfall policy --detail <政策名>\n";
    return out;
}

std::string policyDetailText(const GameState& st, u32 empireId, const PolicyOption& opt) {
    const Empire* e = st.empire(empireId);
    std::string out;
    out += style("【" + std::string(opt.nameZh) + "】", Style::Heading) + "  " +
           std::string(policyGroupName(opt.group)) + "\n";
    out += wrapJoin(opt.desc, 86, "  ") + "\n\n";
    out += "  推行成本：" + groupDigits(opt.influenceCost) + " 影响力\n";
    out += "  每季维护：" + std::to_string(opt.upkeep) + " cr\n";
    out += "  过渡期：" + std::to_string(opt.transition) + " 季（期间效果线性切换）\n";
    out += "  效果：" + policyEffectText(opt) + "\n";
    if (opt.favored != FactionKind::Count)
        out += "  派系反应：+" + fixedStrPlain(opt.factionDelta * Fixed(100), 0) + "% 于 " +
               std::string(factionKindName(opt.favored));
    if (opt.harmed != FactionKind::Count)
        out += "，-" + fixedStrPlain(opt.factionDelta * Fixed(100), 0) + "% 于 " +
               std::string(factionKindName(opt.harmed));
    out += "\n";
    if (e != nullptr) {
        std::size_t gi = static_cast<std::size_t>(opt.group);
        bool isCur = (e->policies.active[gi] == opt.id);
        bool isPend = (e->policies.pending[gi] == opt.id);
        out += std::string("\n  当前状态：") + (isCur ? "已生效" : (isPend ? "推行中" : "未启用")) + "\n";
        if (!isCur && !isPend) {
            bool can = e->influence.rawValue() >= Fixed(opt.influenceCost).rawValue() &&
                       e->policies.pending[gi] == kNoPolicy;
            out += std::string("  可否推行：") + (can ? style("是", Style::Good)
                                                       : style("否（影响力不足或该组正在过渡）", Style::Bad)) +
                   "\n";
        }
    }
    return out;
}

}  // namespace gf
