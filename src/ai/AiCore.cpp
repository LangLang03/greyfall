#include "util/Fmt.h"
#include "ai/AiCore.h"
#include "domain/Trade.h"
#include "domain/Building.h"
#include "domain/Construction.h"
#include "domain/Planet.h"

#include <algorithm>

#include "ai/BetrayalCalculus.h"
#include "ai/FactionAI.h"
#include "ai/Negotiation.h"
#include "ai/FraudDetect.h"
#include "ai/Payoff.h"
#include "ai/PowerBalancing.h"
#include "ai/Reputation.h"
#include "ai/ToModel.h"
#include "combat/Resolver.h"
#include "core/TickPipeline.h"
#include "domain/Treaty.h"
#include "gen/EmpireGen.h"
#include "mkt/Futures.h"
#include "mkt/MarketEngine.h"
#include "mkt/Insider.h"
#include "mkt/OrderBook.h"
#include "util/Str.h"

namespace gf {

ReaderCache& readerCache(GameState& st) { return st.reader; }

void aiReadPlayer(GameState& st) {
    ReaderCache& cache = readerCache(st);
    Observable obs = omniscientSnapshot(st, kPlayerId);

    // 记录暴露路径（日志可复盘）
    int forged = 0;
    for (const auto& f : obs.exposure) {
        if (f.forged) ++forged;
    }
    st.logEvent(LogPhase::Reader, "reader.snapshot",
                "泛视网络快照：覆盖率 " + fixedStrPlain(obs.coverage * Fixed(100), 1) + "%，字段 " +
                    std::to_string(obs.exposure.size()) + " 个" +
                    (forged > 0 ? "，其中 " + std::to_string(forged) + " 个被布置（伪造）" : "") +
                    "；rollbackCount=" + std::to_string(obs.rollbackCount),
                kPlayerId);

    cache.prev[kPlayerId] = obs;
    cache.valid[kPlayerId] = true;

    // 每个 AI 也读取其他主体（用于信誉与制衡）
    for (const auto& e : st.empires) {
        if (e.isPlayer || !e.alive) continue;
        cache.prev[e.id] = omniscientSnapshot(st, e.id);
        cache.valid[e.id] = true;
    }
}

void aiUpdatePlayerModel(GameState& st) {
    ReaderCache& cache = readerCache(st);
    const Observable& obs = cache.prev[kPlayerId];

    for (auto& e : st.empires) {
        if (e.isPlayer || !e.alive) continue;
        ComputeBudget modelBudget = ComputeBudget::forDifficulty(st.difficulty);
        modelBudget.reset(e.mind.nodeBudget, e.mind.foresight);
        (void)modelBudget;

        // 1) FraudDetect：一致性异常打分
        Observable empty;
        const Observable& prev = cache.prev[e.id].subject == e.id ? cache.prev[e.id] : empty;
        std::vector<FraudFinding> findings = fraudDetect(st, e.id, kPlayerId, obs, empty);
        (void)prev;
        Fixed fraud = fraudOverallScore(findings);
        // 只有「综合评分达到门槛」才记录并注入噪声。
        // 早期条件是 !findings.empty()，任何一条轻微异常都触发 ——
        // 7 个 AI 每季各写多条，日志被彻底淹没。
        if (fraud.rawValue() >= Fixed::pct(12).rawValue()) {
            fraudLog(st, e.id, findings);
            // 被布置的数据污染模型
            toModelInjectNoise(e.mind.playerModel, fraud * Fixed::pct(30), fraud * Fixed::pct(40));
        }

        // 2) 主要观测者（国力最强者）更新玩家模型
        bool primary = true;
        for (const auto& other : st.empires) {
            if (other.id == e.id || other.isPlayer || !other.alive) continue;
            if (other.powerIndex().rawValue() > e.powerIndex().rawValue()) {
                primary = false;
                break;
            }
        }
        {
            // 全体 AI 都更新心智模型；国力最强者（primary）学得更快
            Fixed eta = primary ? (Fixed::pct(12) + Fixed::bp(modelBudget.foresight * 200)) : Fixed::pct(5);
            std::array<Fixed, kGoalDim> grad = actionGradient(st, obs, obs);
            toModelUpdate(e.mind.playerModel, grad, eta);
            toModelUpdateType(e.mind.playerModel, obs, st, Fixed::pct(10));
            e.mind.playerModel.riskAversion = fxLerp(e.mind.playerModel.riskAversion, toModelInferRisk(st, obs),
                                                     primary ? Fixed::pct(15) : Fixed::pct(6));
            e.mind.playerModel.discount = fxLerp(e.mind.playerModel.discount, toModelInferDiscount(st, obs),
                                                 primary ? Fixed::pct(15) : Fixed::pct(6));
            toModelUpdateHabits(e.mind.playerModel, obs, st);
        }
        if (primary) {
            st.logEvent(LogPhase::Model, kLogModel,
                        e.name + " 更新了对你的心智模型：类型判定【" +
                            std::string(actorTypeName(toModelDominantType(e.mind.playerModel))) + "】，置信度 " +
                            fixedStrPlain(e.mind.playerModel.modelConfidence, 2) + "，θ 观测样本 " +
                            std::to_string(e.mind.playerModel.observations),
                        e.id, e.mind.playerModel.modelConfidence);
        }

        // 3) 信誉更新：观察玩家的行为
        Fixed playerBehavior = Fixed(0);
        for (const auto& t : st.treaties) {
            if (t.a != kPlayerId && t.b != kPlayerId) continue;
            if (t.kind == TreatyKind::TradePact) playerBehavior += Fixed::pct(1);
            if (t.kind == TreatyKind::DefensivePact) playerBehavior += Fixed::pct(2);
        }
        if (st.rollbackCount > 0) playerBehavior -= Fixed::pct(3) * Fixed(static_cast<i64>(st.rollbackCount));
        if (st.chronicleBurned) playerBehavior -= Fixed::pct(20);
        reputationUpdate(st, e.id, kPlayerId, playerBehavior);

        // 4) 威胁评估
        Fixed threat = st.empires[kPlayerId].powerIndex() - e.powerIndex();
        threat += e.mind.playerModel.modelConfidence * Fixed(2);
        if (threat.rawValue() > 0) e.mind.threat[kPlayerId] = fxClamp(e.mind.threat[kPlayerId] + threat * Fixed::pct(2),
                                                                     Fixed(0), Fixed(1));
        else e.mind.threat[kPlayerId] = e.mind.threat[kPlayerId] * Fixed::pct(97);
    }
}

void aiTakeActions(GameState& st, TickReport& rep) {
    ReaderCache& cache = readerCache(st);
    opinionPhase(st);

    for (auto& e : st.empires) {
        if (e.isPlayer || !e.alive) continue;
        ComputeBudget budget = ComputeBudget::forDifficulty(st.difficulty);
        budget.reset(e.mind.nodeBudget, e.mind.foresight);

        // 1) 预测玩家意图（top-M 策略路径）
        IntentPrediction intent = predictIntent(st, kPlayerId, budget);
        cache.intent[e.id] = intent;
        if (e.id % 3 == st.tick % 3) intentLog(st, e.id, intent);

        // 2) 前瞻搜索选动作
        RolloutResult result;
        AiAction best = chooseBestAction(st, e.id, intent, budget, &result);
        if (best.desc.empty()) continue;

        // 3) 背叛 EV 评估（对玩家）
        BetrayalEV ev = betrayalCalculus(st, e.id, kPlayerId);
        if (ev.shouldBetray) {
            betrayalLog(st, e.id, kPlayerId, ev);
            AiAction betray;
            betray.kind = AiActionKind::Betray;
            betray.target = kPlayerId;
            betray.desc = "背约（EV " + fixedStrSigned(ev.total, 2) + " > 阈值 " + fixedStr(ev.threshold, 2) + "）";
            aiExecuteAction(st, e.id, betray, rep);
            ++rep.aiActions;
            continue;
        }
        betrayalLog(st, e.id, kPlayerId, ev);

        // AI 主动谈判：只有在谈判权重超过阈值时才会发起（默认不愿意谈）。
        // 权重来自领土、经济、制裁、战争与军事劣势的累积。
        if (st.rng.chance(RngStream::Diplo, Fixed::pct(12))) {
            NegotiationWeight nw = negotiationWeight(st, e.id, kPlayerId);
            if (nw.willing) {
                NegotiationTerms terms;
                Term t;
                t.kind = TermKind::Credits;
                // 索价随权重上升
                t.amount = 5000 + static_cast<i64>(nw.total.rawValue()) * 30 / 1000;
                if (st.empires[kPlayerId].treasury.rawValue() / FIX > t.amount) {
                    terms.demand.push_back(t);
                    NegotiationResult nr = negotiate(st, e.id, kPlayerId, terms);
                    if (nr.accepted) {
                        st.logEvent(LogPhase::Model, kLogEnvoy,
                                    e.name + " 主动发起谈判并索取 " + groupDigits(t.amount) + " cr：" + nr.reason,
                                    e.id, Fixed(t.amount));
                        ++rep.aiActions;
                        continue;
                    }
                }
            }
        }

        aiExecuteAction(st, e.id, best, rep);
        ++rep.aiActions;
        // 常规发展类动作（研发/建造）不值得每 tick 记录 —— 只记有对外影响的动作，
        // 保证日志里能看清「谁对谁做了什么」。
        bool noteworthy = best.kind != AiActionKind::Research && best.kind != AiActionKind::Build &&
                          best.kind != AiActionKind::Hoard;
        if (noteworthy) {
            st.logEvent(LogPhase::Ai, "ai.action",
                        e.name + " 执行【" + std::string(aiActionName(best.kind)) + "】" + best.desc +
                            "  EV=" + fixedStrSigned(result.ev, 2) + "（下行惩罚 " +
                            fixedStr(result.riskPenalty, 2) + "，报复风险 " + fixedStr(result.retaliation, 2) +
                            "，计算节点 " + std::to_string(budget.nodeSpent) + "/" +
                            std::to_string(budget.nodeBudget) + "）",
                        e.id, result.ev);
        }
    }

    // 4) 制衡联盟（全局）
    powerBalancingPhase(st);
}

void aiExecuteAction(GameState& st, u32 actor, const AiAction& a, TickReport& rep) {
    Empire* e = st.empire(actor);
    if (e == nullptr) return;
    switch (a.kind) {
        case AiActionKind::MarketBuy: {
            OrderRequest req;
            req.owner = actor;
            req.res = a.res;
            req.exch = kExchCX;
            req.buy = true;
            req.qty = a.qty;
            req.kind = OrderKind::Limit;
            req.tif = Tif::Gtc;
            Fixed mid = st.market.exchanges[kExchCX].books[a.res].mid;
            req.px = mid + fxMax(mid * Fixed::bp(20), Fixed::raw(1));
            (void)marketSubmitOrder(st, req);
            // AI 也会在期货市场对冲
            if (st.rng.chance(RngStream::Ai, Fixed::pct(30))) {
                (void)futuresOpen(st, actor, a.res, static_cast<u8>(st.rng.pick(RngStream::Ai, kFuturesTerms)),
                          a.qty, false, 2, nullptr);
            }
            break;
        }
        case AiActionKind::MarketSell: {
            OrderRequest req;
            req.owner = actor;
            req.res = a.res;
            req.exch = kExchCX;
            req.buy = false;
            req.qty = a.qty;
            req.kind = OrderKind::Limit;
            req.tif = Tif::Gtc;
            Fixed mid = st.market.exchanges[kExchCX].books[a.res].mid;
            req.px = fxMax(mid - mid * Fixed::bp(20), Fixed::raw(1));
            (void)marketSubmitOrder(st, req);
            break;
        }
        case AiActionKind::DiploOffer: {
            Relation& rel = st.relation(a.target, actor);
            rel.opinion = fxClamp(rel.opinion + Fixed::pct(6), Fixed(-1), Fixed(1));
            e->opinion[a.target] = rel.opinion;
            Treaty t;
            t.kind = TreatyKind::TradePact;
            t.a = actor;
            t.b = a.target;
            t.signedTick = st.tick;
            t.expireTick = static_cast<i64>(st.tick) + 12;
            t.terms = a.terms;
            upsertTreaty(st.treaties, t);
            st.logEvent(LogPhase::Model, kLogTreaty, e->name + " 提议贸易协定", actor);
            break;
        }
        case AiActionKind::DiploDemand: {
            Empire& p = st.empires[kPlayerId];
            Fixed demand = p.treasury * a.terms;
            if (demand.rawValue() > 0) {
                // 玩家被迫让步（AI 判断可欺）
                p.treasury -= demand;
                st.market.margin.cash = p.treasury;
                e->treasury += demand;
                st.logEvent(LogPhase::Model, kLogEnvoy,
                            e->name + " 索取贡赋 " + fixedStr(demand, 0) + " cr（判定你可欺，置信 " +
                                fixedStrPlain(e->mind.playerModel.modelConfidence, 2) + "）",
                            actor, demand);
                for (auto& other : st.empires)
                    if (other.id != actor && other.id != kPlayerId) other.addOpinion(kPlayerId, Fixed(-1) * (Fixed::pct(2)));
            }
            break;
        }
        case AiActionKind::Betray: {
            Empire& p = st.empires[kPlayerId];
            // 背约：夺取一部分库存与影响力，摧毁条约
            for (int c = 0; c < kCommodityCount; ++c) {
                Fixed take = p.stock[static_cast<std::size_t>(c)] * Fixed::pct(12);
                p.stock[static_cast<std::size_t>(c)] -= take;
                e->stock[static_cast<std::size_t>(c)] += take;
            }
            Fixed inf = p.influence * Fixed::pct(15);
            p.influence -= inf;
            e->influence += inf;
            // 关系恶化
            Relation& rel = st.relation(kPlayerId, actor);
            rel.opinion = fxClamp(rel.opinion - Fixed::pct(60), Fixed(-1), Fixed(1));
            declareWar(st, actor, kPlayerId, st.rng.chance(RngStream::Ai, Fixed::pct(45)));
            e->betrayalsCommitted += 1;
            p.betrayalsSuffered += 1;
            // 删除双方条约
            st.treaties.erase(std::remove_if(st.treaties.begin(), st.treaties.end(),
                                             [&](const Treaty& t) {
                                                 return (t.a == actor && t.b == kPlayerId) ||
                                                        (t.b == actor && t.a == kPlayerId);
                                             }),
                              st.treaties.end());
            // 全体信誉惩罚
            reputationUpdateAll(st, actor, -Fixed::pct(25), kPlayerId);
            st.logEvent(LogPhase::Ai, kLogBetrayal,
                        "【背约】" + e->name + " 撕毁全部条约并夺取你 12% 的库存与 15% 的影响力（EV 阈值被突破）",
                        actor);
            rep.eventsFired += 1;
            break;
        }
        case AiActionKind::Embargo: {
            Relation& rel = st.relation(actor, a.target);
            rel.embargo = true;
            rel.opinion = fxClamp(rel.opinion - Fixed::pct(15), Fixed(-1), Fixed(1));
            st.market.exchanges[kExchFX].embargo = fxClamp(st.market.exchanges[kExchFX].embargo + Fixed::pct(10),
                                                           Fixed(0), Fixed(1));
            st.logEvent(LogPhase::Market, "diplo.embargo", e->name + " 对目标实施封锁（边疆运费与关税上升）",
                        actor);
            break;
        }
        case AiActionKind::DeclareWar: {
            declareWar(st, actor, a.target, true);
            e->lastWarTick = static_cast<u32>(st.tick);
            st.logEvent(LogPhase::Combat, kLogWar, e->name + " 向目标宣战", actor);
            rep.eventsFired += 1;
            break;
        }
        case AiActionKind::Invade: {
            // 把可用舰队派往目标星系
            const Empire* foe = st.empire(a.target);
            if (foe == nullptr || foe->systems.empty()) break;
            u32 dest = static_cast<u32>(a.qty);
            if (st.system(dest) == nullptr) dest = foe->systems.front();
            int sent = 0;
            for (u32 fid : e->fleets) {
                Fleet* f = st.fleet(fid);
                if (f == nullptr) continue;
                // 组织度不足的舰队留在后方休整
                if (f->org.rawValue() < f->maxOrg.rawValue() / 2) continue;
                if (f->battle != 0xFFFFFFFFu) continue;
                f->targetSystem = dest;
                f->order = FleetOrder::Engage;
                ++sent;
            }
            st.logEvent(LogPhase::Combat, kLogWar,
                        e->name + " 出兵 " + std::to_string(sent) + " 支舰队进攻 " +
                            std::string(st.system(dest) ? st.system(dest)->name : "?"),
                        actor);
            if (sent == 0) {
                // 无可用舰队：回防休整
                for (u32 fid : e->fleets) {
                    Fleet* f = st.fleet(fid);
                    if (f != nullptr && f->battle == 0xFFFFFFFFu) f->order = FleetOrder::Patrol;
                }
            }
            break;
        }
        case AiActionKind::Colonize: {
            if (e->apLeft < apcost::kColony) break;
            for (const auto& sys : st.map.systems) {
                if (sys.owner != kNoEmpire) continue;
                std::string message;
                if (startColony(st, actor, sys.id, &message)) { e->apLeft -= apcost::kColony; break; }
            }
            break;
        }
        case AiActionKind::Research:
            // 研究已改为持续性立项（见 aiResearchPhase），不再作为一次性行动。
            // 保留该分支为空壳，是为了兼容既有的行动枚举与回放记录。
            break;
        case AiActionKind::Build: {
            // 早期 Build 与 Research 合并，只做研究、**从不建造** ——
            // 结果是全世界 240 季零建筑，AI 国库一路囤到 19.6 万却无处分花，
            // 而建筑系统（产出/研究/防御）对 AI 完全失效。
            (void)aiConstructBest(st, e->id);
            break;
        }
        case AiActionKind::Mega: {
            for (int definition = 0; definition < kMegastructureCount; ++definition) {
                const auto& info = megastructureInfo(definition);
                if (e->apLeft < info.apCost) continue;
                u32 target = e->capital;
                for (const auto& m : e->megas) if (m.defId == definition) target = m.system;
                std::string message;
                if (startMegaStage(st, actor, definition, target, &message)) { e->apLeft -= info.apCost; break; }
            }
            break;
        }
        case AiActionKind::Propaganda: {
            e->propaganda += Fixed::pct(5);
            // 削弱玩家观感
            for (auto& other : st.empires) {
                if (other.id == actor || other.id == kPlayerId) continue;
                other.addOpinion(kPlayerId, Fixed(-1) * (Fixed::pct(3)));
            }
            st.logEvent(LogPhase::Model, "diplo.propaganda", e->name + " 在第三方散布关于你的叙事", actor);
            break;
        }
        case AiActionKind::Spy: {
            // 窃取玩家线索或制造伪造
            if (!st.clues.empty()) {
                u16 target = static_cast<u16>(st.rng.pick(RngStream::Ai, st.clues.size()));
                st.clues[target].credibility = st.clues[target].credibility * Fixed::pct(85);
            }
            st.logEvent(LogPhase::Model, kLogSpy, e->name + " 对你发动了间谍行动（线索可信度受损）", actor);
            break;
        }
        case AiActionKind::PowerBalance: {
            // 由 powerBalancingPhase 统一处理
            break;
        }
        case AiActionKind::Hoard: {
            e->stock[a.res] += Fixed(a.qty);
            break;
        }
        case AiActionKind::FuturesHedge: {
            (void)futuresOpen(st, actor, a.res, static_cast<u8>(st.rng.pick(RngStream::Ai, kFuturesTerms)),
                              a.qty, true, 3, nullptr);
            break;
        }
        case AiActionKind::DiploThreat: {
            Relation& rel = st.relation(a.target, actor);
            rel.fear = fxClamp(rel.fear + Fixed::pct(10), Fixed(0), Fixed(1));
            break;
        }
        case AiActionKind::Count:
            break;
    }
}

std::string threatReport(const GameState& st, u32 observer) {
    const Empire* o = st.empire(observer);
    if (o == nullptr) return "非法主体";
    std::string out;
    out += "═══ 威胁评估：" + o->name + " 眼中的你 ═══\n";
    out += "威胁值 " + bar(o->mind.threat[kPlayerId], 20) + " " +
           fixedStrPlain(o->mind.threat[kPlayerId] * Fixed(100), 1) + "%\n";
    out += "对你的信誉 " + bar(o->mind.reputation[kPlayerId], 20) + " " +
           fixedStrPlain(o->mind.reputation[kPlayerId] * Fixed(100), 1) + "%\n";
    out += "怨恨残留 " + bar(o->mind.grudge[kPlayerId], 20) + "\n";
    out += "心智模型置信度 " + fixedStrPlain(o->mind.playerModel.modelConfidence, 2) + "    前瞻深度 " +
           std::to_string(o->mind.foresight) + " 季    计算预算 " + std::to_string(o->mind.nodeBudget) + "\n\n";
    out += "最近的背叛 EV 分解：\n  " + (o->mind.lastBetrayalReason.empty() ? "（尚未评估）" : o->mind.lastBetrayalReason) +
           "\n\n";
    out += coalitionReport(st, kPlayerId);
    return out;
}

std::string spyReport(const GameState& st, u32 target, int budget) {
    const Empire* t = st.empire(target);
    if (t == nullptr) return "非法目标";
    std::string out;
    out += "═══ 情报报告：" + t->name + " ═══\n";
    out += "预算投入 " + std::to_string(budget) + " → 情报深度 " +
           std::string(budget >= 6 ? "完整" : (budget >= 3 ? "部分" : "粗略")) + "\n\n";
    out += "国库 " + fixedStr(t->treasury, 0) + " cr    国力指数 " + fixedStr(t->powerIndex(), 2) + "\n";
    out += "军事 " + fixedStr(t->military, 0) + "    经济 " + fixedStr(t->economy, 0) + "    稳定 " +
           fixedStrPlain(t->stability, 2) + "\n";
    out += "信用评级 " + fixedStrPlain(t->creditRating, 2) + "    影响力 " + fixedStr(t->influence, 0) + "\n";
    out += "舰队 " + std::to_string(t->fleets.size()) + " 支    星系 " + std::to_string(t->systems.size()) +
           " 个    巨构 " + std::to_string(t->megas.size()) + " 项\n";
    if (budget >= 3) {
        out += "\n资产（部分）：\n";
        for (int c = 0; c < kCommodityCount; ++c) {
            i64 q = t->stock[static_cast<std::size_t>(c)].rawValue() / FIX;
            if (q < 1000) continue;
            out += "  " + padRight(std::string(commodityName(c)), 12) + groupDigits(q) + "\n";
        }
    }
    if (budget >= 6) {
        out += "\n对玩家的真实态度：\n";
        out += "  观感 " + fixedStrPlain(t->opinion[kPlayerId], 2) + "    威胁 " +
               fixedStrPlain(t->mind.threat[kPlayerId], 2) + "\n";
        out += "  类型判定：" + std::string(actorTypeName(toModelDominantType(t->mind.playerModel))) + "\n";
        out += "  当前背叛 EV：" + fixedStr(-t->mind.lastBetrayalEV, 2) + "（正值表示它正在考虑动手）\n";
    }
    out += "\n" + insiderReport(st, target);
    return out;
}

// ---------------------------------------------------------------------------
// AI 建造
// ---------------------------------------------------------------------------
namespace {

/// 建筑效果 → 它补的是哪种商品（非产出类返回 -1）
int effectCommodity(BuildingEffect ef) {
    switch (ef) {
        case BuildingEffect::ProdEnergy: return static_cast<int>(Commodity::Energy);
        case BuildingEffect::ProdMinerals: return static_cast<int>(Commodity::Minerals);
        case BuildingEffect::ProdFood: return static_cast<int>(Commodity::Food);
        case BuildingEffect::ProdMedicines: return static_cast<int>(Commodity::Medicines);
        case BuildingEffect::ProdAlloys: return static_cast<int>(Commodity::Alloys);
        case BuildingEffect::ProdComponents: return static_cast<int>(Commodity::Components);
        case BuildingEffect::ProdUnity: return static_cast<int>(Commodity::Unity);
        case BuildingEffect::ProdInfluence: return static_cast<int>(Commodity::Influence);
        default: return -1;
    }
}

}  // namespace

void aiResearchPhase(GameState& st) {
    // AI 与玩家走**完全相同**的立项制规则：选一项科技立项，
    // 每季投入资金推进，受最短工期与速度上限约束。
    // 不存在「按点数直接买断」的旁路 —— 否则 AI 与玩家的科技节奏无法比较。
    for (auto& e : st.empires) {
        if (e.isPlayer || !e.alive) continue;
        if ((st.tick + e.id) % 3 != 0) continue;

        // 已有立项：维持投入（按收入比例，扣不起时由 researchTick 自动降档）
        if (e.tech.project != TechState::kNoTech) {
            Fixed want = e.lastIncome.rawValue() > 0 ? e.lastIncome * Fixed::pct(25) : Fixed(0);
            want = fxMin(want, fxMax(e.treasury, Fixed(0)));
            if (want.rawValue() < Fixed(200).rawValue()) want = Fixed(200);
            e.tech.fundingPerTick = want;
            continue;
        }

        // 无立项：挑一个当前可研究、且层级最低的科技
        int pick = -1;
        for (int i = 0; i < kTechCount; ++i) {
            if (!techAvailable(e.tech, i)) continue;
            if (pick < 0 || techInfo(i).tier < techInfo(pick).tier ||
                (techInfo(i).tier == techInfo(pick).tier &&
                 e.tech.focus[static_cast<std::size_t>(techInfo(i).branch)] >
                 e.tech.focus[static_cast<std::size_t>(techInfo(pick).branch)])) pick = i;
        }
        if (pick < 0) continue;
        if (!techStartProject(e.tech, pick, nullptr)) continue;
        Fixed want = e.lastIncome.rawValue() > 0 ? e.lastIncome * Fixed::pct(25) : Fixed(0);
        want = fxMin(want, fxMax(e.treasury, Fixed(0)));
        if (want.rawValue() < Fixed(200).rawValue()) want = Fixed(200);
        e.tech.fundingPerTick = want;
    }
}

void aiConstructionPhase(GameState& st) {
    // 错开各帝国的建造节奏，避免同季集中结算
    for (auto& e : st.empires) {
        if (e.isPlayer || !e.alive) continue;
        if ((st.tick + e.id) % 2 != 0) continue;
        // 闲置资金门槛：至少要有 8 季收入以上的现金才考虑投资，
        // 且投资后仍需保留 3 季收入的周转金 —— 否则会把资金全部锁进建筑，
        // 导致市场采购与**贸易进口**停摆（实测贸易量跌到 0）。
        Fixed reserve = e.lastIncome.rawValue() > 0 ? e.lastIncome * Fixed(8) : Fixed(5000);
        if (reserve.rawValue() < Fixed(5000).rawValue()) reserve = Fixed(5000);
        if (e.treasury.rawValue() < reserve.rawValue()) continue;
        // 每季最多建一项（受资源约束自然限速）
        (void)aiConstructBest(st, e.id);
    }
}

bool aiConstructBest(GameState& st, u32 empire) {
    Empire* e = st.empire(empire);
    if (e == nullptr || !e->alive) return false;

    // 可持续性约束：建筑会带来**永久维护费**。
    // 没有这道闸门时 AI 会一路建到国库为负（实测 240 季 273 座、国库 -4.4 万）。
    // lastIncome 已扣除现有维护，只额外预留在建项目的未来维护费。
    i64 upkeepNow = 0;
    for (const auto& p : st.planets) {
        if (p.owner != empire) continue;
        for (const auto& order : p.buildQueue)
            upkeepNow += buildingInfo(static_cast<int>(order.building)).upkeep;
    }
    Fixed income = fxMax(e->lastIncome, Fixed(0));

    // 1) 找出最大的产出缺口（需求 − 产出），据此决定优先建什么
    std::array<Fixed, kCommodityCount> gap{};
    for (int c = 0; c < kCommodityCount; ++c) {
        Fixed need = resourceDemand(st, *e, c);
        if (need.rawValue() <= 0) continue;
        Fixed prod = empireProduction(st, *e, static_cast<u8>(c));
        Fixed g = need - prod;
        if (g.rawValue() > 0) gap[static_cast<std::size_t>(c)] = g;
    }

    // 2) 为每种候选建筑打分：补缺口越多越好，越便宜越好
    int bestB = -1;
    u32 bestPlanet = kNoSystem;
    Fixed bestScore = Fixed(0);
    for (const auto& p : st.planets) {
        if (p.owner != empire) continue;
        // 单一行星建筑数上限（避免把一切都堆在一个球上）
        if (p.buildings.size() + p.buildQueue.size() >= 8 || p.buildQueue.size() >= 2) continue;
        for (int b = 0; b < kBuildingCount; ++b) {
            const BuildingInfo& bi = buildingInfo(b);
            // 科技门槛
            if (bi.requireTech >= 0 && !techCompleted(e->tech, bi.requireTech)) continue;
            // 唯一建筑：该行星不得已有
            if (bi.unique) {
                bool dup = false;
                for (u32 ex : p.buildings)
                    if (static_cast<int>(ex & 0xFFu) == b) dup = true;
                for (const auto& order : p.buildQueue)
                    if (static_cast<int>(order.building) == b) dup = true;
                if (dup) continue;
            }
            // 代价 + 周转金：建造后仍须保留 3 季收入用于贸易与市场采购
            Fixed buffer = e->lastIncome.rawValue() > 0 ? e->lastIncome * Fixed(3) : Fixed(2000);
            if (e->treasury.rawValue() < Fixed(bi.creditCost).rawValue() + buffer.rawValue()) continue;
            // 维护可持续性：新增维护费不得超过「收入 − 现有维护」的一半
            {
                i64 headroom = static_cast<i64>(income.rawValue() / FIX) - upkeepNow;
                if (headroom <= 0) continue;
                if (bi.upkeep * 2 > headroom) continue;
            }
            bool afford = true;
            for (int c = 0; c < kCommodityCount; ++c) {
                i64 need = bi.cost[static_cast<std::size_t>(c)];
                if (need <= 0) continue;
                if (e->stock[static_cast<std::size_t>(c)].rawValue() < need * FIX) afford = false;
            }
            if (!afford) continue;

            // 打分。
            // 注意：必须给「任何产出」一个基础分 —— 早期只在**存在缺口**时给分，
            // 于是没有缺口的帝国什么都建不了（实测 240 季仅有 1 个帝国建了 15 座）。
            // 过剩产出同样有价值：可出口、可囤积、可支撑战时消耗。
            Fixed score = Fixed(0);
            int c = effectCommodity(bi.effect);
            if (c >= 0) {
                // 基础分：产出本身就有价值
                score += bi.effectValue * Fixed(2);
                // 缺口加成：缺口越大越优先
                if (gap[static_cast<std::size_t>(c)].rawValue() > 0)
                    score += fxMin(bi.effectValue, gap[static_cast<std::size_t>(c)]) * Fixed(8);
            } else if (bi.effect == BuildingEffect::ProdResearch) {
                score += bi.effectValue * Fixed(6);
            } else if (bi.effect == BuildingEffect::Defense) {
                score += bi.effectValue * Fixed(2);
            } else if (bi.effect == BuildingEffect::Stability) {
                score += bi.effectValue * Fixed(3);
            } else if (bi.effect == BuildingEffect::ProdCredits) {
                score += bi.effectValue * Fixed(2);
            } else if (bi.effect == BuildingEffect::Trading) {
                score += bi.effectValue * Fixed(2);
            } else if (bi.effect == BuildingEffect::Shipyard) {
                score += bi.effectValue * Fixed(2);
            } else if (bi.effect == BuildingEffect::Storage) {
                score += bi.effectValue * Fixed(1);
            }
            if (score.rawValue() <= 0) continue;
            // 扣掉代价（每 1000 cr 视为 1 分成本）
            score -= Fixed(bi.creditCost) / Fixed(1000);
            if (score.rawValue() > bestScore.rawValue()) {
                bestScore = score;
                bestB = b;
                bestPlanet = p.id;
            }
        }
    }
    if (bestB < 0 || bestPlanet == kNoSystem || bestScore.rawValue() <= 0) return false;

    return enqueueBuilding(st, empire, bestPlanet, bestB, nullptr);
}

}  // namespace gf
