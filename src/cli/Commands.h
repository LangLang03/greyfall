#pragma once
// CLI 上下文与命令分发
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "cli/ArgParser.h"
#include "core/GameState.h"
#include "save/SlotManager.h"

namespace gf {

struct CliEnv {
    SlotManager slots;
    std::string slotId = "main";
    std::string passphrase;
    std::string dataDir;
    bool quiet = false;
    bool verbose = false;
    bool noAutosave = false;
    bool dryRun = false;
    bool ascii = false;
    bool noEncrypt = false;
    i64 limit = 0;
    i64 asOf = -1;

    GameState st;
    bool hasState = false;
    /// 本进程是否已经落盘（防止重复 autosave）
    bool committed = false;
    /// 本进程 load 到的 chronicle 链头（用于比对）
    bool chronicleExisted = true;
    u32 loadedRollbackCount = 0;

    [[nodiscard]] std::string epochDir() const;
    [[nodiscard]] std::string chronicleFile() const;

    /// 载入存档；required=false 时允许不存在（返回 false）
    bool loadState(bool required = true);
    /// 落盘（autosave / 显式 save）。dryRun 时只打印
    void commit(std::string_view why = "autosave", bool force = false);
    /// 写入 chronicle 一条 tick 记录
    void chronicleTick(u64 actionDigest);

