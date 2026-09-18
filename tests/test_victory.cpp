#include <fstream>
#include "domain/Economy.h"
#include <iterator>

#include "check.h"
#include "ai/FactionAI.h"
#include "core/Victory.h"
#include "core/TickPipeline.h"
#include "domain/Policy.h"
#include "gen/WorldGen.h"
#include "plot/BeatResolver.h"
#include "save/Migration.h"
#include "save/Serde.h"

using namespace gf;

namespace {

GameState victoryWorld(int difficulty = 5) {
    WorldGenOptions opts;
    opts.seed = 9002;
    opts.difficulty = difficulty;
    opts.empireCount = 8;
    opts.systemCount = 48;
    GameState st;
    generateWorld(st, opts);
    auto& p = st.player();
    for (auto& e : st.empires) {
        if (e.id == kPlayerId) continue;
        e.alive = false;
        e.systems.clear();
        e.fleets.clear();
    }
    p.systems.clear();
    for (auto& s : st.map.systems) {
        s.owner = kPlayerId;
        p.systems.push_back(s.id);
    }
    for (auto& planet : st.planets) {
        planet.owner = kPlayerId;
        planet.colonized = true;
    }
    p.domestic.unrest = Fixed::pct(10);
    p.stability = Fixed::pct(85);
    p.domestic.legitimacy = Fixed::pct(85);
    p.domestic.coupCountdown = 0;
    for (auto& f : p.domestic.factions) f.satisfaction = Fixed::pct(75);
    p.treasury = Fixed(200000);
    st.market.margin.cash = p.treasury;
    p.lastIncome = Fixed(1000);
    for (int c = 0; c < kCommodityCount; ++c)
        p.stock[static_cast<std::size_t>(c)] = fxMax(Fixed(1000), resourceDemand(st, p, c) * Fixed(3));
    st.pending.items.clear();
    st.revolts.clear();
    return st;
}

void victorySample(GameState& st) {
    ++st.tick;
    victoryPhase(st);
}

}  // namespace

TEST(victory, thresholds_and_exact_boundaries) {
    GameState base = victoryWorld();
    CHECK(checkVictory(base).currentCriteriaMet);
    CHECK(!checkVictory(base).won);
    for (int metric = 0; metric < 4; ++metric) {
        GameState st = base;
        if (metric == 0) st.player().domestic.unrest += Fixed::raw(1);
        if (metric == 1) st.player().stability -= Fixed::raw(1);
        if (metric == 2) st.player().domestic.legitimacy -= Fixed::raw(1);
        if (metric == 3)
            for (auto& f : st.player().domestic.factions) f.satisfaction -= Fixed::raw(1);
        CHECK(!checkVictory(st).currentCriteriaMet);
    }
    const auto rules = victoryRules(5);
    CHECK_EQ(rules.maxUnrestPct, 10);
    CHECK_EQ(rules.minStabilityPct, 85);
    CHECK_EQ(rules.minLegitimacyPct, 85);
    CHECK_EQ(rules.minAverageSatisfactionPct, 75);
    CHECK_EQ(rules.minFactionSatisfactionPct, 60);
    CHECK_EQ(rules.reserveQuarters, 2);
    const std::string report = victoryReport(base);
    for (const char* text : {"10%", "85%", "75%", "60%", "0 / 24", "能源", "药品"})
        CHECK(report.find(text) != std::string::npos);
}

TEST(victory, high_average_does_not_hide_an_unhappy_faction) {
    GameState st = victoryWorld();
    for (auto& f : st.player().domestic.factions) f.satisfaction = Fixed::pct(90);
    auto& least = st.player().domestic.factions.front();
    least.satisfaction = Fixed::pct(60);
    CHECK(checkVictory(st).currentCriteriaMet);
    least.satisfaction -= Fixed::raw(1);
    CHECK(checkVictory(st).avgFactionSatisfaction > Fixed::pct(75));
    CHECK(!checkVictory(st).currentCriteriaMet);
    st.player().domestic.factions.clear();
    CHECK(!checkVictory(st).currentCriteriaMet);
}

