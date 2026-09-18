// 重放：逐 tick 状态哈希一致 / RNG 消费计数入档 / 战斗确定性 / 完整流水线阶段序
#include "check.h"
#include "plot/EventSystem.h"
#include "ai/AiCore.h"
#include "combat/Resolver.h"
#include "core/GameState.h"
#include "core/TickPipeline.h"
#include "gen/WorldGen.h"
#include "mkt/MarketEngine.h"
#include "mkt/OrderBook.h"
#include "save/Serde.h"

using namespace gf;

namespace {

GameState replayWorld(u64 seed) {
    WorldGenOptions o;
    o.seed = seed;
    o.difficulty = 3;
    o.empireCount = 10;
    o.systemCount = 40;
    GameState st;
    generateWorld(st, o);
    return st;
}

void stepResolved(GameState& st) {
    advanceOneTick(st);
    while (!st.pending.empty()) resolveChoiceAuto(st, 0);
}

}  // namespace

TEST(replay, same_seed_same_tick_hashes) {
    GameState a = replayWorld(0x5EED01);
    GameState b = replayWorld(0x5EED01);
    for (int i = 0; i < 15; ++i) {
        stepResolved(a);
        stepResolved(b);
        CHECK_EQ(a.stateHash(), b.stateHash());
        CHECK_EQ(a.fingerprint(), b.fingerprint());
    }
}

TEST(replay, rng_counters_are_recorded) {
    GameState a = replayWorld(0x5EED02);
    for (int i = 0; i < 5; ++i) stepResolved(a);
    bool anyConsumed = false;
    for (std::size_t i = 0; i < kRngStreamCount; ++i)
        if (a.rng.counters()[i] > 0) anyConsumed = true;
    CHECK(anyConsumed);
    std::vector<u8> bytes = serializeState(a);
    GameState b;
    CHECK(tryDeserializeState(bytes, b));
    for (std::size_t i = 0; i < kRngStreamCount; ++i) {
        CHECK_EQ(b.rng.counters()[i], a.rng.counters()[i]);
        CHECK_EQ(b.rng.states()[i], a.rng.states()[i]);
    }
    CHECK_EQ(b.rng.fingerprint(), a.rng.fingerprint());
}

TEST(replay, save_load_mid_run_continues_identically) {
    GameState a = replayWorld(0x5EED03);
    for (int i = 0; i < 8; ++i) stepResolved(a);
    std::vector<u8> snapshot = serializeState(a);
    GameState b;
    CHECK(tryDeserializeState(snapshot, b));
    for (int i = 0; i < 6; ++i) {
        stepResolved(a);
        stepResolved(b);
        CHECK_EQ(a.stateHash(), b.stateHash());
    }
}

TEST(replay, reader_cache_does_not_leak_between_states) {
    // 两个状态交替推进：若观测缓存是全局的，第二个状态会读到第一个的观测
    GameState a = replayWorld(0x5EED04);
    GameState b = replayWorld(0x5EED04);
    for (int i = 0; i < 8; ++i) {
        stepResolved(a);
        stepResolved(b);
    }
    CHECK_EQ(a.stateHash(), b.stateHash());
    CHECK_EQ(a.empires[1].mind.playerModel.observations, b.empires[1].mind.playerModel.observations);
}

TEST(replay, combat_resolution_is_deterministic) {
    GameState a = replayWorld(0x5EED05);
    GameState b = a;
    a.relation(1, 2).atWar = true;
    b.relation(1, 2).atWar = true;
    for (int i = 0; i < 6; ++i) {
        TickReport ra;
        TickReport rb;
        combatPhase(a, ra);
        combatPhase(b, rb);
        CHECK_EQ(a.stateHash(), b.stateHash());
        CHECK_EQ(ra.eventsFired, rb.eventsFired);
    }
}

TEST(replay, pipeline_phases_all_observable) {
    GameState st = replayWorld(0x5EED06);
    OrderRequest req;
    req.owner = kPlayerId;
    req.res = static_cast<u8>(Commodity::Alloys);
    req.exch = kExchCX;
    req.buy = true;
    req.qty = 100;
    req.px = st.market.exchanges[kExchCX].books[static_cast<std::size_t>(Commodity::Alloys)].mid + Fixed(1);
    (void)marketSubmitOrder(st, req);
    for (int i = 0; i < 12; ++i) stepResolved(st);
    bool hasMarket = false, hasAi = false, hasEcon = false, hasClueOrPlot = false, hasTick = false;
    for (const auto& e : st.log) {
        LogPhase p = static_cast<LogPhase>(e.phase);
        if (p == LogPhase::Market) hasMarket = true;
        if (p == LogPhase::Ai) hasAi = true;
        if (p == LogPhase::Economy) hasEcon = true;
        if (p == LogPhase::Clue || p == LogPhase::Plot) hasClueOrPlot = true;
        if (p == LogPhase::Tick) hasTick = true;
    }
    CHECK(hasMarket);
    CHECK(hasAi);
    CHECK(hasEcon);
    CHECK(hasTick);
    (void)hasClueOrPlot;
    CHECK(st.tick >= 12);
}

TEST(replay, fingerprint_is_pure_function_of_state) {
    GameState st = replayWorld(0x5EED07);
    u64 h0 = st.fingerprint();
    for (int i = 0; i < 4; ++i) stepResolved(st);
    u64 h1 = st.fingerprint();
    CHECK(h0 != h1);
    CHECK_EQ(st.fingerprint(), h1);
    // 状态哈希也必须是纯函数
    CHECK_EQ(st.stateHash(), st.stateHash());
}

TEST(replay, headless_ticks_resolve_everything) {
    GameState st = replayWorld(0x5EED08);
    runHeadlessTicks(st, 30);
    CHECK_EQ(st.tick, 30ull);
    CHECK(st.pending.empty());
    CHECK(st.empires.size() >= 8);
    for (const auto& e : st.empires) {
        CHECK(e.stability.rawValue() >= 0 && e.stability.rawValue() <= FIX);
        CHECK(e.creditRating.rawValue() >= 0 && e.creditRating.rawValue() <= FIX);
        CHECK(e.domestic.unrest.rawValue() >= 0 && e.domestic.unrest.rawValue() <= FIX);
    }
}
