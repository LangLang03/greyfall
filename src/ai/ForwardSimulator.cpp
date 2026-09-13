#include "ai/ToModel.h"
#include "ai/ForwardSimulator.h"

#include <algorithm>

#include "ai/Payoff.h"
#include "combat/Resolver.h"
#include "domain/Empire.h"
#include "mkt/Matching.h"
#include "mkt/OrderBook.h"
#include "util/Str.h"

namespace gf {
namespace {

/// 轻量投影：只复制 rollout 需要的标量，避免整状态拷贝
struct Projection {
    Fixed cash = Fixed(0);
    std::array<Fixed, kCommodityCount> stock{};
    std::array<Fixed, kCommodityCount> price{};
    std::array<Fixed, kCommodityCount> sigma{};
    Fixed military = Fixed(0);
    Fixed influence = Fixed(0);
    Fixed stability = Fixed(0);
    Fixed unity = Fixed(0);
    u32 systems = 0;
    u32 techs = 0;
    Fixed upkeep = Fixed(0);
};

Projection project(const GameState& st, u32 actor) {
    Projection p;
    const Empire* e = st.empire(actor);
    if (e == nullptr) return p;
    p.cash = e->isPlayer ? st.market.margin.cash : e->treasury;
    p.stock = e->stock;
    p.military = e->military;
    p.influence = e->influence;
    p.stability = e->stability;
    p.unity = e->unity;
    p.systems = static_cast<u32>(e->systems.size());
    p.techs = static_cast<u32>(e->tech.completed.size());
    for (u32 fid : e->fleets) {
        const Fleet* f = st.fleet(fid);
        if (f != nullptr) p.upkeep += Fixed(f->upkeep);
    }
    for (int c = 0; c < kCommodityCount; ++c) {
        const Book& b = st.market.exchanges[kExchCX].books[static_cast<std::size_t>(c)];
        p.price[static_cast<std::size_t>(c)] = b.mid.rawValue() > 0 ? b.mid : commodityInfo(c).basePrice;
        p.sigma[static_cast<std::size_t>(c)] = b.sigma;
    }
    return p;
}

/// 简化撮合：给定净需求量，按 sqrt 冲击估算执行价
Fixed simulateExecution(const Projection& p, u8 res, i64 qty, bool buy, Fixed* slippage) {
    const CommodityInfo& ci = commodityInfo(res);
    Fixed ref = p.price[res];
    Fixed v20 = Fixed(std::max<i64>(1, ci.typicalVolume / 20));
    Fixed impact = impactTemporary(p.sigma[res], qty, v20, Fixed(1));
    if (slippage) *slippage = impact;
    return buy ? (ref + ref * impact) : (ref - ref * impact);
}

/// 该帝国是否存在「可投入进攻」的舰队。
///
/// 判据与 `AiCore` 的 Invade 执行保持一致：舰队存在、不在交战中、
/// 组织度不低于上限的一半（组织度不足的舰队留在后方休整）。
/// 用它作为生成候选的前置条件，可以避免「出兵 0 支舰队」这种空动作
/// 占掉候选位并污染日志。
[[nodiscard]] bool hasInvasionForce(const GameState& st, u32 empire) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return false;
    for (u32 fid : e->fleets) {
        const Fleet* f = st.fleet(fid);
        if (f == nullptr) continue;
        if (f->battle != 0xFFFFFFFFu) continue;
        if (f->org.rawValue() < f->maxOrg.rawValue() / 2) continue;
        return true;
    }
    return false;
}

}  // namespace

std::string_view aiActionName(AiActionKind k) {
    switch (k) {
        case AiActionKind::MarketBuy: return "买入建仓";
        case AiActionKind::MarketSell: return "卖出平仓";
        case AiActionKind::FuturesHedge: return "期货对冲";
        case AiActionKind::Hoard: return "囤积现货";
        case AiActionKind::DiploOffer: return "外交示好";
        case AiActionKind::DiploDemand: return "索取贡赋";
        case AiActionKind::DiploThreat: return "武力威胁";
        case AiActionKind::Betray: return "背约";
        case AiActionKind::Embargo: return "封锁";
        case AiActionKind::DeclareWar: return "宣战";
        case AiActionKind::Colonize: return "殖民";
        case AiActionKind::Research: return "研发";
        case AiActionKind::Build: return "建造";
        case AiActionKind::Mega: return "巨构推进";
        case AiActionKind::Propaganda: return "舆论战";
        case AiActionKind::Spy: return "间谍行动";
        case AiActionKind::PowerBalance: return "制衡联盟";
        case AiActionKind::Invade: return "出兵入侵";
        case AiActionKind::Count: break;
    }
    return "?";
}