    [[nodiscard]] Empire& player();
    [[nodiscard]] const Empire& player() const;
    [[nodiscard]] bool hasPendingChoices() const { return !st.pending.empty(); }
};

using CmdFn = int (*)(CliEnv&, const Args&);

struct CommandDef {
    std::string_view name;
    CmdFn fn;
    std::string_view group;
    std::string_view summary;
};

/// 全部已注册命令
[[nodiscard]] const std::vector<CommandDef>& commandTable();
/// 查找命令（返回 nullptr 表示未知）
[[nodiscard]] const CommandDef* findCommand(std::string_view name);
/// 命令分组名列表
[[nodiscard]] std::vector<std::string_view> commandGroups();

/// 程序入口
int runCli(int argc, char** argv);

// ---- 各模块命令实现 ----
// Core
int cmdHelp(CliEnv&, const Args&);
int cmdMan(CliEnv&, const Args&);
int cmdVersion(CliEnv&, const Args&);
int cmdStatus(CliEnv&, const Args&);
int cmdSelftest(CliEnv&, const Args&);
int cmdVerify(CliEnv&, const Args&);
int cmdSlots(CliEnv&, const Args&);
int cmdNew(CliEnv&, const Args&);
int cmdResume(CliEnv&, const Args&);
// 世界
int cmdOverview(CliEnv&, const Args&);
int cmdStarmap(CliEnv&, const Args&);
int cmdSectors(CliEnv&, const Args&);
int cmdPlanet(CliEnv&, const Args&);
int cmdEmpires(CliEnv&, const Args&);
int cmdRelations(CliEnv&, const Args&);
int cmdTreaties(CliEnv&, const Args&);
int cmdFederation(CliEnv&, const Args&);
int cmdDomestic(CliEnv&, const Args&);
int cmdTech(CliEnv&, const Args&);
int cmdFleets(CliEnv&, const Args&);
int cmdShip(CliEnv&, const Args&);
int cmdBuildings(CliEnv&, const Args&);
int cmdLogs(CliEnv&, const Args&);
int cmdHistory(CliEnv&, const Args&);
int cmdReplay(CliEnv&, const Args&);
// 市场
int cmdMarket(CliEnv&, const Args&);
int cmdBook(CliEnv&, const Args&);
int cmdQuote(CliEnv&, const Args&);
int cmdCurve(CliEnv&, const Args&);
int cmdPosition(CliEnv&, const Args&);
int cmdPnl(CliEnv&, const Args&);
int cmdVol(CliEnv&, const Args&);
int cmdArb(CliEnv&, const Args&);
int cmdShock(CliEnv&, const Args&);
int cmdCredit(CliEnv&, const Args&);
int cmdFx(CliEnv&, const Args&);
int cmdBlackmarket(CliEnv&, const Args&);
int cmdOrder(CliEnv&, const Args&);
int cmdCancel(CliEnv&, const Args&);
int cmdModify(CliEnv&, const Args&);
int cmdFutures(CliEnv&, const Args&);
int cmdSettle(CliEnv&, const Args&);
int cmdBorrow(CliEnv&, const Args&);
int cmdRepay(CliEnv&, const Args&);
int cmdEscrow(CliEnv&, const Args&);
int cmdInsure(CliEnv&, const Args&);
// 外交 / 情报
int cmdEnvoy(CliEnv&, const Args&);
int cmdSpy(CliEnv&, const Args&);
int cmdGift(CliEnv&, const Args&);
int cmdIntel(CliEnv&, const Args&);
int cmdPropaganda(CliEnv&, const Args&);
int cmdNegotiate(CliEnv&, const Args&);
// 道具
int cmdInventory(CliEnv&, const Args&);
int cmdItem(CliEnv&, const Args&);
int cmdCombine(CliEnv&, const Args&);
int cmdUse(CliEnv&, const Args&);
int cmdDisassemble(CliEnv&, const Args&);
int cmdForgeProve(CliEnv&, const Args&);
int cmdEquip(CliEnv&, const Args&);
// 线索
int cmdClues(CliEnv&, const Args&);
int cmdLink(CliEnv&, const Args&);
int cmdUnlink(CliEnv&, const Args&);
int cmdArchive(CliEnv&, const Args&);
int cmdDeduce(CliEnv&, const Args&);
int cmdConclusions(CliEnv&, const Args&);
// 国家运营
int cmdColony(CliEnv&, const Args&);
int cmdShipBuild(CliEnv&, const Args&);
int cmdFleet(CliEnv&, const Args&);
int cmdEdict(CliEnv&, const Args&);
int cmdResearch(CliEnv&, const Args&);
int cmdBuild(CliEnv&, const Args&);
int cmdMilitary(CliEnv&, const Args&);
int cmdProposal(CliEnv&, const Args&);
int cmdCasus(CliEnv&, const Args&);
int cmdEffects(CliEnv&, const Args&);
int cmdPersonnel(CliEnv&, const Args&);
int cmdGov(CliEnv&, const Args&);
int cmdDesign(CliEnv&, const Args&);
int cmdBase(CliEnv&, const Args&);
int cmdRevolt(CliEnv&, const Args&);
int cmdSpecies(CliEnv&, const Args&);
int cmdQueue(CliEnv&, const Args&);
int cmdCorruption(CliEnv&, const Args&);
int cmdMega(CliEnv&, const Args&);
int cmdAscend(CliEnv&, const Args&);
int cmdRecruit(CliEnv&, const Args&);
// 推进与纪元
int cmdAdvance(CliEnv&, const Args&);
int cmdChoose(CliEnv&, const Args&);
int cmdDefer(CliEnv&, const Args&);
int cmdEpoch(CliEnv&, const Args&);
int cmdResolve(CliEnv&, const Args&);
int cmdVictory(CliEnv&, const Args&);
int cmdBattles(CliEnv&, const Args&);
int cmdCommanders(CliEnv&, const Args&);
int cmdPolicy(CliEnv&, const Args&);
int cmdFront(CliEnv&, const Args&);
int cmdParliament(CliEnv&, const Args&);
int cmdTrade(CliEnv&, const Args&);
int cmdNetwork(CliEnv&, const Args&);
int cmdPlanets(CliEnv&, const Args&);
int cmdPeace(CliEnv&, const Args&);
// Save
int cmdSave(CliEnv&, const Args&);
int cmdLoad(CliEnv&, const Args&);
int cmdRollback(CliEnv&, const Args&);
int cmdExport(CliEnv&, const Args&);
int cmdImport(CliEnv&, const Args&);
int cmdPrune(CliEnv&, const Args&);
int cmdDelete(CliEnv&, const Args&);
int cmdChronicle(CliEnv&, const Args&);

}  // namespace gf
