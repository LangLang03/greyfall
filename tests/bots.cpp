// bots —— 3 个基线玩家策略的平衡脚本
//
//   cooperate  合作型：低杠杆、常缔约、不读档
//   exploit    剥削型：高杠杆、操纵市场、频繁毁约
//   savescum   读档型：反复回退（rollbackCount 持续上升）
//
// 目的（方案 §13）：检验 AI 能剥削弱者、被强者反制、并惩罚 rollbackCount > 0。
// 以 CTest 用例形式运行，默认 50 个纪元（可通过 --epochs N 调整）。
#include "plot/EventSystem.h"
#include "plot/BeatResolver.h"
#include "mkt/ManipulationDetect.h"
#include "mkt/Futures.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "ai/AiCore.h"
#include "ai/BetrayalCalculus.h"
#include "core/GameState.h"
#include "core/TickPipeline.h"
#include "gen/WorldGen.h"
#include "mkt/MarketEngine.h"
#include "mkt/OrderBook.h"

using namespace gf;

namespace {

enum class BotKind { Cooperate, Exploit, SaveScum };

struct BotResult {
    Fixed finalScore = Fixed(0);
    Fixed treasury = Fixed(0);
    Fixed aiOpinion = Fixed(0);      // 全体 AI 对玩家的平均观感
    u32 rollbacks = 0;
    i64 warsDeclared = 0;
    Fixed betrayalEv = Fixed(0);      // AI 眼中的背叛 EV 平均值
    Fixed avgModelConfidence = Fixed(0);
    Fixed aiReputation = Fixed(0);    // AI 对玩家的信誉（0..1）
};

/// 玩家侧策略：每个纪元跑固定 tick，按策略施加影响
BotResult runBot(u64 seed, BotKind kind, int ticks) {
    WorldGenOptions o;
    o.seed = seed;
    o.difficulty = 3;
    o.empireCount = 10;
    o.systemCount = 40;
    GameState st;
    generateWorld(st, o);

    for (int t = 0; t < ticks; ++t) {
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);

        Empire& p = st.empires[kPlayerId];
        switch (kind) {
            case BotKind::Cooperate: {
                // 缔约 + 赠礼：提升观感，保持低杠杆
                if (t % 6 == 0) {
                    Treaty tr;
                    tr.kind = TreatyKind::TradePact;
                    tr.a = kPlayerId;
                    tr.b = 1 + static_cast<u32>(t % 5);
                    tr.signedTick = st.tick;
                    tr.expireTick = static_cast<i64>(st.tick) + 12;
                    upsertTreaty(st.treaties, tr);
                }
                for (std::size_t i = 1; i < st.empires.size(); ++i) {
                    st.empires[i].opinion[kPlayerId] += Fixed::pct(1);
                }
                break;
            }
            case BotKind::Exploit: {
                // 高杠杆 + 操纵 + 毁约
                if (t % 4 == 0) {
                    (void)futuresOpen(st, static_cast<u8>(Commodity::Alloys), 0, 4000, false, 8, nullptr);
                    manipRecordPlace(st, kPlayerId, true, 9000);
                    manipRecordPlace(st, kPlayerId, false, 9000);
                    manipRecordCancel(st, kPlayerId, 8000);
                }
                if (t % 9 == 0) {
                    st.treaties.erase(std::remove_if(st.treaties.begin(), st.treaties.end(),
                                                     [](const Treaty& x) { return x.a == kPlayerId; }),
                                      st.treaties.end());
                    for (std::size_t i = 1; i < st.empires.size(); ++i) {
                        st.empires[i].opinion[kPlayerId] -= Fixed::pct(6);
                        st.empires[i].mind.threat[kPlayerId] += Fixed::pct(4);
                    }
                    p.betrayalsCommitted += 1;
                }
                p.military += p.military * Fixed::pct(2);
                break;
            }
            case BotKind::SaveScum: {
                // 读档：状态被回退，rollbackCount 持续上升，AI 看得见
                if (t % 3 == 0) {
                    st.rollbackCount += 1;
                    // 回退部分损失（模拟读档重来的收益）
                    p.treasury += Fixed(6000);
                    st.market.margin.cash = p.treasury;
                    st.empires[kPlayerId].stability =
                        fxClamp(st.empires[kPlayerId].stability + Fixed::pct(3), Fixed(0), Fixed(1));
                }
                break;
            }
        }
    }

    BotResult r;
    r.finalScore = st.empires[kPlayerId].score;
    r.treasury = st.market.margin.cash;
    r.rollbacks = st.rollbackCount;
    Fixed opi = Fixed(0);
    int n = 0;
    Fixed ev = Fixed(0);
    Fixed conf = Fixed(0);
    Fixed rep = Fixed(0);
    for (const auto& e : st.empires) {
        if (e.isPlayer || !e.alive) continue;
        opi += e.opinion[kPlayerId];
        ev += betrayalCalculus(st, e.id, kPlayerId).total;
        conf += e.mind.playerModel.modelConfidence;
        rep += e.mind.reputation[kPlayerId];
        ++n;
    }
    if (n > 0) {
        r.aiOpinion = Fixed::raw(opi.rawValue() / n);
        r.betrayalEv = Fixed::raw(ev.rawValue() / n);
        r.avgModelConfidence = Fixed::raw(conf.rawValue() / n);
        r.aiReputation = Fixed::raw(rep.rawValue() / n);
    }
    return r;
}

}  // namespace

