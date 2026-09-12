#include <filesystem>
#include "cli/Commands.h"

#include <cstdlib>

#include "core/Errors.h"
#include "save/Chronicle.h"
#include "save/Migration.h"
#include "save/SaveFile.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {

std::string CliEnv::epochDir() const { return dataDir + "/epoch-" + std::to_string(st.epochIndex); }
std::string CliEnv::chronicleFile() const { return Chronicle::pathFor(epochDir()); }

Empire& CliEnv::player() {
    if (st.empires.empty()) fail(ExitCode::Internal, "状态中没有玩家帝国");
    return st.empires[kPlayerId];
}
const Empire& CliEnv::player() const {
    if (st.empires.empty()) fail(ExitCode::Internal, "状态中没有玩家帝国");
    return st.empires[kPlayerId];
}

bool CliEnv::loadState(bool required) {
    if (hasState) return true;
    if (!slots.exists(slotId)) {
        if (required) {
            fail(ExitCode::NoSave, "存档槽 '" + slotId + "' 不存在。先运行：greyfall new --seed <S>");
        }
        return false;
    }
    std::vector<u8> bytes;
    bool usedBackup = false;
    if (!slots.readSlotWithFallback(slotId, bytes, &usedBackup)) {
        fail(ExitCode::Integrity, "无法读取存档槽 '" + slotId + "'（主档与备份均不可读）");
    }
    if (usedBackup) note("[恢复] 主档不可读，已从 .bak 备份载入");

    SaveHeaderInfo hdr = peekSave(bytes);
    // 先用**明文头部**检查 schema 版本，再尝试解码。
    // serde 是字段顺序流，字段增删后旧存档的解码会失败；
    // 若不先查版本，用户看到的是「完整性失败(3)」，而真实原因是版本不兼容(6)。
    if (hdr.schemaVersion > static_cast<u32>(kSchemaVersion)) {
        fail(ExitCode::VersionMismatch,
             "存档 schemaVersion=" + std::to_string(hdr.schemaVersion) + " 高于本程序 " +
                 std::to_string(kSchemaVersion) + "（请使用更新版本的 greyfall）");
    }
    if (hdr.schemaVersion < static_cast<u32>(kSchemaVersion) &&
        !migration::canUpgrade(static_cast<int>(hdr.schemaVersion))) {
        fail(ExitCode::VersionMismatch,
             "存档 schemaVersion=" + std::to_string(hdr.schemaVersion) + " 无法迁移到 " +
                 std::to_string(kSchemaVersion) + "（迁移路径：" +
                 migration::describe(static_cast<int>(hdr.schemaVersion)) + "）");
    }
    std::string detail;
    DecodeStatus status = decodeSave(bytes, st, passphrase, &detail);
    if (status != DecodeStatus::Ok && !usedBackup) {
        // 尝试备份
        std::vector<u8> bak;
        if (slots.readSlotWithFallback(slotId, bak, &usedBackup) && usedBackup) {
            DecodeStatus s2 = decodeSave(bak, st, passphrase, &detail);
            if (s2 == DecodeStatus::Ok) {
                note("[恢复] 主档损坏，已回退到 .bak");
                status = DecodeStatus::Ok;
                bytes = std::move(bak);
            }
        }
    }
    if (status != DecodeStatus::Ok) {
        ExitCode code = ExitCode::Integrity;
        if (status == DecodeStatus::VersionTooHigh) code = ExitCode::VersionMismatch;
        fail(code, "存档槽 '" + slotId + "' 解码失败：" + std::string(decodeStatusName(status)) +
                       (detail.empty() ? "" : "（" + detail + "）"));
    }
    if (hdr.schemaVersion > static_cast<u32>(kSchemaVersion)) {
        fail(ExitCode::VersionMismatch, "存档 schemaVersion=" + std::to_string(hdr.schemaVersion) +
                                            " 高于本程序 " + std::to_string(kSchemaVersion));
    }
    if (hdr.schemaVersion < static_cast<u32>(kSchemaVersion)) {
        std::string err;
        if (!migration::chain(static_cast<int>(hdr.schemaVersion), st, &err)) {
            fail(ExitCode::VersionMismatch, "存档迁移失败：" + err);
        }
        note("[迁移] schema " + std::to_string(hdr.schemaVersion) + " → " + std::to_string(kSchemaVersion));
    }
    st.schemaVersion = static_cast<u32>(kSchemaVersion);
    // chronicle 链是否存在，决定了"档案焚毁"是否生效
    if (st.epochIndex == 0 && st.tick == 0) {
        st.chronicleBurned = false;
    } else if (!Chronicle::exists(chronicleFile())) {
        if (!st.chronicleBurned) {
            note("[档案焚毁] chronicle 链文件缺失：各方将你视为背约者（观感与援助下降，AI 优先采用不可逆打击）");
        }
        st.chronicleBurned = true;
    }
    st.loadCount += 1;
    loadedRollbackCount = st.rollbackCount;
    hasState = true;
    return true;
}

