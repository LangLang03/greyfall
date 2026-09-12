#include <fstream>
#include <iterator>
#include "check.h"
#include "ai/AiCore.h"
#include "core/TickPipeline.h"
#include "domain/Development.h"
#include "domain/Trade.h"
#include "gen/EmpireGen.h"
#include "plot/BeatResolver.h"
#include "save/Serde.h"

using namespace gf;
namespace {
GameState developmentWorld() {
    GameState st;
    st.relations.resize(static_cast<std::size_t>(kMaxEmpires) * kMaxEmpires);
    st.empires.resize(2);
    st.map.systems.resize(4);
    for (u32 i = 0; i < 4; ++i) {
        auto& sys = st.map.systems[i]; sys.id = i; sys.name = "S" + std::to_string(i);
        sys.owner = i == 0 ? 0 : (i == 3 ? 1 : kNoEmpire);
        for (u32 j = 0; j < 4; ++j) if (j != i) sys.links.push_back(j);
        Planet p; p.id = i; p.system = i; p.name = "P" + std::to_string(i);
        p.type = PlanetType::Rocky; p.size = 10; p.habitability = Fixed::pct(80);
        p.owner = sys.owner; p.colonized = p.owner != kNoEmpire; p.pops = p.colonized ? 200 : 0;
        p.yield.fill(Fixed(0)); p.yield[0] = Fixed(100);
        st.planets.push_back(p); sys.planets.push_back(i);
    }
    Planet extra = st.planets[1]; extra.id = 4; st.planets.push_back(extra); st.map.systems[1].planets.push_back(4);
    for (u32 i = 0; i < 2; ++i) {
        auto& e = st.empires[i]; e.id = i; e.alive = true; e.isPlayer = i == 0;
        e.capital = i == 0 ? 0 : 3; e.systems = {e.capital}; e.name = "E" + std::to_string(i);
        e.ethics = {0, 3, 4}; e.civics.fill(0); e.government = 0;
        e.stock.fill(Fixed(200000)); e.demand.fill(Fixed(0));
        e.treasury = Fixed(5000000); e.unity = Fixed(20000); e.apLeft = 20;
    }
    st.market.margin.cash = st.player().treasury;
    refreshEmpireBonuses(st);
    return st;
}
void runDevelopment(GameState& st, int ticks) {
    for (int i = 0; i < ticks; ++i) { developmentPhase(st); ++st.tick; }
}
void finishDevelopment(GameState& st, u32 empire = 0) {
    for (int i = 0; i < 100 && !st.empires[empire].developmentProjects.empty(); ++i) runDevelopment(st, 1);
    CHECK(st.empires[empire].developmentProjects.empty());
}
void completeBranch(Empire& e, TechBranch branch, int count) {
    for (int i = 0; i < kTechCount && count > 0; ++i) if (techInfo(i).branch == branch) {
        e.tech.completed.push_back(static_cast<u8>(i)); --count;
    }
}
}

TEST(development, colony_requires_all_materials_and_failed_order_is_atomic) {
    auto st = developmentWorld(); st.player().stock[static_cast<int>(Commodity::Components)] = Fixed(179);
    const auto before = serializeState(st);
    CHECK(!startColony(st, 0, 1, nullptr));
    CHECK_EQ(serializeState(st), before);
    st.player().stock[static_cast<int>(Commodity::Components)] = Fixed(10000);
    st.player().treasury = Fixed(0); st.market.margin.cash = Fixed(0);
    CHECK(!startColony(st, 0, 1, nullptr)); CHECK(st.player().developmentProjects.empty());
}