TEST(victory, fiscal_health_and_two_quarter_reserves_are_required) {
    const GameState base = victoryWorld();
    GameState st = base;
    st.player().lastIncome = Fixed::raw(-1);
    CHECK(!checkVictory(st).currentCriteriaMet);
    st = base;
    st.market.margin.cash = Fixed::raw(-1);
    CHECK(!checkVictory(st).currentCriteriaMet);
    st = base;
    st.player().treasury = Fixed::raw(-1);
    CHECK(!checkVictory(st).currentCriteriaMet);
    st = base;
    st.player().treasury = st.market.margin.cash = st.player().lastIncome = Fixed(0);
    CHECK(checkVictory(st).currentCriteriaMet);
    for (Commodity resource : {Commodity::Energy, Commodity::Food, Commodity::Medicines}) {
        st = base;
        const auto i = static_cast<std::size_t>(resource);
        st.player().demand[i] = Fixed(10);
        st.player().stock[i] = resourceDemand(st, st.player(), static_cast<int>(i)) * Fixed(2);
        CHECK(checkVictory(st).currentCriteriaMet);
        st.player().stock[i] -= Fixed::raw(1);
        CHECK(!checkVictory(st).currentCriteriaMet);
    }
}

TEST(victory, local_disorder_and_deferred_faction_demands_block_consolidation) {
    GameState st = victoryWorld();
    Revolt revolt;
    revolt.system = st.player().capital;
    revolt.owner = kPlayerId;
    revolt.stage = RevoltStage::Unrest;
    st.revolts.push_back(revolt);
    CHECK(!checkVictory(st).currentCriteriaMet);
    st.revolts.front().stage = RevoltStage::Calm;
    CHECK(checkVictory(st).currentCriteriaMet);
    st.player().domestic.coupCountdown = 1;
    CHECK(!checkVictory(st).currentCriteriaMet);
    st.player().domestic.coupCountdown = 0;
    PendingChoice choice;
    choice.kind = ChoiceKind::Faction;
    choice.scopeTarget = kPlayerId;
    choice.deferredUntil = 100;
    st.pending.push(choice);
    CHECK(!checkVictory(st).currentCriteriaMet);
}

TEST(victory, duration_scales_with_difficulty_and_queries_do_not_count) {
    for (int difficulty = 1; difficulty <= 5; ++difficulty) {
        GameState st = victoryWorld(difficulty);
        const u32 required = static_cast<u32>(12 + 3 * (difficulty - 1));
        CHECK_EQ(victoryRules(difficulty).requiredQuarters, required);
        const auto before = serializeState(st);
        for (int i = 0; i < 4; ++i) {
            (void)checkVictory(st);
            (void)victoryReport(st);
            victoryPhase(st);
        }
        CHECK_EQ(before, serializeState(st));
        for (u32 i = 1; i <= required; ++i) {
            victorySample(st);
            victoryPhase(st);
            CHECK_EQ(st.victory.consecutiveQuarters, i);
            CHECK_EQ(checkVictory(st).won, i == required);
        }
        CHECK(st.ended);
        CHECK_EQ(st.endingId, 12u);
    }
}

TEST(victory, interruptions_and_missed_quarters_restart_the_count) {
    GameState st = victoryWorld();
    for (int i = 0; i < 5; ++i) victorySample(st);
    st.player().lastIncome = Fixed(-1);
    victorySample(st);
    CHECK_EQ(st.victory.consecutiveQuarters, 0u);
    st.player().lastIncome = Fixed(1);
    victorySample(st);
    CHECK_EQ(st.victory.consecutiveQuarters, 1u);
    st.tick += 10;
    CHECK_EQ(checkVictory(st).sustainedQuarters, 0u);
    victoryPhase(st);
    CHECK_EQ(st.victory.consecutiveQuarters, 1u);
    CHECK(!checkVictory(st).won);
}