RolloutResult simulateAction(const GameState& st, u32 actor, const AiAction& a,
                             const IntentPrediction& playerIntent, int horizon, ComputeBudget& budget) {
    RolloutResult r;
    r.horizon = horizon;
    const Empire* e = st.empire(actor);
    if (e == nullptr) {
        r.feasible = false;
        r.reason = "非法主体";
        return r;
    }
    // 计算预算：每次 rollout 消耗 (horizon × 常数)
    const i64 cost = 120 * horizon + 60;
    if (!budget.canAfford(cost)) {
        r.feasible = false;
        r.reason = "计算预算耗尽";
        return r;
    }
    budget.spend(cost);

    Projection p = project(st, actor);
    const ToModel& model = e->mind.playerModel;
    Fixed delta = model.discount;
    Fixed riskAversion = model.riskAversion;

    // 基础效用（t=0）
    PayoffBreakdown base = payoffBreakdown(st, actor, e->mind.playerModel.wGoal);
    Fixed ev = Fixed(0);
    Fixed discountPow = Fixed(1);

    // 玩家会在未来 H 期按预测买入 → 价格被推高，这是 AI 的获利来源
    std::array<Fixed, kCommodityCount> predictedDrift{};
    for (int c = 0; c < kCommodityCount; ++c) {
        Fixed demand = playerIntent.netDemand[static_cast<std::size_t>(c)];
        if (demand.rawValue() <= 0) continue;
        const CommodityInfo& ci = commodityInfo(c);
        Fixed v20 = Fixed(std::max<i64>(1, ci.typicalVolume / 20));
        Fixed impact = impactTemporary(p.sigma[static_cast<std::size_t>(c)], demand.rawValue() / FIX, v20, Fixed(1));
        predictedDrift[static_cast<std::size_t>(c)] = impact * playerIntent.confidence;
    }

    for (int t = 0; t < horizon; ++t) {
        Fixed step = Fixed(0);
        Fixed cash = p.cash;
        std::array<Fixed, kCommodityCount> stock = p.stock;

        // 玩家需求推动价格
        for (int c = 0; c < kCommodityCount; ++c) {
            p.price[static_cast<std::size_t>(c)] =
                p.price[static_cast<std::size_t>(c)] * (Fixed(1) + predictedDrift[static_cast<std::size_t>(c)]);
        }

        switch (a.kind) {
            case AiActionKind::MarketBuy: {
                Fixed slip = Fixed(0);
                Fixed exec = simulateExecution(p, a.res, a.qty, true, &slip);
                Fixed costCash = Fixed::raw(mulDivSat(exec.rawValue(), a.qty, 1));
                if (costCash.rawValue() > cash.rawValue()) {
                    r.feasible = false;
                    r.reason = "现金不足";
                    return r;
                }
                cash -= costCash;
                stock[a.res] += Fixed(a.qty);
                // 玩家未来会以更高价接手
                Fixed futurePrice = p.price[a.res] * (Fixed(1) + predictedDrift[a.res]);
                Fixed gain = Fixed::raw(mulDivSat((futurePrice - exec).rawValue(), a.qty, 1));
                step += gain / Fixed(1000);
                r.tradeGain += gain / Fixed(1000) * discountPow;
                p.price[a.res] = exec * (Fixed(1) + slip);
                break;
            }
            case AiActionKind::MarketSell: {
                Fixed qty = fxMin(Fixed(a.qty), stock[a.res]);
                if (qty.rawValue() <= 0) {
                    r.feasible = false;
                    r.reason = "无可卖库存";
                    return r;
                }
                i64 q = qty.rawValue() / FIX;
                Fixed slip = Fixed(0);
                Fixed exec = simulateExecution(p, a.res, q, false, &slip);
                cash += Fixed::raw(mulDivSat(exec.rawValue(), q, 1));
                stock[a.res] -= Fixed(q);
                step += Fixed::raw(mulDivSat(slip.rawValue(), q, 1)) / Fixed(2000);
                r.tradeGain += step * discountPow;
                p.price[a.res] = exec * (Fixed(1) - slip);
                break;
            }
            case AiActionKind::Hoard: {
                // 囤积：压低当下库存，抬高未来价格
                Fixed futurePrice = p.price[a.res] * (Fixed(1) + predictedDrift[a.res]);
                Fixed now = p.price[a.res];
                step += Fixed::raw(mulDivSat((futurePrice - now).rawValue(), a.qty, 1)) / Fixed(1500);
                r.tradeGain += step * discountPow;
                break;
            }
            case AiActionKind::FuturesHedge: {
                const FuturesQuote& f =
                    st.market.exchanges[kExchCX].futures[a.res][std::min<int>(3, std::max(0, static_cast<int>(a.qty % 4)))];
                Fixed spot = p.price[a.res];
                Fixed gain = (spot - f.price) * Fixed(std::max<i64>(1, std::abs(a.qty))) / Fixed(1000);
                step += gain;
                r.tradeGain += gain * discountPow;
                break;
            }
            case AiActionKind::DiploOffer: {
                const Empire* tgt = st.empire(a.target);
                if (tgt != nullptr) {
                    Fixed gain = a.terms / Fixed(1000);
                    step += gain;
                    r.tradeGain += gain * discountPow;
                }
                break;
            }
            case AiActionKind::DiploDemand:
            case AiActionKind::DiploThreat: {
                const Empire* tgt = st.empire(a.target);
                if (tgt != nullptr) {
                    Fixed strength = p.military / (tgt->military + Fixed(1));
                    Fixed gain = fxClamp(strength, Fixed(0), Fixed(3)) * a.terms / Fixed(500);
                    step += gain;
                    r.betrayalGain += gain * discountPow;
                }
                break;
            }
            case AiActionKind::Betray: {
                // 背叛收益：夺取目标的库存与影响力
                const Empire* tgt = st.empire(a.target);
                if (tgt != nullptr) {
                    Fixed grab = Fixed(0);
                    for (const auto& s : tgt->stock) grab += s;
                    grab = grab / Fixed(200);
                    step += grab;
                    r.betrayalGain += grab * discountPow;
                }
                break;
            }
            case AiActionKind::Embargo: {
                const Empire* tgt = st.empire(a.target);
                if (tgt != nullptr) {
                    step += Fixed::pct(6);
                    r.betrayalGain += Fixed::pct(6) * discountPow;
                }
                break;
            }
            case AiActionKind::DeclareWar: {
                Fixed odds = combatOdds(st, actor, a.target);
                step += odds * Fixed(4) - Fixed(2);
                r.betrayalGain += (odds * Fixed(4)) * discountPow;
                break;
            }
            case AiActionKind::Colonize:
                step += Fixed::pct(12);
                break;
            case AiActionKind::Research:
                step += Fixed::pct(8);
                break;
            case AiActionKind::Build: {
                // 建造的收益随「闲置资金」上升：
                // 国库充裕却不投资 => 资金毫无价值（实测 AI 曾囤到 19.6 万仍不建造）。
                // 用收入倍数衡量闲置程度，使「有钱就该花在产能上」成为自然选择。
                Fixed idle = e->lastIncome.rawValue() > 0
                                 ? e->treasury / e->lastIncome
                                 : Fixed(0);
                step += Fixed::pct(10) + fxClamp(idle, Fixed(0), Fixed(5)) * Fixed::pct(6);
                break;
            }
            case AiActionKind::Mega:
                step += Fixed::pct(18);
                break;
            case AiActionKind::Propaganda: {
                Fixed gain = Fixed::pct(5) * (Fixed(1) + p.influence / Fixed(1000));
                step += gain;
                break;
            }
            case AiActionKind::Spy: {
                Fixed gain = Fixed::pct(7);
                step += gain;
                break;
            }
            case AiActionKind::PowerBalance: {
                step += Fixed::pct(9);
                break;
            }
            case AiActionKind::Invade: {
                // 入侵收益：胜算 × 领土价值。夺取星系是唯一的胜利路径
                //（胜利条件要求击败所有对手），因此权重显著高于内政行动。
                // 早期用 odds*8-2，在 43% 胜算下只有 +1.4，低于建造的 +0.4~0.7 增量，
                // AI 于是永远选择内政而非出兵。
                Fixed odds = combatOdds(st, actor, a.target);
                Fixed reward = odds * Fixed(60) - Fixed(8);
                step += reward;
                r.betrayalGain += reward * discountPow;
                break;
            }
            case AiActionKind::Count:
                break;
        }

        // 玩家可能的报复：随玩家军事与"报复型"后验上升
        Fixed retaliationRisk = Fixed(0);
        if (a.kind == AiActionKind::Betray || a.kind == AiActionKind::DeclareWar ||
            a.kind == AiActionKind::Embargo || a.kind == AiActionKind::DiploDemand) {
            Fixed playerMil = st.empires.empty() ? Fixed(0) : st.empires[kPlayerId].military;
            Fixed typeWeight = model.typeBelief[static_cast<std::size_t>(ActorType::Retaliate)];
            retaliationRisk = (playerMil / Fixed(400)) * (Fixed::pct(30) + typeWeight * Fixed(2));
            // 读档者会重试 → 报复更可能发生
            retaliationRisk = retaliationRisk * (Fixed(1) + Fixed(static_cast<i64>(st.rollbackCount)) * Fixed::pct(25));
        }
        // 结盟报复：仅对「新的敌对行为」计罚。
        // 入侵（Invade）的前提是双方**已经交战** —— 结盟代价在宣战时就已经付过，
        // 再计一次会让入侵的 EV 恒低于建造/研发（实测 53.9 vs 63.7），
        // AI 因此从不出兵，战争永远打不起来。
        Fixed alliance = Fixed(0);
        if (a.kind != AiActionKind::Invade) alliance = payoffAllianceRetaliation(st, actor, a.target);
        r.retaliation += (retaliationRisk + alliance) * discountPow;
        step -= retaliationRisk + alliance;

        // 运维成本
        step -= p.upkeep / Fixed(4000);

        ev += step * discountPow;
        discountPow = discountPow * delta;
    }

    r.ownUtility = base.total;
    r.riskPenalty = riskAversion * payoffVaR(st, actor, 10);
    r.ev = ev + base.total * Fixed::pct(20) - r.riskPenalty;
    return r;
}