void CliEnv::commit(std::string_view why, bool force) {
    if (!hasState) return;
    if (dryRun) {
        note(std::string("[dry-run] 跳过落盘（") + std::string(why) + "）");
        return;
    }
    if (noAutosave && !force) return;
    SaveOptions opts;
    opts.encrypt = !noEncrypt;
    opts.compress = true;
    st.lastCommitTick = st.tick;
    std::vector<u8> bytes = encodeSave(st, passphrase, opts);
    slots.writeSlot(slotId, bytes);
    slots.rotateCheckpoints(slotId, bytes);
    committed = true;
    trace(std::string("[save] ") + std::string(why) + " → " + slots.slotPath(slotId) + " (" +
          humanBytes(bytes.size()) + ")");
}

void CliEnv::chronicleTick(u64 actionDigest) {
    if (dryRun) return;
    std::error_code ec;
    std::filesystem::create_directories(epochDir(), ec);
    u64 stateDigest = st.fingerprint();
    st.chronicleHead =
        Chronicle::append(chronicleFile(), ChronicleKind::Tick, st.tick, actionDigest, stateDigest);
}

// ---------------------------------------------------------------------------

const std::vector<CommandDef>& commandTable() {
    static const std::vector<CommandDef> table = {
        // 生命周期
        {"help", cmdHelp, "生命周期", "总览帮助"},
        {"man", cmdMan, "生命周期", "单命令手册"},
        {"version", cmdVersion, "生命周期", "版本与构建信息"},
        {"new", cmdNew, "生命周期", "以种子开新纪元"},
        {"status", cmdStatus, "生命周期", "一屏概览"},
        {"selftest", cmdSelftest, "生命周期", "内置自检"},
        {"verify", cmdVerify, "生命周期", "校验存档完整性"},
        {"slots", cmdSlots, "生命周期", "列出存档槽"},
        {"resume", cmdResume, "生命周期", "恢复最近进度"},
        // 存档
        {"save", cmdSave, "存档", "另存为槽"},
        {"load", cmdLoad, "存档", "载入槽"},
        {"rollback", cmdRollback, "存档", "回退 n 个 tick"},
        {"export", cmdExport, "存档", "导出存档文件"},
        {"import", cmdImport, "存档", "导入存档文件"},
        {"prune", cmdPrune, "存档", "剪枝旧槽"},
        {"delete", cmdDelete, "存档", "删除槽"},
        {"chronicle", cmdChronicle, "存档", "查看防读档哈希链"},

        // 世界
        {"overview", cmdOverview, "世界", "世界总览"},
        {"starmap", cmdStarmap, "世界", "ASCII 星图与航线"},
        {"sectors", cmdSectors, "世界", "星区列表"},
        {"planet", cmdPlanet, "世界", "行星详情"},
        {"planets", cmdPlanets, "世界", "己方行星星表（含编号，供 build 使用）"},
        {"empires", cmdEmpires, "世界", "帝国列表"},
        {"relations", cmdRelations, "世界", "关系矩阵"},
        {"treaties", cmdTreaties, "世界", "条约列表"},
        {"federation", cmdFederation, "世界", "联邦与投票"},
        {"domestic", cmdDomestic, "世界", "国内派系（双层博弈）"},
        {"tech", cmdTech, "世界", "科技树"},
        {"fleets", cmdFleets, "世界", "舰队列表"},
        {"ship", cmdShip, "世界", "舰船设计"},
        {"buildings", cmdBuildings, "世界", "建筑与巨构"},
        {"logs", cmdLogs, "世界", "事件日志（可过滤 AI 阶段）"},
        {"history", cmdHistory, "世界", "历史轨迹与状态哈希"},
        {"replay", cmdReplay, "世界", "从检查点重放并 diff"},

        // 市场
        {"market", cmdMarket, "市场", "盘面速览（档位/spread/σ/V20）"},
        {"book", cmdBook, "市场", "订单簿"},
        {"quote", cmdQuote, "市场", "跨所报价"},
        {"curve", cmdCurve, "市场", "期货期限结构与基差"},
        {"position", cmdPosition, "市场", "持仓"},
        {"pnl", cmdPnl, "市场", "盈亏"},
        {"vol", cmdVol, "市场", "波动率（GARCH）"},
        {"arb", cmdArb, "市场", "跨所套利"},
        {"shock", cmdShock, "市场", "冲击日志"},
        {"credit", cmdCredit, "市场", "信用、债务与托管"},
        {"fx", cmdFx, "市场", "汇率与配给"},
        {"blackmarket", cmdBlackmarket, "市场", "黑市"},
        {"order", cmdOrder, "市场", "下单（限价/市价/冰量）"},
        {"cancel", cmdCancel, "市场", "撤单"},
        {"modify", cmdModify, "市场", "改单"},
        {"futures", cmdFutures, "市场", "期货开仓"},
        {"settle", cmdSettle, "市场", "交割与债务结算"},
        {"borrow", cmdBorrow, "市场", "借款/发债"},
        {"repay", cmdRepay, "市场", "还款"},
        {"escrow", cmdEscrow, "市场", "托管 / 信用证"},
        {"insure", cmdInsure, "市场", "保险"},

        // 外交 / 情报
        {"envoy", cmdEnvoy, "外交", "外交使节"},
        {"spy", cmdSpy, "外交", "间谍行动（含 read-mind）"},
        {"gift", cmdGift, "外交", "赠礼（成本信号）"},
        {"intel", cmdIntel, "外交", "情报 / 透明度报告 / 布置数据"},
        {"propaganda", cmdPropaganda, "外交", "舆论战"},
        {"negotiate", cmdNegotiate, "外交", "谈判（交换 / 单方面索取）"},

        // 道具
        {"inventory", cmdInventory, "道具", "库存道具与规则引擎合计"},
        {"item", cmdItem, "道具", "道具详情与求值轨迹"},
        {"combine", cmdCombine, "道具", "合成"},
        {"use", cmdUse, "道具", "使用道具"},
        {"disassemble", cmdDisassemble, "道具", "拆解回材"},
        {"forge-prove", cmdForgeProve, "道具", "造假冒牌（污染 AI 模型）"},
        {"equip", cmdEquip, "道具", "装备模块"},

        // 线索
        {"clues", cmdClues, "线索", "线索超图"},
        {"link", cmdLink, "线索", "连接线索"},
        {"unlink", cmdUnlink, "线索", "断开线索"},
        {"archive", cmdArchive, "线索", "归档线索"},
        {"deduce", cmdDeduce, "线索", "推断结论（最小充分集）"},
        {"conclusions", cmdConclusions, "线索", "已提交的结论与结局向量"},

        // 运营
        {"colony", cmdColony, "运营", "殖民"},
        {"ship-build", cmdShipBuild, "运营", "建造舰船"},
        {"fleet", cmdFleet, "运营", "舰队命令"},
        {"edict", cmdEdict, "运营", "颁布法令 / 满足派系"},
        {"research", cmdResearch, "运营", "研究"},
        {"build", cmdBuild, "运营", "建造建筑"},
        {"military", cmdMilitary, "军事", "军备（军力上限 / 积累速度 / 补满工期）"},
        {"proposal", cmdProposal, "外交", "AI 提案箱（查看 / 接受 / 拒绝）"},
        {"casus", cmdCasus, "外交", "正当战争理由与战争疲劳"},
        {"effects", cmdEffects, "情报", "增益与减益总览（效果 + 描述）"},
        {"personnel", cmdPersonnel, "运营", "人事（领袖 / 科学家 / 集团军）"},
        {"gov", cmdGov, "运营", "政体（合法性来源 / 选举 / 镇压）"},
        {"design", cmdDesign, "军事", "舰船设计器（舰体 / 模块 / 改造）"},
        {"base", cmdBase, "军事", "恒星基地（建立 / 升级 / 防御与补给）"},
        {"revolt", cmdRevolt, "运营", "起义与党派斗争（动乱 / 镇压 / 派系通牒）"},
        {"species", cmdSpecies, "运营", "种族 / 奴役 / 太空生物 / 施压 / 基因改造"},
        {"queue", cmdQueue, "运营", "建造队列（查看 / 取消 / 清空）"},
        {"corruption", cmdCorruption, "运营", "腐败（态势 / 反腐运动）"},
        {"mega", cmdMega, "运营", "巨构工程"},
        {"ascend", cmdAscend, "运营", "飞升"},
        {"recruit", cmdRecruit, "运营", "招募"},

        // 推进与纪元
        {"advance", cmdAdvance, "推进", "推进 tick（完整 14 阶段流水线）"},
        {"choose", cmdChoose, "推进", "结算待抉择事件"},
        {"defer", cmdDefer, "推进", "延后抉择"},
        {"epoch", cmdEpoch, "纪元", "纪元报告 / 开启下一纪元"},
        {"resolve", cmdResolve, "决议", "决议系统（主动/自动/可阻止/倒计时/牺牲换利）"},
        {"victory", cmdVictory, "决议", "胜利条件进度"},
        {"battles", cmdBattles, "军事", "战斗态势（组织度驱动）"},
        {"front", cmdFront, "军事", "战线总览（前线扇区与兵力分配）"},
        {"commanders", cmdCommanders, "军事", "指挥官（招募 / 分配）"},
        {"policy", cmdPolicy, "决议", "政策（持久化法令 / 分组互斥 / 过渡期）"},
        {"parliament", cmdParliament, "决议", "议会立法（席位 / 表决 / 拉票）"},
        {"trade", cmdTrade, "经济", "贸易路线与关税（开设 / 关税 / 关税战）"},
        {"network", cmdNetwork, "情报", "间谍网络（渗透 / 特工 / 任务 / 破获）"},
        {"peace", cmdPeace, "军事", "和平会议（战争分数 / 割让 / 赔款）"},
    };
    return table;
}