TEST(victory, revived_rivals_and_player_extinction_prevent_victory) {
    GameState st = victoryWorld();
    victorySample(st);
    auto& rival = st.empires[1];
    rival.alive = true;
    st.map.systems.back().owner = rival.id;
    // 即使领土缓存尚未重建，也不能漏掉真实拥有的星系。
    CHECK_EQ(checkVictory(st).aliveRivals, 1);
    victorySample(st);
    CHECK_EQ(st.victory.consecutiveQuarters, 0u);
    rival.alive = false;
    st.player().alive = false;
    CHECK(!checkVictory(st).currentCriteriaMet);
    GameState empty;
    CHECK(!checkVictory(empty).won);
    CHECK(!victoryReport(empty).empty());
}

TEST(victory, progress_survives_save_load_and_converges_identically) {
    GameState a = victoryWorld();
    for (int i = 0; i < 11; ++i) victorySample(a);
    GameState b;
    CHECK(tryDeserializeState(serializeState(a), b));
    CHECK_EQ(b.victory.consecutiveQuarters, 11u);
    CHECK_EQ(b.victory.lastEvaluatedTick, a.tick);
    CHECK_EQ(a.fingerprint(), b.fingerprint());
    for (int i = 0; i < 13; ++i) {
        victorySample(a);
        victorySample(b);
        CHECK_EQ(a.victory.consecutiveQuarters, b.victory.consecutiveQuarters);
        CHECK_EQ(a.victory.achieved, b.victory.achieved);
    }
    CHECK(checkVictory(b).won);
    GameState finished;
    CHECK(tryDeserializeState(serializeState(b), finished));
    CHECK(checkVictory(finished).won);
}

TEST(victory, schema3_migrates_without_inventing_progress_or_revoking_wins) {
    for (bool won : {false, true}) {
        const std::string path = std::string(GF_TEST_SOURCE_DIR) +
            (won ? "/data/schema3-victory-won.bin" : "/data/schema3-victory.bin");
        std::ifstream file(path, std::ios::binary);
        CHECK(file.good());
        if (!file.good()) continue;
        std::vector<u8> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        GameState st;
        CHECK(tryDeserializeState(bytes, st));
        CHECK_EQ(st.schemaVersion, static_cast<u32>(kSchemaVersion));
        CHECK_EQ(st.tick, 37u);
        CHECK_EQ(st.player().treasury, Fixed(55555));
        CHECK_EQ(st.victory.consecutiveQuarters, 0u);
        CHECK_EQ(st.victory.lastEvaluatedTick, st.tick);
        CHECK_EQ(st.victory.achieved, won);
    }
    CHECK(migration::canUpgrade(2));
    CHECK(migration::canUpgrade(3));
}

TEST(victory, no_automatic_ending_at_tick_200_and_story_does_not_block_conquest) {
    GameState st = victoryWorld();
    st.tick = 200;
    plotPhase(st);
    CHECK(!st.ended);
    CHECK(!st.victory.achieved);
    st.plot.committedConclusions.clear();
    for (u16 i = 0; i < 40; ++i) st.plot.committedConclusions.push_back(i);
    plotPhase(st);
    CHECK(st.ended);
    CHECK(st.endingId != 12);
    for (u32 i = 0; i < victoryRules(st.difficulty).requiredQuarters; ++i) victorySample(st);
    CHECK(checkVictory(st).won);
    CHECK_EQ(st.endingId, 12u);
}

TEST(victory, endless_mode_records_a_single_permanent_achievement) {
    GameState st = victoryWorld();
    st.endless = true;
    for (int i = 0; i < 30; ++i) victorySample(st);
    CHECK(checkVictory(st).won);
    CHECK(!st.ended);
    CHECK_EQ(checkVictory(st).sustainedQuarters, victoryRules(st.difficulty).requiredQuarters);
    int wins = 0;
    for (const auto& event : st.log)
        if (event.code == kLogEnding) ++wins;
    CHECK_EQ(wins, 1);
    st.player().domestic.unrest = Fixed(1);
    victorySample(st);
    plotPhase(st);
    CHECK(checkVictory(st).won);
    CHECK_EQ(st.endingId, 12u);
    WorldGenOptions options;
    options.seed = 77;
    generateWorld(st, options);
    CHECK(!st.victory.achieved);
    CHECK_EQ(st.victory.consecutiveQuarters, 0u);
}