std::vector<AiAction> generateCandidates(const GameState& st, u32 actor, const IntentPrediction& playerIntent,
                                        const ComputeBudget& budget) {
    std::vector<AiAction> out;
    const Empire* e = st.empire(actor);
    if (e == nullptr) return out;

    // 1) 针对玩家预测需求的建仓（front-running 的核心）
    std::vector<std::pair<Fixed, int>> ranked;
    for (int c = 0; c < kCommodityCount; ++c) {
        if (playerIntent.netDemand[static_cast<std::size_t>(c)].rawValue() <= 0) continue;
        ranked.emplace_back(playerIntent.urgency[static_cast<std::size_t>(c)], c);
    }
    std::sort(ranked.begin(), ranked.end(),
              [](const auto& x, const auto& y) { return x.first.rawValue() > y.first.rawValue(); });
    int added = 0;
    for (const auto& kv : ranked) {
        if (added >= 3) break;
        int c = kv.second;
        const CommodityInfo& ci = commodityInfo(c);
        AiAction a;
        a.kind = AiActionKind::MarketBuy;
        a.res = static_cast<u8>(c);
        a.qty = std::max<i64>(1, ci.typicalVolume / 300);
        a.px = st.market.exchanges[kExchCX].books[static_cast<std::size_t>(c)].mid;
        a.desc = "抢在玩家需求前建仓 " + std::string(commodityName(c)) + " ×" + std::to_string(a.qty) +
                 "（预测置信 " + fixedStrPlain(playerIntent.confidence, 2) + "）";
        out.push_back(std::move(a));
        ++added;
    }

    // 2) 外交示好（若玩家模型判定为合作型）
    ActorType type = toModelDominantType(e->mind.playerModel);
    if (type == ActorType::Cooperate || e->mind.playerModel.modelConfidence.rawValue() < Fixed::pct(40).rawValue()) {
        AiAction a;
        a.kind = AiActionKind::DiploOffer;
        a.target = kPlayerId;
        a.terms = Fixed::pct(10);
        a.desc = "向玩家示好（换取未来贸易优先权）";
        out.push_back(std::move(a));
    }

    // 3) 索取贡赋（若判定玩家可欺）
    if (e->mind.playerModel.modelConfidence.rawValue() > Fixed::pct(55).rawValue() &&
        e->military.rawValue() > st.empires[kPlayerId].military.rawValue()) {
        AiAction a;
        a.kind = AiActionKind::DiploDemand;
        a.target = kPlayerId;
        a.terms = Fixed::pct(15);
        a.desc = "向玩家索取贡赋（判定其可欺）";
        out.push_back(std::move(a));
    }

    // 4) 制衡（若玩家国力过高）
    Fixed myPower = e->powerIndex();
    Fixed playerPower = st.empires[kPlayerId].powerIndex();
    if (playerPower.rawValue() > myPower.rawValue() * Fixed::raw(1300).rawValue() / FIX) {
        AiAction a;
        a.kind = AiActionKind::PowerBalance;
        a.target = kPlayerId;
        a.desc = "组建遏制联盟（平衡玩家霸权）";
        out.push_back(std::move(a));
    }

    // 4.5) 出兵入侵：只要处于战争状态，就评估是否进攻敌方星系
    //
    // 两道闸门（否则战争退化为无意义的每季重复刷屏）：
    //   ① 入侵冷却：一次真实入侵后 6 季内不再生成候选
    //   ② 兵力可行性：没有任何「可投入的舰队」时不生成候选 ——
    //      原先无舰队也会生成，`AiCore` 于是每 tick 打印
    //      「出兵 0 支舰队进攻 X」，并且白白占掉一个候选位。
    constexpr u64 kInvadeCooldownQuarters = 6;
    const bool invadeReady = st.tick >= e->lastInvadeTick + kInvadeCooldownQuarters;
    if (invadeReady && hasInvasionForce(st, actor)) {
    for (const auto& other : st.empires) {
        if (other.id == actor || !other.alive) continue;
        if (!atWarWith(st, actor, other.id)) continue;
        if (other.systems.empty()) continue;
        // 找与我方接壤的敌方星系；没有接壤则打最靠近首都的
        u32 target = other.systems.front();
        Fixed bestScore = Fixed(-1);
        for (u32 sid : other.systems) {
            const SystemNode* s = st.system(sid);
            if (s == nullptr) continue;
            // 接壤优先
            bool adjacent = false;
            for (u32 mine : e->systems) {
                const SystemNode* ms = st.system(mine);
                if (ms == nullptr) continue;
                for (u32 l : ms->links)
                    if (l == sid) adjacent = true;
            }
            Fixed score = adjacent ? Fixed(100) : Fixed(50);
            // 防守薄弱的优先
            score += Fixed(1000) / (systemDefense(st, sid) + Fixed(1));
            if (score.rawValue() > bestScore.rawValue()) {
                bestScore = score;
                target = sid;
            }
        }
        AiAction a;
        a.kind = AiActionKind::Invade;
        a.target = other.id;
        a.qty = static_cast<i64>(target);
        a.desc = "出兵进攻 " + other.name + " 的 " +
                 std::string(st.system(target) ? st.system(target)->name : "?");
        out.push_back(std::move(a));
        break;
    }
    }

    // 5) 常规发展
    {
        AiAction a;
        a.kind = AiActionKind::Research;
        a.desc = "推进研究";
        out.push_back(a);
    }
    {
        AiAction a;
        a.kind = AiActionKind::Build;
        a.desc = "推进建造";
        out.push_back(a);
    }

    // 6) 封锁 / 宣战（低概率，仅在关系恶劣时）
    for (const auto& other : st.empires) {
        if (other.id == actor || !other.alive) continue;
        const Relation& rel = st.relation(actor, other.id);
        if (rel.opinion.rawValue() > -Fixed::pct(30).rawValue()) continue;
        AiAction a;
        a.kind = AiActionKind::Embargo;
        a.target = other.id;
        a.desc = "对 " + other.name + " 实施封锁";
        out.push_back(std::move(a));
        break;
    }

    // top-K 剪枝
    int k = std::max(2, budget.topK);
    if (static_cast<int>(out.size()) > k) out.resize(static_cast<std::size_t>(k));
    return out;
}

AiAction chooseBestAction(const GameState& st, u32 actor, const IntentPrediction& playerIntent,
                          ComputeBudget& budget, RolloutResult* outResult) {
    std::vector<AiAction> candidates = generateCandidates(st, actor, playerIntent, budget);
    AiAction best;
    RolloutResult bestR;
    bestR.ev = Fixed::raw(std::numeric_limits<i64>::min() / 4);
    int horizon = std::max(1, static_cast<int>(budget.foresight) + 1);
    for (const auto& a : candidates) {
        if (!budget.canAfford(120 * horizon + 60)) break;
        RolloutResult r = simulateAction(st, actor, a, playerIntent, horizon, budget);
        if (!r.feasible) continue;
        if (r.ev.rawValue() > bestR.ev.rawValue()) {
            bestR = r;
            best = a;
        }
    }
    if (outResult != nullptr) *outResult = bestR;
    return best;
}

}  // namespace gf