TEST(development, colony_reserves_without_ownership_and_settles_only_one_planet) {
    auto st = developmentWorld(); auto& e = st.player();
    const auto money = e.treasury, alloys = e.stock[static_cast<int>(Commodity::Alloys)];
    CHECK(startColony(st, 0, 1, nullptr));
    const auto p = e.developmentProjects.front();
    CHECK(p.totalTicks >= 6); CHECK_EQ(st.system(1)->owner, kNoEmpire);
    CHECK_EQ(e.systems.size(), 1u); CHECK_EQ(e.colonizing.size(), 1u);
    CHECK_EQ(e.stock[static_cast<int>(Commodity::Alloys)], alloys - Fixed(300));
    CHECK_EQ(e.treasury, money - Fixed(p.paidCredits));
    runDevelopment(st, static_cast<int>(p.totalTicks) - 1);
    CHECK_EQ(st.system(1)->owner, kNoEmpire); CHECK(!st.planet(1)->colonized);
    runDevelopment(st, 1);
    CHECK_EQ(st.system(1)->owner, 0u); CHECK(st.planet(1)->colonized); CHECK(!st.planet(4)->colonized);
    CHECK_EQ(e.coloniesFounded, 1u); CHECK(e.colonizing.empty());
    CHECK_EQ(e.treasury, money - Fixed(p.paidCredits + p.upkeep * p.totalTicks));
    CHECK_EQ(e.lastIncome, -Fixed(p.upkeep * p.totalTicks));
    const auto young = planetNaturalProduction(*st.planet(1), 0, Fixed(0), Fixed(0));
    runDevelopment(st, 12);
    const auto mature = planetNaturalProduction(*st.planet(1), 0, Fixed(0), Fixed(0));
    CHECK_EQ(mature, young * Fixed(4));
    CHECK(startColony(st, 0, 1, nullptr)); finishDevelopment(st);
    CHECK(st.planet(4)->colonized); CHECK_EQ(e.systems.size(), 2u);
}

TEST(development, colony_caps_depend_on_ethics_government_and_unique_technology) {
    auto st = developmentWorld(); auto& e = st.player();
    const auto base = colonialCapacity(st, 0); CHECK_EQ(base.parallel, 1); CHECK_EQ(base.territories, 6);
    e.ethics = {static_cast<u8>(EthicAxis::Expansion)};
    auto cap = colonialCapacity(st, 0); CHECK_EQ(cap.parallel, 2); CHECK_EQ(cap.territories, 10);
    e.government = 14; cap = colonialCapacity(st, 0); CHECK_EQ(cap.parallel, 3); CHECK_EQ(cap.territories, 14);
    completeBranch(e, TechBranch::Society, 8); completeBranch(e, TechBranch::Engineering, 8);
    cap = colonialCapacity(st, 0); CHECK_EQ(cap.parallel, 5); CHECK_EQ(cap.territories, 26);
    e.tech.completed.push_back(e.tech.completed.front()); CHECK_EQ(colonialCapacity(st, 0).territories, 26);
    e.ethics = {static_cast<u8>(EthicAxis::Isolation)}; e.government = 0; e.tech.completed.clear();
    CHECK_EQ(colonialCapacity(st, 0).territories, 4); CHECK_EQ(colonialCapacity(st, 0).parallel, 1);
}

TEST(development, reservations_block_parallel_spam_and_rival_claims) {
    auto st = developmentWorld(); CHECK(startColony(st, 0, 1, nullptr));
    auto before = serializeState(st);
    CHECK(!startColony(st, 0, 2, nullptr)); CHECK(!startColony(st, 1, 1, nullptr));
    CHECK_EQ(serializeState(st), before);
    CHECK(cancelDevelopment(st, 0, 0, nullptr)); CHECK(startColony(st, 1, 1, nullptr));
}

TEST(development, unfinished_colonies_are_not_expansion_bridges) {
    auto st = developmentWorld(); st.player().ethics = {static_cast<u8>(EthicAxis::Expansion)};
    st.system(2)->links = {1, 3}; st.system(0)->links = {1};
    CHECK(startColony(st, 0, 1, nullptr)); CHECK(!startColony(st, 0, 2, nullptr));
    finishDevelopment(st); CHECK(startColony(st, 0, 2, nullptr));
}

