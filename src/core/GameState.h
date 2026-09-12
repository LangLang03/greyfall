#pragma once
// GameState —— 世界的唯一真相（纯数据 + 少量派生查询）
#include <array>
#include <string>
#include <utility>
#include <vector>

#include "clue/ClueDef.h"
#include "core/ActionPoints.h"
#include "core/Errors.h"
#include "core/LogCodes.h"
#include "core/Pending.h"
#include "core/Victory.h"
#include "domain/Empire.h"
#include "domain/Federation.h"
#include "domain/Observable.h"
#include "domain/Commander.h"
#include "domain/Fleet.h"
#include "domain/Planet.h"
#include "domain/Sector.h"
#include "domain/Treaty.h"
#include "items/ItemDef.h"
#include "domain/Peace.h"
#include "domain/Proposal.h"
#include "domain/Revolt.h"
#include "domain/Starbase.h"
#include "mkt/MarketState.h"
#include "rng/Streams.h"
#include "util/Fixed.h"

namespace gf {

struct LogEntry {
    u64 tick = 0;
    u8 phase = 0;
    u32 actor = 0xFFFFFFFFu;
    std::string code;
    std::string text;
    Fixed value{};
};

/// 历史轨迹点（history / replay 命令）
struct HistoryPoint {
    u64 tick = 0;
    u64 stateHash = 0;
    std::array<Fixed, kMaxEmpires> score{};
    std::array<Fixed, kMaxEmpires> treasury{};
    std::array<Fixed, kCommodityCount> index{};
};

/// 纪元继承的遗产位（跨纪元累积）
enum LegacyFlag : u64 {
    kLegacyInstitutions = 1ull << 0,
    kLegacyDebt = 1ull << 1,
    kLegacyEnemies = 1ull << 2,
    kLegacyTech = 1ull << 3,
    kLegacyReputation = 1ull << 4,
    kLegacyBrokenMinds = 1ull << 5,
    kLegacyClues = 1ull << 6,
    kLegacyItems = 1ull << 7,
};

struct GameState {
    // ---- 元数据 ----
    u32 schemaVersion = static_cast<u32>(kSchemaVersion);
    u64 seed = 0;
    u64 tick = 0;
    int difficulty = 2;
    u32 epochIndex = 0;
    std::string epochName;
    u64 legacyMask = 0;
    int aiForesight = 2;
    i64 aiNodeBudget = 24000;
    bool endless = false;
    bool ended = false;
    u8 endingId = 0;
    u8 act = 1;
    VictoryProgress victory;
    /// 全局词缀：位掩码（见 gen/ModifierGen.h）
    u64 modifierBits = 0;
    std::string modifierName;

    // ---- 世界 ----
    StarMap map;
    std::vector<Planet> planets;
    std::vector<Empire> empires;
    std::vector<Fleet> fleets;
    std::vector<Commander> commanders;
    std::vector<Battle> battles;
    u32 nextBattleId = 1;
    std::vector<Treaty> treaties;
    std::vector<Federation> federations;
    std::vector<Relation> relations;   // kMaxEmpires*kMaxEmpires，行主序 a*kMaxEmpires+b
    std::vector<CrisisState> crises;
    PendingQueue pending;
    std::vector<PlannedAction> pendingActions;

    // ---- 市场 ----
    MarketState market;
    /// 和平会议（战争结束时的领土与赔款清算）
    std::vector<PeaceConference> peace;
    /// 待玩家回应的 AI 提案
    ProposalBox proposals;
    /// 恒星基地（星系级设施）
    std::vector<Starbase> starbases;
    /// 境内动乱（叛乱 / 割据）
    std::vector<Revolt> revolts;
    /// 太空生物种群
    std::vector<FaunaHerd> fauna;
    /// 人事系统的 id 计数器
    u32 nextScientistId = 1;
    u32 nextFormationId = 1;

    // ---- 玩家资产 ----
    Inventory inventory;
    std::vector<ClueNode> clues;
    std::vector<ClueEdge> clueEdges;
    PlotState plot;

    // ---- 随机 ----
    RngBus rng;