TEST(victory, final_quarter_is_checked_after_economic_and_event_settlement) {
    GameState st = victoryWorld();
    st.player().domestic.unrest = Fixed(0);
    st.player().stability = st.player().domestic.legitimacy = Fixed(1);
    for (auto& f : st.player().domestic.factions) f.satisfaction = Fixed(1);
    st.victory.consecutiveQuarters = victoryRules(st.difficulty).requiredQuarters - 1;
    CHECK(checkVictory(st).currentCriteriaMet);
    ActiveEffect expense;
    expense.target = ResTarget::Treasury;
    expense.value = Fixed(-1000000);
    expense.ticksLeft = 2;
    st.player().resolutions.active.push_back(expense);
    advanceOneTick(st);
    CHECK(st.player().treasury < Fixed(0));
    CHECK(!st.victory.achieved);
    CHECK_EQ(st.victory.consecutiveQuarters, 0u);
}

TEST(victory, full_pipeline_awards_conquest_after_the_final_qualifying_quarter) {
    GameState st = victoryWorld();
    // 完成开发的战后领土才能覆盖扩张后的行政开销。
    for (auto& planet : st.planets) { planet.development = Fixed(8); planet.pops = 2000; }
    st.player().domestic.unrest = Fixed(0);
    st.player().stability = st.player().domestic.legitimacy = Fixed(1);
    for (auto& f : st.player().domestic.factions) f.satisfaction = Fixed(1);
    st.victory.consecutiveQuarters = victoryRules(st.difficulty).requiredQuarters - 1;
    advanceOneTick(st);
    if (!st.victory.achieved) std::printf("%s", victoryReport(st).c_str());
    CHECK(st.victory.achieved);
    CHECK(st.ended);
    CHECK_EQ(st.endingId, 12u);
    CHECK_EQ(st.victory.lastEvaluatedTick, st.tick);
    CHECK_EQ(checkVictory(st).sustainedQuarters, 24u);
}

TEST(victory, mature_peaceful_economy_can_sustain_harder_governance) {
    // 受控的战后成熟经济：检验真实内政、政策、消费、收入的收敛，
    // 不在循环中补钱、补货或直接改写民心指标。
    GameState st = victoryWorld();
    auto& p = st.player();
    p.government = 6;
    p.ethics = {2, 3, 4};
    p.domestic.unrest = Fixed::pct(35);
    p.stability = p.domestic.legitimacy = Fixed::pct(55);
    for (auto& f : p.domestic.factions) f.satisfaction = Fixed::pct(50);
    for (auto& planet : st.planets) { planet.development = Fixed(8); planet.pops = std::max<i64>(2000, planet.pops); }
    // 医疗工业满足新增殖民补给，验证持续生产而非临时补库存。
    auto& medical = st.planets.front();
    medical.buildings.push_back(16); medical.buildings.push_back(16);
    p.influence = Fixed(2000);
    for (const char* name : {"生态经济", "福利国家", "要塞主义"}) {
        const auto* policy = policyFind(name);
        CHECK(policy != nullptr);
        if (policy == nullptr) continue;
        std::string error;
        CHECK(policyEnact(st, kPlayerId, *policy, &error));
        for (int i = 0; i <= policy->transition; ++i) policyPhase(st);
    }
    for (int i = 0; i < 240 && !st.victory.achieved; ++i) {
        domesticPhase(st);
        economyPhase(st);
        phaseCommit(st);
    }
    if (!st.victory.achieved) std::printf("%s", victoryReport(st).c_str());
    CHECK(st.victory.achieved);
    CHECK(checkVictory(st).currentCriteriaMet);
    CHECK(p.lastIncome >= Fixed(0));
}