TEST(development, planet_suitability_is_checked_before_payment) {
    auto st = developmentWorld(); st.planet(1)->type = PlanetType::GasGiant; st.planet(4)->type = PlanetType::Asteroid;
    CHECK(!startColony(st, 0, 1, nullptr));
    st.planet(1)->type = PlanetType::Barren; st.planet(1)->habitability = Fixed::pct(10);
    CHECK(!startColony(st, 0, 1, nullptr));
    st.player().tech.completed.push_back(57); CHECK(startColony(st, 0, 1, nullptr));
}

TEST(development, supply_shortage_pauses_and_eight_stalled_quarters_release_claim) {
    auto st = developmentWorld(); CHECK(startColony(st, 0, 1, nullptr));
    const auto p = st.player().developmentProjects.front();
    st.player().stock[static_cast<int>(Commodity::Medicines)] = Fixed(0);
    auto cash = st.player().treasury;
    runDevelopment(st, 1);
    CHECK_EQ(st.player().developmentProjects.front().ticksLeft, p.totalTicks);
    CHECK(!st.player().developmentProjects.front().pauseReason.empty());
    CHECK_EQ(st.player().treasury, cash - Fixed(p.upkeep / 5));
    runDevelopment(st, 7); CHECK(st.player().developmentProjects.empty()); CHECK(st.player().colonizing.empty());
    CHECK_EQ(st.system(1)->owner, kNoEmpire); CHECK(startColony(st, 1, 1, nullptr));
}

TEST(development, blockade_and_cash_shortage_cannot_grant_free_progress) {
    auto st = developmentWorld(); CHECK(startColony(st, 0, 1, nullptr));
    auto total = st.player().developmentProjects.front().ticksLeft;
    st.system(1)->blockade = Fixed::pct(50); runDevelopment(st, 1);
    CHECK_EQ(st.player().developmentProjects.front().ticksLeft, total);
    st.system(1)->blockade = Fixed(0); st.player().treasury = Fixed(0); runDevelopment(st, 1);
    CHECK_EQ(st.player().developmentProjects.front().ticksLeft, total); CHECK_EQ(st.player().treasury, Fixed(0));
    st.player().treasury = Fixed(10000); runDevelopment(st, 1);
    CHECK_EQ(st.player().developmentProjects.front().ticksLeft, total - 1);
    CHECK_EQ(st.player().developmentProjects.front().stalledTicks, 0u);
}

TEST(development, cancellation_cannot_refund_materials_or_produce_profit) {
    auto st = developmentWorld(); const auto cash = st.player().treasury;
    CHECK(startColony(st, 0, 1, nullptr)); const auto project = st.player().developmentProjects.front();
    const auto stock = st.player().stock;
    CHECK(cancelDevelopment(st, 0, 0, nullptr));
    CHECK_EQ(st.player().treasury, cash - Fixed(project.paidCredits) + Fixed(project.paidCredits * 40 / 100));
    CHECK_EQ(st.player().stock, stock); CHECK(!cancelDevelopment(st, 0, 0, nullptr));
}

TEST(development, severe_discounts_keep_positive_cash_material_and_time_costs) {
    auto st = developmentWorld(); auto& e = st.player();
    e.ascensions = 8; e.ethics.fill(static_cast<u8>(EthicAxis::Expansion));
    CHECK(startColony(st, 0, 1, nullptr)); const auto& p = e.developmentProjects.front();
    CHECK(p.paidCredits >= 13000); CHECK(p.totalTicks >= 6); CHECK(p.supplies[0] > 0);
}

TEST(development, developed_colonies_add_recurring_cash_and_resource_costs) {
    auto st = developmentWorld(); CHECK_EQ(developmentUpkeep(st, 0), Fixed(0));
    CHECK(startColony(st, 0, 1, nullptr)); finishDevelopment(st);
    CHECK_EQ(colonialUpkeep(st, 0).credits, Fixed(115));
    CHECK_EQ(resourceDemand(st, st.player(), static_cast<int>(Commodity::Energy)), Fixed(8));
    CHECK_EQ(resourceDemand(st, st.player(), static_cast<int>(Commodity::Food)), Fixed(5));
    CHECK_EQ(resourceDemand(st, st.player(), static_cast<int>(Commodity::Medicines)), Fixed(1));
    CHECK(developmentUpkeep(st, 0) > Fixed(0));
}