    // ---- AI 观测缓存（派生数据，不入档；每个状态各一份以保证 replay 可复现）----
    ReaderCache reader;

    // ---- 存档博弈 ----
    u32 rollbackCount = 0;
    u64 chronicleHead = 0;
    /// 档案焚毁（chronicle 链缺失）：各方视为背约者。
    /// 必须与 chronicleHead==0 区分开 —— head 为 0 只表示"还没写过链"，
    /// 而无头运行（selftest / bots）绝不能被当成焚毁。
    bool chronicleBurned = false;
    u64 lastCommitTick = 0;
    u32 saveCount = 0;
    u32 loadCount = 0;

    // ---- 日志与历史 ----
    std::vector<LogEntry> log;
    u64 logSeq = 0;
    std::vector<HistoryPoint> history;

    // ------------------------------------------------------------------
    [[nodiscard]] Empire& player() { return empires[kPlayerId]; }
    [[nodiscard]] const Empire& player() const { return empires[kPlayerId]; }
    [[nodiscard]] Empire* empire(u32 id) {
        return id < empires.size() ? &empires[id] : nullptr;
    }
    [[nodiscard]] const Empire* empire(u32 id) const {
        return id < empires.size() ? &empires[id] : nullptr;
    }
    [[nodiscard]] Planet* planet(u32 id) { return id < planets.size() ? &planets[id] : nullptr; }
    [[nodiscard]] const Planet* planet(u32 id) const {
        return id < planets.size() ? &planets[id] : nullptr;
    }
    [[nodiscard]] Fleet* fleet(u32 id) { return id < fleets.size() ? &fleets[id] : nullptr; }
    [[nodiscard]] Battle* battle(u32 id) {
        for (auto& b : battles)
            if (b.id == id) return &b;
        return nullptr;
    }
    [[nodiscard]] const Battle* battle(u32 id) const {
        for (const auto& b : battles)
            if (b.id == id) return &b;
        return nullptr;
    }
    [[nodiscard]] Commander* commander(u32 id) {
        for (auto& c : commanders)
            if (c.id == id) return &c;
        return nullptr;
    }
    [[nodiscard]] const Commander* commander(u32 id) const {
        for (const auto& c : commanders)
            if (c.id == id) return &c;
        return nullptr;
    }
    [[nodiscard]] const Fleet* fleet(u32 id) const { return id < fleets.size() ? &fleets[id] : nullptr; }
    [[nodiscard]] SystemNode* system(u32 id) { return map.find(id); }
    [[nodiscard]] const SystemNode* system(u32 id) const { return map.find(id); }

    [[nodiscard]] Relation& relation(u32 a, u32 b) {
        static Relation dummy;
        std::size_t idx = static_cast<std::size_t>(a) * kMaxEmpires + b;
        if (idx >= relations.size()) return dummy;
        return relations[idx];
    }
    [[nodiscard]] const Relation& relation(u32 a, u32 b) const {
        static const Relation dummy{};
        std::size_t idx = static_cast<std::size_t>(a) * kMaxEmpires + b;
        if (idx >= relations.size()) return dummy;
        return relations[idx];
    }
    void initRelations();

    [[nodiscard]] std::size_t aliveEmpires() const;

    // ---- 日志 ----
    void logEvent(LogPhase phase, std::string_view code, std::string text, u32 actor = 0xFFFFFFFFu,
                  Fixed value = Fixed(0));
    /// 只保留最近 N 条（默认 4000）以免存档膨胀
    void trimLog(std::size_t keep = 1200);

    // ---- 摘要 ----
    /// 状态内容哈希（确定性序列化后的 SHA256 前 8 字节，replay 比对用）
    [[nodiscard]] u64 stateHash() const;
    /// 轻量指纹（不序列化，用于快速相等性检查）
    [[nodiscard]] u64 fingerprint() const;
    void pushHistory();

    /// 玩家可见的当前幕
    [[nodiscard]] u8 currentAct() const { return plot.act; }
};

[[nodiscard]] std::string stateSummaryLine(const GameState& st);

}  // namespace gf