const CommandDef* findCommand(std::string_view name) {
    for (const auto& c : commandTable())
        if (c.name == name) return &c;
    return nullptr;
}

std::vector<std::string_view> commandGroups() {
    std::vector<std::string_view> groups;
    for (const auto& c : commandTable()) {
        bool found = false;
        for (auto g : groups)
            if (g == c.group) found = true;
        if (!found) groups.push_back(c.group);
    }
    return groups;
}

int runCli(int argc, char** argv) {
    Args args = Args::parse(argc, argv);
    CliEnv env{};   // 全局选项在下面统一填充

    // 全局选项
    if (args.has("quiet")) env.quiet = true;
    if (args.has("verbose")) env.verbose = true;
    if (env.quiet && !env.verbose) setVerbose(-1);
    else if (env.verbose) setVerbose(2);
    else setVerbose(0);
    env.noAutosave = args.has("no-autosave");
    env.dryRun = args.has("dry-run");
    env.ascii = args.has("ascii") || args.has("no-color") || std::getenv("NO_COLOR") != nullptr;
    env.noEncrypt = args.has("no-encrypt");
    env.limit = args.getInt("limit", 0);
    env.asOf = args.getInt("as-of", -1);
    if (args.has("slot")) env.slotId = args.get("slot", "main");
    if (args.has("key")) env.passphrase = args.get("key");
    if (args.has("data-dir")) {
        env.dataDir = args.get("data-dir");
    } else if (const char* envDir = std::getenv("GREYFALL_DIR"); envDir && *envDir) {
        env.dataDir = envDir;
    } else {
        env.dataDir = SlotManager::defaultDataDir();
    }
    if (const char* k = std::getenv("GREYFALL_KEY"); k && *k && env.passphrase.empty()) env.passphrase = k;
    env.slots = SlotManager(env.dataDir);
    setColorEnabled(!env.ascii);

    if (args.action().empty() || args.action() == "--help" || args.action() == "-h") {
        return cmdHelp(env, args);
    }
    const CommandDef* cmd = findCommand(args.action());
    if (cmd == nullptr) {
        // 兼容 `greyfall market --res alloys` 这类别名（market → book/quote 组合）
        fail(ExitCode::BadArgs, "未知命令：" + args.action() + "。运行 `greyfall help` 查看全部命令。");
    }
    return cmd->fn(env, args);
}

}  // namespace gf