TEST(development, ai_colonization_uses_the_same_costs_and_queue) {
    auto st = developmentWorld(); AiAction action; action.kind = AiActionKind::Colonize; TickReport report;
    st.empires[1].treasury = Fixed(0); aiExecuteAction(st, 1, action, report);
    CHECK(st.empires[1].developmentProjects.empty());
    st.empires[1].treasury = Fixed(5000000); const auto ap = st.empires[1].apLeft;
    aiExecuteAction(st, 1, action, report);
    CHECK_EQ(st.empires[1].developmentProjects.size(), 1u); CHECK_EQ(st.empires[1].systems.size(), 1u);
    CHECK_EQ(st.empires[1].apLeft, ap - apcost::kColony); CHECK(st.empires[1].treasury < Fixed(5000000));
    finishDevelopment(st, 1); CHECK_EQ(st.empires[1].systems.size(), 2u);
}

TEST(development, mega_is_timed_unique_and_pays_exact_total_installments) {
    auto st = developmentWorld(); auto& e = st.player(); const auto& info = megastructureInfo(0);
    e.tech.completed.push_back(static_cast<u8>(info.requireTech)); i64 paid = 0;
    for (int stage = 0; stage < info.stages; ++stage) {
        CHECK(startMegaStage(st, 0, 0, 0, nullptr)); paid += e.developmentProjects.back().paidCredits;
        CHECK_EQ(e.megas[0].stage, stage); CHECK(!st.system(0)->megastructure);
        CHECK(!startMegaStage(st, 0, 0, 0, nullptr)); finishDevelopment(st);
        CHECK_EQ(e.megas[0].stage, stage + 1);
    }
    CHECK_EQ(paid, info.creditCost); CHECK(st.system(0)->megastructure); CHECK(e.megas[0].complete);
    CHECK(!startMegaStage(st, 0, 0, 0, nullptr)); CHECK_EQ(megaProduction(st, e, 0), info.effectValue);
    CHECK(developmentUpkeep(st, 0) >= Fixed(info.creditCost / 500));
}

TEST(development, mega_requires_cash_and_cannot_resume_in_another_system) {
    auto st = developmentWorld(); auto& e = st.player(); e.tech.completed.push_back(50);
    e.treasury = Fixed(0); const auto before = serializeState(st);
    CHECK(!startMegaStage(st, 0, 0, 0, nullptr)); CHECK_EQ(serializeState(st), before);
    e.treasury = Fixed(5000000); CHECK(startMegaStage(st, 0, 0, 0, nullptr)); finishDevelopment(st);
    st.system(1)->owner = 0; CHECK(!startMegaStage(st, 0, 0, 1, nullptr)); CHECK_EQ(e.megas.size(), 1u);
}

TEST(development, starbase_has_no_early_defense_or_instant_upgrades) {
    auto st = developmentWorld(); const auto defense = systemDefense(st, 0);
    CHECK(starbaseFound(st, 0, 0, nullptr)); CHECK(!starbaseAt(st, 0));
    CHECK_EQ(systemDefense(st, 0), defense); CHECK(!starbaseUpgrade(st, 0, 0, nullptr));
    finishDevelopment(st); CHECK(starbaseAt(st, 0)); CHECK(systemDefense(st, 0) > defense);
    CHECK_EQ(developmentUpkeep(st, 0), Fixed(50));
    CHECK(starbaseUpgrade(st, 0, 0, nullptr)); CHECK(!starbaseDismantle(st, 0, 0, nullptr));
    CHECK_EQ(starbaseAt(st, 0)->tier, StarbaseTier::Outpost);
    finishDevelopment(st); CHECK_EQ(starbaseAt(st, 0)->tier, StarbaseTier::Starport);
    CHECK_EQ(developmentUpkeep(st, 0), Fixed(200));
}

