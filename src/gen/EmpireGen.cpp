#include "gen/EmpireGen.h"
#include "domain/Government.h"

#include <algorithm>
#include <limits>

#include "domain/Empire.h"
#include "domain/Personnel.h"
#include "domain/SpeciesAdv.h"
#include "domain/Parliament.h"
#include "domain/Policy.h"
#include "domain/Resolution.h"
#include "domain/Fleet.h"
#include "combat/Resolver.h"
#include "gen/NameGen.h"
#include "rng/Streams.h"

namespace gf {
namespace {

i64 squaredDist(const SystemNode& a, const SystemNode& b) {
    i64 dx = static_cast<i64>(a.x) - b.x;
    i64 dy = static_cast<i64>(a.y) - b.y;
    return dx * dx + dy * dy;
}

FleetDesign makeDesign(const Empire& e, HullClass hull, u64 variant) {
    const HullInfo& hi = hullInfo(hull);
    FleetDesign d;
    d.hull = hull;
    d.name = std::string(hi.nameZh) + std::string("-") + std::string(1, static_cast<char>('A' + static_cast<int>(variant % 6)));
    d.firepower = hi.baseFirepower;
    d.defense = hi.baseDefense;
    d.speed = hi.baseSpeed;
    d.supplyUse = hi.baseSupply;
    d.creditCost = hi.baseCost;
    // 按船体槽位装配同类模块
    int slots = hi.slots;
    for (int i = 0; i < slots; ++i) {
        u8 mod = 0;
        if (i % 4 == 0) mod = static_cast<u8>((variant + i) % 8);
        else if (i % 4 == 1) mod = static_cast<u8>(8 + (variant % 5));
        else if (i % 4 == 2) mod = static_cast<u8>(14 + (variant % 5));
        else mod = static_cast<u8>(20 + (variant % 12));
        d.modules.push_back(mod);
        const ModuleInfo& mi = moduleInfo(mod);
        switch (mi.effect) {
            case ModuleEffect::Firepower: d.firepower += mi.effectValue; break;
            case ModuleEffect::Defense: d.defense += mi.effectValue; break;
            case ModuleEffect::Speed: d.speed += mi.effectValue; break;
            case ModuleEffect::Supply: d.supplyUse += mi.effectValue; break;
            default: break;
        }
        for (int c = 0; c < kCommodityCount; ++c)
            // 注意：buildCost 是「单位数」而非定点原始值。
            // 早期这里用了 rawValue()（×1000），把 40 合金记成 40,000 ——
            // 于是造一艘护卫舰需要 80,000 合金，永远造不起。
            d.buildCost[static_cast<std::size_t>(c)] +=
                mi.buildCost[static_cast<std::size_t>(c)].rawValue() / FIX;
    }
    (void)e;
    return d;
}

}  // namespace

Fixed empireModifier(const Empire& e, ModKind kind) {
    Fixed acc = developmentModifier(e, kind);
    const SpeciesInfo& sp = speciesInfo(e.species);
    for (u8 i = 0; i < sp.modCount; ++i)
        if (sp.mods[i].kind == kind) acc += sp.mods[i].value;
    for (std::size_t i = 0; i < e.ethics.size(); ++i) {
        const EthicInfo& et = ethicInfo(e.ethics[i]);
        for (u8 k = 0; k < et.modCount; ++k)
            if (et.mods[k].kind == kind) acc += et.mods[k].value;
    }
    for (std::size_t i = 0; i < e.civics.size(); ++i) {
        const CivicInfo& cv = civicInfo(e.civics[i]);
        for (u8 k = 0; k < cv.modCount; ++k)
            if (cv.mods[k].kind == kind) acc += cv.mods[k].value;
        // 互斥公民：双方同持时收益减半
        for (u8 c = 0; c < cv.conflictCount; ++c) {
            for (std::size_t j = 0; j < e.civics.size(); ++j) {
                if (e.civics[j] == cv.conflicts[c]) acc -= cv.mods[0].value * Fixed::pct(50);
            }
        }
    }
    const GovernmentInfo& gv = governmentInfo(e.government);
    if (kind == ModKind::Stability) acc += gv.unrestBias;
    // 政体差异化：工厂效率（社会主义最高）
    if (kind == ModKind::BuildRate) acc += gv.buildEfficiency;
    // 政体差异化：外交信誉与关系改善（民主主义最高）
    if (kind == ModKind::DiploWeight) acc += gv.opinionGain;
    // 游牧政体：军事力量随「群势」提升（舰队即国土）
    if (kind == ModKind::MilitaryPower) {
        // 由 SpeciesAdv 派生：此处只做粗略加成，精确值见 nomadicMilitaryBonus
        (void)0;
    }
    // 基因改造：永久改写本国种族的特质
    if (e.geneMods[static_cast<std::size_t>(GeneMod::Hardy)] && kind == ModKind::Growth)
        acc += Fixed::pct(20);
    if (e.geneMods[static_cast<std::size_t>(GeneMod::Industrious)] && kind == ModKind::BuildRate)
        acc += Fixed::pct(15);
    if (e.geneMods[static_cast<std::size_t>(GeneMod::Erudite)] && kind == ModKind::ResearchRate)
        acc += Fixed::pct(15);
    if (e.geneMods[static_cast<std::size_t>(GeneMod::Resilient)] && kind == ModKind::Stability)
        acc += Fixed::pct(12);
    if (e.geneMods[static_cast<std::size_t>(GeneMod::Docile)] && kind == ModKind::Unrest)
        acc -= Fixed::pct(15);

    // 领袖：国家元首的出身提供长期修正
    switch (e.ruler.trait) {
        case RulerTrait::Administrator:
            if (kind == ModKind::BuildRate) acc += Fixed::pct(10);
            if (kind == ModKind::Stability) acc += Fixed::pct(8);
            break;
        case RulerTrait::Warlord:
            if (kind == ModKind::MilitaryPower) acc += Fixed::pct(12);
            break;
        case RulerTrait::Scientist:
            if (kind == ModKind::ResearchRate) acc += Fixed::pct(15);
            break;
        case RulerTrait::Merchant:
            if (kind == ModKind::TradeMargin) acc += Fixed::pct(10);
            if (kind == ModKind::CreditRating) acc += Fixed::pct(10);
            break;
        case RulerTrait::Demagogue:
            if (kind == ModKind::InfluenceGain) acc += Fixed::pct(15);
            break;
        case RulerTrait::Reformer:
            if (kind == ModKind::Stability) acc += Fixed::pct(6);
            break;
        default: break;
    }
    // 决议系统：生效中的修正类效果
    for (const auto& a : e.resolutions.active) {
        if (a.ticksLeft == 0) continue;
        if (resTargetToMod(a.target) == kind) acc += a.value;
    }
    // 科技：已完成科技的直接效果 + 分支专精 + 满级奖励
    acc += techModifier(e.tech, kind);
    // 议会：已通过法案的永久效果（只依赖 passed 列表，无需 GameState）
    for (u16 bid : e.parliament.passed) {
        if (bid >= kBillCount) continue;
        const BillDef& bd = billDef(static_cast<int>(bid));
        for (u8 i = 0; i < bd.effectCount && i < bd.effects.size(); ++i)
            if (bd.effects[i].kind == kind) acc += bd.effects[i].value;
    }
    // 政策：持久化法令（含过渡期的线性切换）
    {
        // 需要 GameState 才能查政策；empireModifier 只拿到 Empire，
        // 故此处直接按帝国的 policies 字段结算（不依赖全局状态）。
        for (int pg = 0; pg < kPolicyGroupCount; ++pg) {
            std::size_t gi = static_cast<std::size_t>(pg);
            u8 pend = e.policies.pending[gi];
            u8 act = e.policies.active[gi];
            if (pend != kNoPolicy) {
                const PolicyOption* np = policyOptionById(pend);
                const PolicyOption* op = (act != kNoPolicy) ? policyOptionById(act) : nullptr;
                int total = (np != nullptr && np->transition > 0) ? np->transition : 1;
                Fixed progress = Fixed(1) - Fixed(e.policies.transitionLeft[gi]) / Fixed(total);
                progress = fxClamp(progress, Fixed(0), Fixed(1));
                if (op != nullptr)
                    for (u8 i = 0; i < op->effectCount && i < op->effects.size(); ++i)
                        if (op->effects[i].kind == kind) acc += op->effects[i].value * (Fixed(1) - progress);
                if (np != nullptr)
                    for (u8 i = 0; i < np->effectCount && i < np->effects.size(); ++i)
                        if (np->effects[i].kind == kind) acc += np->effects[i].value * progress;
                continue;
            }
            if (act == kNoPolicy) continue;
            const PolicyOption* op = policyOptionById(act);
            if (op == nullptr) continue;
            for (u8 i = 0; i < op->effectCount && i < op->effects.size(); ++i)
                if (op->effects[i].kind == kind) acc += op->effects[i].value;
        }
    }
    return acc;
}

void initMind(Empire& e, int difficulty, RngBus& rng) {
    EmpireMind& m = e.mind;
    for (std::size_t i = 0; i < m.playerModel.wGoal.size(); ++i)
        m.playerModel.wGoal[i] = Fixed::raw(static_cast<i64>(rng.range(RngStream::Ai, 200, 1000)));
    m.playerModel.riskAversion = Fixed::raw(static_cast<i64>(rng.range(RngStream::Ai, 200, 850)));
    m.playerModel.discount = Fixed::raw(static_cast<i64>(rng.range(RngStream::Ai, 880, 985)));
    for (std::size_t i = 0; i < m.playerModel.typeBelief.size(); ++i)
        m.playerModel.typeBelief[i] = Fixed::raw(FIX / static_cast<i64>(kTypeCount));
    m.playerModel.modelConfidence = Fixed(0);
    for (std::size_t i = 0; i < m.reputation.size(); ++i) {
        m.reputation[i] = Fixed::pct(50);
        m.grudge[i] = Fixed(0);
        m.threat[i] = Fixed(0);
    }
    m.foresight = static_cast<u8>(std::min(4, 1 + difficulty / 2 + static_cast<int>(rng.nextU32(RngStream::Ai) % 2)));
    if (m.foresight < 1) m.foresight = 1;
    if (m.foresight > 4) m.foresight = 4;
    m.nodeBudget = 4000 + difficulty * 6000;
}

void generateEmpires(GameState& st, const EmpireGenOptions& opts) {
    RngBus& rng = st.rng;
    NameGen names(opts.seed ^ 0xE7719Eull);

    int count = opts.count;
    if (count < 8) count = 8;
    if (count > kMaxEmpires) count = kMaxEmpires;
    st.empires.clear();
    st.empires.reserve(static_cast<std::size_t>(count));

    // 首都选择：贪心最大化最小间距
    std::vector<u32> capitals;
    if (!st.map.systems.empty()) {
        u32 first = static_cast<u32>(rng.pick(RngStream::Empire, st.map.systems.size()));
        capitals.push_back(first);
        while (static_cast<int>(capitals.size()) < count) {
            i64 bestScore = -1;
            u32 bestSys = 0;
            for (const auto& sys : st.map.systems) {
                if (std::find(capitals.begin(), capitals.end(), sys.id) != capitals.end()) continue;
                i64 minD = std::numeric_limits<i64>::max();
                for (u32 c : capitals) minD = std::min(minD, squaredDist(sys, st.map.systems[c]));
                // 加一点随机扰动，避免完全确定的几何分布
                i64 score = minD + static_cast<i64>(rng.range(RngStream::Empire, 0, 900));
                if (score > bestScore) {
                    bestScore = score;
                    bestSys = sys.id;
                }
            }
            capitals.push_back(bestSys);
        }
    }

    for (int i = 0; i < count; ++i) {
        Empire e;
        e.id = static_cast<u32>(i);
        // 玩家国名同样随机 —— 原先恒为「灰域联合体」，
        // 无论用什么种子开局都是同一个国家，缺少代入感。
        e.name = names.empire();
        e.adjective = names.empireAdj();
        e.rulerName = names.ruler();
        e.isPlayer = (i == 0);
        e.species = static_cast<u8>(rng.pick(RngStream::Empire, kSpeciesCount));
        // 伦理：三条互不相同的轴
        for (std::size_t k = 0; k < e.ethics.size(); ++k) {
            u8 pick = 0;
            for (int attempt = 0; attempt < 12; ++attempt) {
                pick = static_cast<u8>(rng.pick(RngStream::Empire, kEthicsCount));
                bool dup = false;
                for (std::size_t j = 0; j < k; ++j)
                    if (e.ethics[j] == pick) dup = true;
                if (!dup) break;
            }
            e.ethics[k] = pick;
        }
        // 公民：四条，避开互斥
        for (std::size_t k = 0; k < e.civics.size(); ++k) {
            u8 pick = 0;
            for (int attempt = 0; attempt < 24; ++attempt) {
                pick = static_cast<u8>(rng.pick(RngStream::Empire, kCivicsCount));
                bool conflict = false;
                for (std::size_t j = 0; j < k; ++j) {
                    const CivicInfo& cj = civicInfo(e.civics[j]);
                    for (u8 c = 0; c < cj.conflictCount; ++c)
                        if (cj.conflicts[c] == pick) conflict = true;
                    const CivicInfo& cp = civicInfo(pick);
                    if (cp.id == e.civics[j]) conflict = true;
                    for (u8 c = 0; c < cp.conflictCount; ++c)
                        if (cp.conflicts[c] == e.civics[j]) conflict = true;
                }
                if (!conflict) break;
            }
            e.civics[k] = pick;
        }
        e.government = static_cast<u8>(rng.pick(RngStream::Empire, kGovernmentCount));
        e.stance = static_cast<EmpireStance>(rng.pick(RngStream::Empire, static_cast<std::size_t>(EmpireStance::Count)));
        e.capital = i < static_cast<int>(capitals.size()) ? capitals[static_cast<std::size_t>(i)] : 0;

        // 初始经济
        const GovernmentInfo& gv = governmentInfo(e.government);
        e.treasury = Fixed(60000 + rng.range(RngStream::Empire, 0, 40000));
        e.influence = Fixed(300 + rng.range(RngStream::Empire, 0, 400));
        e.unity = Fixed(200 + rng.range(RngStream::Empire, 0, 300));
        for (int c = 0; c < kCommodityCount; ++c) {
            const CommodityInfo& ci = commodityInfo(c);
            Fixed base = ci.basePrice;
            Fixed qty = Fixed(static_cast<i64>(rng.range(RngStream::Empire, 200, 2400)));
            if (ci.cat == EcoCategory::Strategic) qty = qty / Fixed(4);
            if (ci.cat == EcoCategory::Political) qty = qty / Fixed(3);
            e.stock[static_cast<std::size_t>(c)] = qty;
            e.capacity[static_cast<std::size_t>(c)] = qty / Fixed(20);
            // 需求量纲与产出匹配：使典型帝国自给率约 70~90%，
        // 缺口靠贸易补足 —— 这是贸易路线存在的意义。
        // 影响力与凝聚力由系统累积产生（政治行为、建筑），不属于消耗型商品：
        // 若把它们计入需求，每个帝国都会永久「短缺」这两项并推高民怨。
        if (c == static_cast<int>(Commodity::Influence) || c == static_cast<int>(Commodity::Unity)) {
            e.demand[static_cast<std::size_t>(c)] = Fixed(0);
        } else {
            e.demand[static_cast<std::size_t>(c)] = qty / Fixed(120);
        }
            (void)base;
        }
        e.stability = Fixed::pct(55) + governmentInfo(e.government).unrestBias;
        e.legitimacy = gv.legitimacy;
        e.creditRating = Fixed::pct(58) + empireModifier(e, ModKind::CreditRating);
        e.military = Fixed(400 + rng.range(RngStream::Empire, 0, 500));
        e.economy = Fixed(500 + rng.range(RngStream::Empire, 0, 600));
        e.apMax = 4 + gv.apBonus;
        e.apLeft = e.apMax;

        // 舰队设计
        for (int d = 0; d < 3; ++d) {
            HullClass hc = static_cast<HullClass>(std::min(static_cast<int>(HullClass::Cruiser), d + 1));
            FleetDesign fd = makeDesign(e, hc, static_cast<u64>(rng.pick(RngStream::Empire, 6)));
            // id 必须在装配完成后再定：makeDesign 里不设 id，
            // 这里统一分配并同步计数器，避免与后续新建的设计撞号。
            // id 由计数器统一分配，且**不再**用 size() 覆盖 ——
            // 用 size() 会让 id 与容器下标绑定，新建设计时极易撞号。
            fd.id = e.nextDesignId++;
            e.designs.push_back(std::move(fd));
        }

        // 内政派系
        e.domestic.factions.clear();
        for (int f = 0; f < static_cast<int>(FactionKind::Count); ++f) {
            Faction fa;
            fa.kind = static_cast<FactionKind>(f);
            fa.name = std::string(factionKindName(fa.kind));
            fa.influence = Fixed::raw(static_cast<i64>(rng.range(RngStream::Empire, 30, 220)));
            fa.satisfaction = Fixed::raw(static_cast<i64>(rng.range(RngStream::Empire, 350, 750)));
            e.domestic.factions.push_back(std::move(fa));
        }
        e.domestic.legitimacy = gv.legitimacy;
        e.domestic.unrest = Fixed::raw(static_cast<i64>(rng.range(RngStream::Empire, 40, 260)));

        initMind(e, opts.difficulty, rng);
        // 国家领袖：开局即产生，提供全国性修正并有任期与继承
        {
            static const char* kFirst[] = {"阿", "贝", "柯", "德", "恩", "法", "格", "海",
                                           "伊", "杰", "卡", "洛", "米", "诺", "奥", "佩"};
            static const char* kLast[] = {"恩", "斯", "尔", "顿", "森", "华", "理", "文",
                                          "德", "拉", "克", "姆", "诺", "维", "奇", "亚"};
            std::string nm;
            nm += kFirst[rng.pick(RngStream::Empire, sizeof(kFirst) / sizeof(kFirst[0]))];
            nm += kLast[rng.pick(RngStream::Empire, sizeof(kLast) / sizeof(kLast[0]))];
            if (rng.chance(RngStream::Empire, Fixed::pct(50))) {
                nm += "·";
                nm += kLast[rng.pick(RngStream::Empire, sizeof(kLast) / sizeof(kLast[0]))];
            }
            e.ruler.name = nm;
            e.ruler.trait = static_cast<RulerTrait>(
                1 + static_cast<int>(rng.pick(RngStream::Empire, static_cast<std::size_t>(RulerTrait::Count) - 1)));
            e.ruler.skill = Fixed::pct(30) + Fixed::pct(static_cast<i64>(rng.range(RngStream::Empire, 0, 50)));
            e.ruler.age = 40 + static_cast<u32>(rng.range(RngStream::Empire, 0, 15));
            e.ruler.reignStart = 0;
            const bool democratic = isElective(e.government);
            e.ruler.elected = democratic;
            e.ruler.termEnd = democratic ? 40u : 0u;
        }
        // 政策：每组设定默认项（自由市场 / 志愿兵役 / 开放社会 / 开放外交 / 透明情报）
        // 此时 e 尚未 push 进 st.empires，直接写入自身字段。
        for (int pg = 0; pg < kPolicyGroupCount; ++pg) {
            auto defaults = policyOptionsIn(static_cast<PolicyGroup>(pg));
            if (defaults.empty()) continue;
            e.policies.active[static_cast<std::size_t>(pg)] = defaults.front()->id;
            e.policies.pending[static_cast<std::size_t>(pg)] = kNoPolicy;
            e.policies.transitionLeft[static_cast<std::size_t>(pg)] = 0;
        }

        // 科研起点与**研究侧重**。
        // 侧重由伦理与立场决定，使各帝国的科技路径产生真实分化：
        // 尚武/扩张 → 物理+工程；商贸 → 社会+计算；科学 → 物理+计算；
        // 信仰 → 社会+生物；孤立 → 工程+灵能。
        for (auto& p : e.tech.progress) p = Fixed(static_cast<i64>(rng.range(RngStream::Empire, 0, 120)));
        for (auto& f2 : e.tech.focus) f2 = Fixed(1);   // 基线
        auto boost = [&](TechBranch b, i64 amount) {
            e.tech.focus[static_cast<std::size_t>(b)] += Fixed(amount);
        };
        for (u8 eth : e.ethics) {
            switch (static_cast<EthicAxis>(eth)) {
                case EthicAxis::Militarism: boost(TechBranch::Physics, 3); boost(TechBranch::Engineering, 2); break;
                case EthicAxis::Commerce: boost(TechBranch::Society, 3); boost(TechBranch::Computing, 2); break;
                case EthicAxis::Science: boost(TechBranch::Computing, 3); boost(TechBranch::Physics, 2); break;
                case EthicAxis::Faith: boost(TechBranch::Society, 2); boost(TechBranch::Biology, 2); break;
                case EthicAxis::Isolation: boost(TechBranch::Engineering, 2); boost(TechBranch::Psionics, 3); break;
                case EthicAxis::Expansion: boost(TechBranch::Engineering, 3); boost(TechBranch::Biology, 2); break;
                case EthicAxis::Ecology: boost(TechBranch::Biology, 3); break;
                case EthicAxis::Liberty: boost(TechBranch::Computing, 2); break;
                case EthicAxis::Order: boost(TechBranch::Society, 2); break;
                case EthicAxis::Collectivism: boost(TechBranch::Psionics, 2); break;
                case EthicAxis::Individualism: boost(TechBranch::Computing, 2); break;
                case EthicAxis::Purity: boost(TechBranch::Biology, 2); break;
                default: break;
            }
        }
        switch (e.stance) {
            case EmpireStance::Expansionist: boost(TechBranch::Engineering, 2); break;
            case EmpireStance::Mercantile: boost(TechBranch::Society, 2); break;
            case EmpireStance::Scholarly: boost(TechBranch::Computing, 2); break;
            case EmpireStance::Zealot: boost(TechBranch::Society, 2); break;
            case EmpireStance::Isolationist: boost(TechBranch::Psionics, 2); break;
            case EmpireStance::Opportunist: boost(TechBranch::Society, 1); boost(TechBranch::Computing, 1); break;
            default: break;
        }
        e.tech.rate = Fixed(1);
        // 情报机构：初始特工数（难度越高越多）
        e.spy.totalAgents = 2 + static_cast<int>(opts.difficulty);
        e.spy.agentPool = e.spy.totalAgents;
        // 议会：席位由派系影响力与政体决定（此时 e 尚未入 st.empires，直接写入自身）
        {
            Fixed totalInf = Fixed(0);
            for (const auto& f : e.domestic.factions) totalInf += f.influence;
            if (totalInf.rawValue() <= 0) totalInf = Fixed(1);
            int assigned = 0;
            for (const auto& f : e.domestic.factions) {
                int seats = static_cast<int>((f.influence / totalInf * Fixed(100)).rawValue() / FIX);
                if (seats < 1) seats = 1;
                e.parliament.seats[static_cast<std::size_t>(f.kind)] = seats;
                assigned += seats;
            }
            e.parliament.totalSeats = assigned > 0 ? assigned : 1;
            e.parliament.capital = Fixed::pct(50);
            switch (e.government) {
                case 9:
                case 11:
                    e.parliament.threshold = VoteThreshold::SuperMajority;
                    break;
                case 2:
                case 3:
                case 5:
                case 8:
                    e.parliament.threshold = VoteThreshold::AbsoluteMajority;
                    break;
                default:
                    e.parliament.threshold = VoteThreshold::SimpleMajority;
                    break;
            }
        }

        st.empires.push_back(std::move(e));
    }

    // 玩家优先保证有可玩起点
    st.empires[0].treasury = Fixed(120000);
    for (int c = 0; c < kCommodityCount; ++c) {
        st.empires[0].stock[static_cast<std::size_t>(c)] = Fixed(2400);
    }
    st.empires[0].stock[static_cast<std::size_t>(Commodity::Alloys)] = Fixed(12400);

    // 首都与近邻归属
    for (int i = 0; i < count; ++i) {
        Empire& e = st.empires[static_cast<std::size_t>(i)];
        if (e.capital < st.map.systems.size()) {
            st.map.systems[e.capital].owner = e.id;
            st.map.systems[e.capital].capital = true;
            st.map.systems[e.capital].colonized = true;
            e.systems.push_back(e.capital);
            for (u32 pl : st.map.systems[e.capital].planets) {
                if (pl < st.planets.size()) {
                    st.planets[pl].owner = e.id;
                    st.planets[pl].colonized = true;
                    st.planets[pl].capital = true;
                    st.planets[pl].pops = static_cast<i64>(rng.range(RngStream::Empire, 800, 4200));
                }
            }
        }
        // 邻近 1~2 个星系殖民
        int extra = 1 + static_cast<int>(rng.range(RngStream::Empire, 0, 1));
        const SystemNode& cap = st.map.systems[e.capital];
        std::vector<std::pair<i64, u32>> cands;
        for (u32 link : cap.links) cands.emplace_back(squaredDist(cap, st.map.systems[link]), link);
        std::sort(cands.begin(), cands.end());
        for (int k = 0; k < extra && k < static_cast<int>(cands.size()); ++k) {
            u32 sys = cands[static_cast<std::size_t>(k)].second;
            if (st.map.systems[sys].owner != kNoEmpire) continue;
            st.map.systems[sys].owner = e.id;
            st.map.systems[sys].colonized = true;
            e.systems.push_back(sys);
            for (u32 pl : st.map.systems[sys].planets) {
                if (pl < st.planets.size() && !st.planets[pl].colonized) {
                    st.planets[pl].owner = e.id;
                    st.planets[pl].colonized = true;
                    st.planets[pl].pops = static_cast<i64>(rng.range(RngStream::Empire, 200, 1200));
                }
            }
        }
    }

    // 舰队
    for (int i = 0; i < count; ++i) {
        Empire& e = st.empires[static_cast<std::size_t>(i)];
        int n = 2 + static_cast<int>(rng.range(RngStream::Empire, 0, 2));
        for (int f = 0; f < n; ++f) {
            Fleet fl;
            fl.id = static_cast<u32>(st.fleets.size());
            fl.name = names.fleet();
            fl.owner = e.id;
            fl.design = static_cast<u32>(rng.pick(RngStream::Empire, e.designs.size()));
            fl.system = e.systems.empty() ? e.capital : e.systems[static_cast<std::size_t>(rng.pick(RngStream::Empire, e.systems.size()))];
            fl.strength = Fixed(static_cast<i64>(rng.range(RngStream::Empire, 80, 260)));
            fl.upkeep = 40 + static_cast<i64>(rng.range(RngStream::Empire, 0, 120));
            // 组织度初始化（由船体等级决定基线；fleetRecoveryPhase 每 tick 会重算上限）
            {
                HullClass hc = fl.design < e.designs.size() ? e.designs[fl.design].hull : HullClass::Corvette;
                fl.maxOrg = Fixed(100) + Fixed(static_cast<i64>(hc)) * Fixed(15);
            }
            fl.org = fl.maxOrg;
            fl.experience = Fixed::raw(static_cast<i64>(rng.range(RngStream::Empire, 0, 60)));
            fl.planning = Fixed(0);
            e.fleets.push_back(fl.id);
            st.fleets.push_back(std::move(fl));
        }
        // 产能按领土重算
        for (int c = 0; c < kCommodityCount; ++c) {
            Fixed cap = Fixed(0);
            for (u32 sys : e.systems) {
                for (u32 pl : st.map.systems[sys].planets) {
                    if (pl < st.planets.size() && st.planets[pl].owner == e.id)
                        cap += st.planets[pl].yield[static_cast<std::size_t>(c)] * Fixed::pct(20);
                }
            }
            e.capacity[static_cast<std::size_t>(c)] = cap;
        }
        // 指挥官：舰队创建之后再生成并绑定（顺序很重要 —— 之前必须在舰队已存在）
        {
            int nCmd = 2 + static_cast<int>(rng.range(RngStream::Empire, 0, 1));
            for (int k = 0; k < nCmd; ++k) {
                CommanderTrait tr = static_cast<CommanderTrait>(
                    1 + rng.pick(RngStream::Empire, static_cast<std::size_t>(CommanderTrait::Count) - 1));
                u32 cid = recruitCommander(st, e.id, names.ruler() + "·" + std::to_string(k + 1), tr);
                if (k < static_cast<int>(e.fleets.size())) {
                    u32 fid = e.fleets[static_cast<std::size_t>(k)];
                    Fleet* fl = st.fleet(fid);
                    Commander* cm = st.commander(cid);
                    if (fl != nullptr && cm != nullptr) {
                        fl->commander = cid;
                        cm->fleet = fid;
                    }
                    // 老牌帝国的军官起点更高
                    if (cm != nullptr) {
                        Fixed startMerit = Fixed(static_cast<i64>(rng.range(RngStream::Empire, 0, 60)));
                        (void)commanderAwardMerit(st, cid, startMerit);
                    }
                }
            }
        }

        e.popTotal = 0;
        for (const auto& pl : st.planets)
            if (pl.owner == e.id && pl.colonized) e.popTotal += pl.pops;
    }
}

}  // namespace gf
