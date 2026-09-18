#include "core/TickPipeline.h"

#include <algorithm>

#include "ai/AiCore.h"
#include "ai/FactionAI.h"
#include "ai/FederationVote.h"
#include "clue/ClueGraph.h"
#include "combat/Military.h"
#include "combat/Resolver.h"
#include "domain/Federation.h"
#include "gen/EventSchedule.h"
#include "mkt/MarketEngine.h"
#include "mkt/OrderBook.h"
#include "core/ResolutionEngine.h"
#include "domain/Parliament.h"
#include "domain/CasusBelli.h"
#include "domain/Fog.h"
#include "domain/Government.h"
#include "domain/Revolt.h"
#include "domain/Starbase.h"
#include "domain/Personnel.h"
#include "domain/Peace.h"
#include "domain/Proposal.h"
#include "domain/Construction.h"
#include "domain/Corruption.h"
#include "domain/SpeciesAdv.h"
#include "domain/SpyNetwork.h"
#include "domain/Trade.h"
#include "domain/Policy.h"
#include "domain/Economy.h"
#include "plot/EventSystem.h"
#include "plot/BeatResolver.h"
#include "rng/Streams.h"

namespace gf {

void phaseActionPoints(GameState& st) {
    for (auto& e : st.empires) {
        e.apMax = 4 + governmentInfo(e.government).apBonus;
        if (e.mind.foresight >= 3) e.apMax += 1;
        e.apLeft = e.apMax;
    }
    // 提交玩家动作队列（defer 的动作在此刻生效）
    for (const auto& a : st.pendingActions) {
        st.logEvent(LogPhase::Ap, "ap.submit", "提交动作：" + a.command, kPlayerId);
    }
    st.pendingActions.clear();
}

void phaseResolvePending(GameState& st, TickReport& rep) {
    // 未抉择事件：每 tick 提高代价（民心/观感），但不自动消失
    for (auto& c : st.pending.items) {
        if (c.deferredUntil > st.tick) continue;
        ++c.deferCount;
        if (c.deferCount > 3) {
            st.empires[kPlayerId].domestic.unrest += Fixed::pct(3);
            st.logEvent(LogPhase::Pending, "pending.decay",
                        "抉择 #" + std::to_string(c.id) + " 拖延过久：民怨 +3%，相关方观感下降");
            for (auto& e : st.empires)
                if (e.id != kPlayerId && e.alive) e.addOpinion(kPlayerId, Fixed::pct(-2));
        }
    }
    rep.pendingRaised = st.pending.hasBlocking(st.tick);
}

void phaseVolMargin(GameState& st, TickReport& rep) {
    marketUpdateVolatility(st);
    marketSettleMargins(st, rep);
}

void phaseReadPlayer(GameState& st) { aiReadPlayer(st); }
void phaseUpdateModel(GameState& st) { aiUpdatePlayerModel(st); }
void phaseAiActions(GameState& st, TickReport& rep) { aiTakeActions(st, rep); }
void phaseFederation(GameState& st) { federationPhase(st); }
void phaseDomestic(GameState& st) { domesticPhase(st); }
void phaseCombat(GameState& st, TickReport& rep) { combatPhase(st, rep); }
void phaseMilitary(GameState& st) { militaryPhase(st); }
void phaseProposals(GameState& st) { diplomacyPhase(st); aiTradePhase(st); }
void phaseCasus(GameState& st) { casusBelliPhase(st); warWearinessPhase(st); }
void phaseIntel(GameState& st) { intelPhase(st); }
void phasePersonnel(GameState& st) { rulerPhase(st); formationPhase(st); }
void phaseGovernment(GameState& st) { governmentPhase(st); }
void phaseStarbase(GameState& st) { starbasePhase(st); }
void phaseRevolt(GameState& st) { revoltPhase(st); }
void phaseIdeology(GameState& st) { ideologyPhase(st); }
void phaseConstruction(GameState& st) { constructionPhase(st); refreshEmpireBonuses(st); }
void phaseCorruption(GameState& st) { corruptionPhase(st); }
void phaseSpecies(GameState& st) {
    faunaPhase(st);
    laborPhase(st);
    pressurePhase(st);
    genePhase(st);
    nomadicPhase(st);
}
void phasePlot(GameState& st) { plotPhase(st); }

void phaseEvents(GameState& st, TickReport& rep) {
    eventsPhase(st, rep);
    // 和平会议：战争结束时的清算
    peacePhase(st);
    // 决议系统：自动触发、持续效果、倒计时推进
    resolutionPhase(st, rep);
    // 政策系统：推进过渡期
    policyPhase(st);
    // AI 政策决策
    policyAiPhase(st);
    // AI 研究先于建造：两者都从国库取钱，而建造会一次性花掉大笔资金
    //（门槛为「8 季收入」）。若建造先跑，研究几乎拿不到预算 ——
    // 实测 AI 120 季只完成 3~4 项科技，而同等收入的玩家可完成 29 项。
    aiConstructionPhase(st);
    // 议会：政治资本恢复、法案持续影响、席位重算
    parliamentPhase(st);
    // 贸易路线：结算运输、关税与收入，判定中断
    tradePhase(st);
    // 贸易 AI：开设有利可图的路线、对高关税伙伴实施报复
    tradeAiPhase(st);
    // 间谍网络：渗透增长、暴露累积、破获判定
    spyPhase(st);
    spyAiPhase(st);
    // AI 立法：非玩家帝国自行提案与表决
    parliamentAiPhase(st);
}

void phaseClues(GameState& st, TickReport& rep) {
    cluePhase(st, rep);
}

void phaseEconomy(GameState& st) { economyPhase(st); }

void clampInvariants(GameState& st) {
    // 不变式：所有 0..1 量纲在 tick 结束时必须落在有效区间内。
    // 事件、战争、经济等阶段的加减可能越界，这里统一收口，避免长期跑飞。
    for (auto& e : st.empires) {
        e.stability = fxClamp(e.stability, Fixed(0), Fixed(1));
        e.legitimacy = fxClamp(e.legitimacy, Fixed(0), Fixed(1));
        e.creditRating = fxClamp(e.creditRating, Fixed::pct(1), Fixed::pct(99));
        e.domestic.unrest = fxClamp(e.domestic.unrest, Fixed(0), Fixed(1));
        e.domestic.legitimacy = fxClamp(e.domestic.legitimacy, Fixed(0), Fixed(1));
        e.domestic.coupRisk = fxClamp(e.domestic.coupRisk, Fixed(0), Fixed(1));
        for (auto& f : e.domestic.factions) {
            f.satisfaction = fxClamp(f.satisfaction, Fixed(0), Fixed(1));
            f.influence = fxClamp(f.influence, Fixed(0), Fixed(1));
        }
        for (auto& o : e.opinion) o = fxClamp(o, Fixed(-1), Fixed(1));
        for (auto& r : e.mind.reputation) r = fxClamp(r, Fixed(0), Fixed(1));
        for (auto& h : e.mind.playerModel.habits) h = fxClamp(h, Fixed(0), Fixed(1));
        e.mind.playerModel.contamination = fxClamp(e.mind.playerModel.contamination, Fixed(0), Fixed(1));
        e.mind.playerModel.modelConfidence = fxClamp(e.mind.playerModel.modelConfidence, Fixed(0), Fixed(1));
        for (auto& t2 : e.mind.threat) t2 = fxClamp(t2, Fixed(0), Fixed(1));
        if (e.treasury.rawValue() < -Fixed(1000000).rawValue()) e.treasury = Fixed(-1000000);
    }
    for (auto& pl : st.planets) {
        pl.stability = fxClamp(pl.stability, Fixed(0), Fixed(1));
        pl.unrest = fxClamp(pl.unrest, Fixed(0), Fixed(1));
        pl.habitability = fxClamp(pl.habitability, Fixed(0), Fixed(1));
        if (pl.pops < 0) pl.pops = 0;
    }
    for (auto& f : st.fleets) {
        f.morale = fxClamp(f.morale, Fixed(0), Fixed::pct(100));
        f.supply = fxClamp(f.supply, Fixed(0), Fixed::pct(100));
        if (f.strength.rawValue() < 0) f.strength = Fixed(0);
    }
    for (auto& b : st.market.blackMarketPrice) b = fxMax(b, Fixed(0));
    for (auto& x : st.market.fx) {
        if (x.rawValue() <= 0) x = Fixed(1);
    }
}

void phaseCommit(GameState& st) {
    clampInvariants(st);
    ++st.tick;
    nationalEdictPhase(st);
    refreshEmpireBonuses(st);
    victoryPhase(st);
    st.pushHistory();
    marketPruneBooks(st);
}

TickReport advanceOneTick(GameState& st) {
    nationalEdictPhase(st);
    refreshEmpireBonuses(st);
    TickReport rep;
    rep.tick = st.tick;
    phaseActionPoints(st);
    rep.phaseTrace.push_back("phaseActionPoints");
    phaseResolvePending(st, rep);
    rep.phaseTrace.push_back("phaseResolvePending");
    // AI 研究必须在这里跑：收入由 tick 末尾的 phaseEconomy 到账，
    // 而 phaseMarket 会在 tick 开头把国库花掉。
    // 夹在两者之间（原位置在 phaseEvents 内）会让研究永远分不到预算 ——
    // 实测 AI 国库被精确抽到等于一季收入，科技 120 季只完成 3~4 项。
    aiResearchPhase(st);
    rep.phaseTrace.push_back("aiResearchPhase");
    // AI 决议也要在这里决策：决议成本 2,500~11,500 cr，而 phaseMarket
    // 会在 tick 开头把国库花掉。原先决议在市场之后决策，实测 AI
    // 「缺钱」的候选决议有 20~31 项，生效决议长期停在 2~3 项（上限为 6）。
    resolutionAiPhase(st);
    rep.phaseTrace.push_back("resolutionAiPhase");
    phaseMarket(st, rep);
    rep.phaseTrace.push_back("phaseMarket");
    phaseVolMargin(st, rep);
    rep.phaseTrace.push_back("phaseVolMargin");
    phaseReadPlayer(st);
    rep.phaseTrace.push_back("phaseReadPlayer");
    phaseUpdateModel(st);
    rep.phaseTrace.push_back("phaseUpdateModel");
    phaseAiActions(st, rep);
    rep.phaseTrace.push_back("phaseAiActions");
    phaseFederation(st);
    rep.phaseTrace.push_back("phaseFederation");
    phaseDomestic(st);
    rep.phaseTrace.push_back("phaseDomestic");
    phaseProposals(st);
    rep.phaseTrace.push_back("phaseProposals");
    phaseCasus(st);
    rep.phaseTrace.push_back("phaseCasus");
    phaseIntel(st);
    rep.phaseTrace.push_back("phaseIntel");
    phaseStarbase(st);
    rep.phaseTrace.push_back("phaseStarbase");
    phaseRevolt(st);
    rep.phaseTrace.push_back("phaseRevolt");
    phaseIdeology(st);
    rep.phaseTrace.push_back("phaseIdeology");
    phaseConstruction(st);
    rep.phaseTrace.push_back("phaseConstruction");
    phaseCorruption(st);
    rep.phaseTrace.push_back("phaseCorruption");
    phaseSpecies(st);
    rep.phaseTrace.push_back("phaseSpecies");
    phaseGovernment(st);
    rep.phaseTrace.push_back("phaseGovernment");
    phasePersonnel(st);
    rep.phaseTrace.push_back("phasePersonnel");
    // 军备积累先于战斗：造舰需要时间，军力由经济体量决定并逐步逼近目标
    phaseMilitary(st);
    rep.phaseTrace.push_back("phaseMilitary");
    phaseCombat(st, rep);
    rep.phaseTrace.push_back("phaseCombat");
    phaseEvents(st, rep);
    rep.phaseTrace.push_back("phaseEvents");
    phaseClues(st, rep);
    rep.phaseTrace.push_back("phaseClues");
    phasePlot(st);
    rep.phaseTrace.push_back("phasePlot");
    phaseEconomy(st);
    rep.phaseTrace.push_back("phaseEconomy");
    phaseCommit(st);
    rep.phaseTrace.push_back("phaseCommit");
    st.logEvent(LogPhase::Tick, kLogTick,
                "tick " + std::to_string(st.tick) + " 完成：成交 " + std::to_string(rep.fills) + " 笔，名义额 " +
                    fixedStr(rep.notional, 0) + "，AI 动作 " + std::to_string(rep.aiActions));
    st.trimLog();
    return rep;
}

int advanceTicks(GameState& st, int n, bool stopOnPending) {
    for (int i = 0; i < n; ++i) {
        if (stopOnPending && st.pending.hasBlocking(st.tick)) return static_cast<int>(ExitCode::PendingChoice);
        advanceOneTick(st);
    }
    if (stopOnPending && st.pending.hasBlocking(st.tick)) return static_cast<int>(ExitCode::PendingChoice);
    return 0;
}

int runHeadlessTicks(GameState& st, int n) {
    for (int i = 0; i < n; ++i) {
        advanceOneTick(st);
        // 自动结算待抉择（等价于 choose 0）
        if (!st.pending.empty()) {
            while (!st.pending.empty()) {
                resolveChoiceAuto(st, 0);
            }
        }
    }
    return 0;
}

}  // namespace gf