TEST(development, ascension_requires_research_time_and_cannot_compound) {
    auto st = developmentWorld(); auto& e = st.player(); CHECK(!startAscension(st, 0, 1, nullptr));
    completeBranch(e, TechBranch::Engineering, 4);
    auto build = empireModifier(e, ModKind::BuildRate); const auto capacity = e.capacity;
    CHECK(startAscension(st, 0, 1, nullptr)); CHECK_EQ(empireModifier(e, ModKind::BuildRate), build);
    CHECK(!startAscension(st, 0, 1, nullptr)); finishDevelopment(st);
    CHECK_EQ(empireModifier(e, ModKind::BuildRate), build + Fixed::pct(25));
    CHECK_EQ(e.capacity, capacity); CHECK(!startAscension(st, 0, 1, nullptr));
    completeBranch(e, TechBranch::Biology, 4); CHECK(startAscension(st, 0, 2, nullptr)); finishDevelopment(st);
    completeBranch(e, TechBranch::Psionics, 4); CHECK(!startAscension(st, 0, 0, nullptr));
}

TEST(development, gene_modification_uses_materials_and_activates_after_eight_quarters) {
    auto st = developmentWorld(); auto& e = st.player();
    CHECK(!applyGeneMod(st, 0, GeneMod::Hardy, nullptr)); completeBranch(e, TechBranch::Biology, 2);
    CHECK(applyGeneMod(st, 0, GeneMod::Hardy, nullptr)); CHECK(!e.geneMods[0]);
    CHECK(!applyGeneMod(st, 0, GeneMod::Erudite, nullptr)); runDevelopment(st, 7); CHECK(!e.geneMods[0]);
    runDevelopment(st, 1); CHECK(e.geneMods[0]); CHECK(!applyGeneMod(st, 0, GeneMod::Hardy, nullptr));
    CHECK(!applyGeneMod(st, 0, GeneMod::Count, nullptr));
}

TEST(development, recruitment_scales_by_fleet_and_excludes_battles) {
    auto st = developmentWorld(); auto& e = st.player();
    CHECK(!startRecruitment(st, 0, nullptr));
    for (u32 i = 0; i < 3; ++i) { Fleet f; f.id = i; f.owner = 0; f.system = 0;
        f.morale = Fixed::pct(50); f.supply = Fixed::pct(50); st.fleets.push_back(f); e.fleets.push_back(i); }
    st.fleets[2].battle = 0;
    CHECK(startRecruitment(st, 0, nullptr)); CHECK_EQ(e.developmentProjects[0].paidCredits, 7000);
    CHECK_EQ(e.developmentProjects[0].fleets.size(), 2u); CHECK_EQ(st.fleets[0].morale, Fixed::pct(50));
    st.fleets[1].system = 3; runDevelopment(st, 3);
    CHECK_EQ(st.fleets[0].morale, Fixed::pct(60)); CHECK_EQ(st.fleets[0].supply, Fixed::pct(65));
    CHECK_EQ(st.fleets[1].morale, Fixed::pct(50)); CHECK_EQ(st.fleets[2].morale, Fixed::pct(50));
}

TEST(development, edicts_charge_cash_do_not_stack_and_expire_without_permanent_rate_changes) {
    auto st = developmentWorld(); auto& e = st.player(); const auto rate = e.tech.rate;
    const auto mod = empireModifier(e, ModKind::ResearchRate); const auto cash = e.treasury;
    CHECK(activateNationalEdict(st, 0, 3, nullptr)); CHECK_EQ(e.treasury, cash - Fixed(5000));
    CHECK_EQ(empireModifier(e, ModKind::ResearchRate), mod + Fixed::pct(20));
    CHECK_EQ(developmentUpkeep(st, 0), Fixed(1000));
    CHECK(!activateNationalEdict(st, 0, 3, nullptr));
    CHECK(activateNationalEdict(st, 0, 0, nullptr)); CHECK(activateNationalEdict(st, 0, 1, nullptr));
    CHECK(!activateNationalEdict(st, 0, 5, nullptr));
    st.tick = 8; nationalEdictPhase(st); refreshEmpireBonuses(st);
    CHECK(e.nationalEdicts.empty()); CHECK_EQ(e.tech.rate, rate); CHECK_EQ(empireModifier(e, ModKind::ResearchRate), mod);
    e.treasury = Fixed(14999); CHECK(!activateNationalEdict(st, 0, 2, nullptr));
}

