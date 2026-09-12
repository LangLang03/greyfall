#pragma once

#include <string>
#include <vector>
#include "domain/Resource.h"
#include "domain/Species.h"

namespace gf {
struct GameState;
struct Empire;

enum class ProjectKind : u8 { Colony, Mega, Starbase, Ascension, Recruitment, GeneMod, Processing, Migration, Count };

struct DevelopmentProject {
    ProjectKind kind = ProjectKind::Colony;
    u32 target = 0;
    u32 definition = 0;
    u32 auxiliary = 0;
    u32 ticksLeft = 0;
    u32 totalTicks = 0;
    u64 startedTick = 0;
    u64 lastTick = 0;
    bool hasAdvanced = false;
    u32 stalledTicks = 0;
    i64 paidCredits = 0;
    i64 upkeep = 0;
    std::array<i64, kCommodityCount> supplies{};
    std::vector<u32> fleets;
    std::string pauseReason;
};

struct ProjectCost {
    i64 credits = 0;
    i64 unity = 0;
    std::array<i64, kCommodityCount> resources{};
};

struct ColonialCapacity {
    int parallel = 1;
    int territories = 6;
    int active = 0;
    int owned = 0;
    int societyTechs = 0;
    int engineeringTechs = 0;
};

struct ColonialUpkeep {
    Fixed credits = Fixed(0);
    std::array<Fixed, kCommodityCount> resources{};
};

struct NationalEdict {
    u8 kind = 0;
    u64 expiresTick = 0;
};

[[nodiscard]] ColonialCapacity colonialCapacity(const GameState& st, u32 empire);
[[nodiscard]] ColonialUpkeep colonialUpkeep(const GameState& st, u32 empire);
[[nodiscard]] Fixed resourceDemand(const GameState& st, const Empire& e, int commodity);
[[nodiscard]] bool startColony(GameState& st, u32 empire, u32 system, std::string* message);
[[nodiscard]] std::string colonyReport(const GameState& st, u32 empire, u32 system = 0xFFFFFFFFu);
[[nodiscard]] bool startMegaStage(GameState& st, u32 empire, int definition, u32 system, std::string* message);
[[nodiscard]] bool startStarbaseProject(GameState& st, u32 empire, u32 system, bool upgrade, std::string* message);
[[nodiscard]] bool startAscension(GameState& st, u32 empire, int path, std::string* message);
[[nodiscard]] bool startRecruitment(GameState& st, u32 empire, std::string* message);
[[nodiscard]] bool startGeneProject(GameState& st, u32 empire, int gene, std::string* message);
[[nodiscard]] bool startProcessing(GameState& st, u32 empire, i64 batches, std::string* message);
[[nodiscard]] bool startMigration(GameState& st, u32 empire, u32 system, std::string* message);
[[nodiscard]] bool cancelDevelopment(GameState& st, u32 empire, std::size_t index, std::string* message);
[[nodiscard]] std::string developmentReport(const GameState& st, u32 empire);
[[nodiscard]] std::string_view projectKindName(ProjectKind kind);
[[nodiscard]] std::string_view ascensionName(int path);
[[nodiscard]] bool hasDevelopment(const Empire& e, ProjectKind kind, u32 target = 0xFFFFFFFFu);
[[nodiscard]] bool payProject(GameState& st, Empire& e, const ProjectCost& cost, std::string* message);
void addDevelopment(GameState& st, Empire& e, DevelopmentProject project, const ProjectCost& cost, std::string* message);
/// 经济结算后推进项目，费用计入当季净收入；完工项目从下一季开始产出。
void developmentPhase(GameState& st);
void refreshColonizing(Empire& e);
[[nodiscard]] Fixed megaProduction(const GameState& st, const Empire& e, int commodity);
[[nodiscard]] Fixed developmentUpkeep(const GameState& st, u32 empire);
[[nodiscard]] i64 nationalEdictUpkeep(int kind);
[[nodiscard]] Fixed developmentModifier(const Empire& e, ModKind kind);
[[nodiscard]] bool activateNationalEdict(GameState& st, u32 empire, int kind, std::string* message);
void nationalEdictPhase(GameState& st);
[[nodiscard]] std::string nationalEdictReport(const GameState& st, u32 empire);
}  // namespace gf