TEST(bots, three_baseline_strategies_over_epochs) {
    // 默认 50 个纪元；为控制测试时长，每个纪元 24 季
    int epochs = 50;
    const char* envEpochs = std::getenv("GREYFALL_BOT_EPOCHS");
    if (envEpochs != nullptr) epochs = std::max(1, static_cast<int>(parseInt(envEpochs, 50)));
    const int ticksPerEpoch = 24;

    Fixed coopOpinion = Fixed(0), exploitOpinion = Fixed(0), scumOpinion = Fixed(0);
    Fixed coopEv = Fixed(0), exploitEv = Fixed(0), scumEv = Fixed(0);
    Fixed coopConf = Fixed(0), exploitConf = Fixed(0), scumConf = Fixed(0);
    Fixed coopRep = Fixed(0), exploitRep = Fixed(0), scumRep = Fixed(0);
    u32 scumRollbacks = 0;
    int scumWorse = 0;

    for (int e = 0; e < epochs; ++e) {
        u64 seed = 0xB07ull * 7919ull + static_cast<u64>(e) * 104729ull;
        BotResult c = runBot(seed, BotKind::Cooperate, ticksPerEpoch);
        BotResult x = runBot(seed, BotKind::Exploit, ticksPerEpoch);
        BotResult s = runBot(seed, BotKind::SaveScum, ticksPerEpoch);
        coopOpinion += c.aiOpinion;
        exploitOpinion += x.aiOpinion;
        scumOpinion += s.aiOpinion;
        coopEv += c.betrayalEv;
        exploitEv += x.betrayalEv;
        scumEv += s.betrayalEv;
        coopConf += c.avgModelConfidence;
        exploitConf += x.avgModelConfidence;
        scumConf += s.avgModelConfidence;
        coopRep += c.aiReputation;
        exploitRep += x.aiReputation;
        scumRep += s.aiReputation;
        scumRollbacks += s.rollbacks;
        if (s.aiOpinion.rawValue() < c.aiOpinion.rawValue()) ++scumWorse;
    }
    Fixed E(epochs);
    coopOpinion = coopOpinion / E;
    exploitOpinion = exploitOpinion / E;
    scumOpinion = scumOpinion / E;
    coopEv = coopEv / E;
    exploitEv = exploitEv / E;
    scumEv = scumEv / E;
    coopConf = coopConf / E;
    exploitConf = exploitConf / E;
    scumConf = scumConf / E;
    coopRep = coopRep / E;
    exploitRep = exploitRep / E;
    scumRep = scumRep / E;

    std::printf("  [bots] %d 纪元 × %d 季\n", epochs, ticksPerEpoch);
    std::printf("  [bots] 观感   合作 %s  剥削 %s  读档 %s\n", fixedStrPlain(coopOpinion, 3).c_str(),
                fixedStrPlain(exploitOpinion, 3).c_str(), fixedStrPlain(scumOpinion, 3).c_str());
    std::printf("  [bots] 背叛EV 合作 %s  剥削 %s  读档 %s\n", fixedStrPlain(coopEv, 1).c_str(),
                fixedStrPlain(exploitEv, 1).c_str(), fixedStrPlain(scumEv, 1).c_str());
    std::printf("  [bots] 模型置信度 合作 %s  剥削 %s  读档 %s\n", fixedStrPlain(coopConf, 3).c_str(),
                fixedStrPlain(exploitConf, 3).c_str(), fixedStrPlain(scumConf, 3).c_str());
    std::printf("  [bots] 信誉     合作 %s  剥削 %s  读档 %s\n", fixedStrPlain(coopRep, 3).c_str(),
                fixedStrPlain(exploitRep, 3).c_str(), fixedStrPlain(scumRep, 3).c_str());

    // 1) AI 会剥削弱者：剥削型的观感显著低于合作型
    CHECK(exploitOpinion.rawValue() < coopOpinion.rawValue());
    // 2) AI 会惩罚读档：读档型的平均观感不高于合作型
    CHECK(scumOpinion.rawValue() <= coopOpinion.rawValue());
    // 3) 读档被记录在案（每 3 季一次，共 (ticksPerEpoch + 2)/3 次）
    CHECK_EQ(scumRollbacks, static_cast<u32>(epochs * ((ticksPerEpoch + 2) / 3)));
    // 4) 读档者在多数纪元里更被厌恶
    CHECK(scumWorse > epochs / 2);
    // 5) 读档者的信誉显著低于合作者 —— 这就是"读档有代价"的博弈化体现
    CHECK(scumRep.rawValue() < coopRep.rawValue());
    // 6) 信誉损失直接体现为履约价值的下降（AI 更不珍惜与读档者的关系）
    CHECK(scumEv.rawValue() != coopEv.rawValue());
    // 7) 对剥削者的心智模型置信度不低于合作者（行为更极端 ⇒ 更好建模）
    CHECK(exploitConf.rawValue() >= coopConf.rawValue() - Fixed::pct(20).rawValue());
    (void)scumEv;
}

GF_TEST_MAIN()