TEST(development, funding_factions_has_a_cooldown_and_no_instant_unrest_healing) {
    auto st = developmentWorld(); Faction f; f.kind = FactionKind::Military; st.player().domestic.factions.push_back(f);
    st.player().domestic.unrest = Fixed::pct(30);
    CHECK(satisfyFaction(st, 0, f.kind, nullptr)); const auto cash = st.player().treasury;
    CHECK_EQ(st.player().domestic.unrest, Fixed::pct(30));
    CHECK(!satisfyFaction(st, 0, f.kind, nullptr)); CHECK_EQ(st.player().treasury, cash);
    st.tick = 8; CHECK(satisfyFaction(st, 0, f.kind, nullptr));
}

TEST(development, save_reload_preserves_timing_and_idempotent_project_charges) {
    auto st = developmentWorld(); CHECK(startColony(st, 0, 1, nullptr)); developmentPhase(st);
    const auto before = serializeState(st); developmentPhase(st); CHECK_EQ(serializeState(st), before);
    GameState loaded; CHECK(tryDeserializeState(before, loaded)); CHECK_EQ(loaded.player().colonizing, st.player().colonizing);
    developmentPhase(loaded); CHECK_EQ(serializeState(loaded), before);
    ++st.tick; ++loaded.tick; finishDevelopment(st); finishDevelopment(loaded);
    CHECK_EQ(st.stateHash(), loaded.stateHash());
}

TEST(development, schema4_migration_keeps_existing_colonies_and_victory_progress) {
    std::ifstream file(std::string(GF_TEST_SOURCE_DIR) + "/data/schema4-projects.bin", std::ios::binary);
    CHECK(file.good()); if (!file.good()) return;
    std::vector<u8> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    GameState st; CHECK(tryDeserializeState(bytes, st)); CHECK_EQ(st.schemaVersion, static_cast<u32>(kSchemaVersion));
    CHECK_EQ(st.tick, 37u); CHECK_EQ(st.victory.consecutiveQuarters, 8u);
    CHECK_EQ(st.system(0)->owner, 0u); CHECK(st.planet(0)->colonized); CHECK_EQ(st.planet(0)->pops, 200);
    CHECK(st.player().developmentProjects.empty()); CHECK(st.player().colonizing.empty());
    CHECK_EQ(st.planet(0)->settlementTicksLeft, 0u);
}

TEST(development, actual_economy_charges_project_maintenance_and_materials_once) {
    auto st = developmentWorld(); CHECK(startColony(st, 0, 1, nullptr));
    const auto project = st.player().developmentProjects[0];
    auto control = st; control.player().developmentProjects.clear(); refreshColonizing(control.player());
    economyPhase(control); economyPhase(st);
    CHECK_EQ(st.player().lastIncome, control.player().lastIncome - Fixed(project.upkeep));
    CHECK_EQ(st.player().treasury, control.player().treasury - Fixed(project.upkeep));
    CHECK_EQ(st.player().stock[0], control.player().stock[0] - Fixed(project.supplies[0]));
    CHECK_EQ(st.player().developmentProjects[0].ticksLeft, project.totalTicks - 1);
}

TEST(development, processing_limits_batches_and_requires_time_cash_and_supplies) {
    auto st = developmentWorld(); auto& e = st.player(); const auto original = serializeState(st);
    CHECK(!canStarJelly(st, 0, 11, nullptr)); CHECK(!canStarJelly(st, 0, INT64_MAX, nullptr));
    CHECK_EQ(serializeState(st), original);
    const auto luxury = e.stock[static_cast<int>(Commodity::Luxury)];
    const auto food = e.stock[static_cast<int>(Commodity::Food)]; const auto cash = e.treasury;
    CHECK(canStarJelly(st, 0, 10, nullptr)); CHECK(!canStarJelly(st, 0, 1, nullptr));
    CHECK_EQ(e.stock[static_cast<int>(Commodity::Luxury)], luxury);
    CHECK_EQ(e.stock[static_cast<int>(Commodity::Food)], food - Fixed(2000));
    runDevelopment(st, 2); CHECK_EQ(e.stock[static_cast<int>(Commodity::Luxury)], luxury);
    runDevelopment(st, 1); CHECK_EQ(e.stock[static_cast<int>(Commodity::Luxury)], luxury + Fixed(300));
    CHECK_EQ(e.treasury, cash - Fixed(11000));
}

TEST(development, migration_waits_for_transport_and_does_not_farm_stability) {
    auto st = developmentWorld(); auto& e = st.player();
    for (int i = 0; i < kCivicsCount; ++i) if (civicInfo(i).idName == "nomadic") e.civics[0] = static_cast<u8>(i);
    st.system(1)->owner = 0; e.systems.push_back(1); e.influence = Fixed(1000);
    st.system(0)->capital = true; st.planet(0)->capital = true;
    const auto stable = e.stability; const auto cash = e.treasury;
    CHECK(nomadicMigrate(st, 0, 1, nullptr)); CHECK_EQ(e.capital, 0u);
    CHECK(!nomadicMigrate(st, 0, 1, nullptr)); runDevelopment(st, 5);
    CHECK_EQ(e.capital, 1u); CHECK_EQ(e.stability, stable); CHECK_EQ(e.influence, Fixed(700));
    CHECK_EQ(e.treasury, cash - Fixed(12500)); CHECK(!st.system(0)->capital); CHECK(st.system(1)->capital);
}

TEST(development, medical_industry_supplies_colonies_in_both_forecast_and_settlement) {
    auto st = developmentWorld(); auto& e = st.player(); const auto med = static_cast<u8>(Commodity::Medicines);
    auto control = st;
    st.planet(0)->buildings.push_back(16);
    CHECK_EQ(empireProduction(st, e, med) - empireProduction(control, control.player(), med), Fixed(75));
    economyPhase(st); economyPhase(control);
    CHECK_EQ(e.stock[med] - control.player().stock[med], Fixed(75));
    CHECK(e.lastIncome < control.player().lastIncome);
}

TEST(development, territory_capacity_reserves_pending_claims_and_prices_overextension) {
    auto st = developmentWorld(); auto& e = st.player(); e.ethics = {static_cast<u8>(EthicAxis::Expansion)};
    const int limit = colonialCapacity(st, 0).territories;
    for (int i = 0; i < limit - 1; ++i) {
        SystemNode sys; sys.id = static_cast<u32>(st.map.systems.size()); sys.owner = 0; sys.links = {0};
        st.system(0)->links.push_back(sys.id); e.systems.push_back(sys.id); st.map.systems.push_back(sys);
    }
    CHECK(startColony(st, 0, 1, nullptr)); CHECK(!startColony(st, 0, 2, nullptr));
    finishDevelopment(st); CHECK_EQ(colonialCapacity(st, 0).owned, limit);
    const auto normal = colonialUpkeep(st, 0).credits;
    st.system(3)->owner = 0; e.systems.push_back(3);
    CHECK_EQ(colonialCapacity(st, 0).owned, limit + 1);
    CHECK(colonialUpkeep(st, 0).credits > normal + Fixed(95));
    CHECK(!startColony(st, 0, 2, nullptr));
}

TEST(development, abandoned_first_mega_stage_releases_the_site_and_unique_slot) {
    auto st = developmentWorld(); st.player().tech.completed.push_back(50);
    CHECK(startMegaStage(st, 0, 0, 0, nullptr)); CHECK(cancelDevelopment(st, 0, 0, nullptr));
    CHECK(st.player().megas.empty()); CHECK(!st.system(0)->megastructure);
    st.system(1)->owner = 0; CHECK(startMegaStage(st, 0, 0, 1, nullptr));
}
