#include <filesystem>
#include "save/Lz77.h"
#include <chrono>

#include "cli/Commands.h"
#include "cli/Man.h"
#include "cli/TextTable.h"
#include "core/Errors.h"
#include "core/ResolutionEngine.h"
#include "core/SelfTest.h"
#include "gen/WorldGen.h"
#include "plot/Skeleton.h"
#include "save/Chronicle.h"
#include "save/SaveFile.h"
#include "util/Fmt.h"
#include "util/Str.h"
#include "util/Utf8Width.h"

namespace gf {

int cmdHelp(CliEnv&, const Args& args) {
    if (args.has("short")) {
        out(style("灰域纪元 GREYFALL", Style::Heading) + "  —— 命令一览（man <cmd> 查看详情）");
        for (const auto& c : commandTable()) {
            std::string line = "  " + padRight(std::string(c.name), 14) + std::string(c.summary);
            out(line);
        }
        return 0;
    }
    out(helpText());
    return 0;
}

int cmdMan(CliEnv&, const Args& args) {
    if (args.posCount() == 0) {
        for (const auto& e : manEntries()) {
            std::string line = "  " + padRight(std::string(e.name), 14) + "[" + std::string(e.group) + "] " +
                               std::string(e.summary);
            out(line);
        }
        return 0;
    }
    std::string name = args.pos(0);
    if (findManEntry(name) == nullptr) {
        fail(ExitCode::BadArgs, "没有 `" + name + "` 的手册条目");
    }
    out(manPage(name));
    return 0;
}

int cmdVersion(CliEnv&, const Args&) {
    out(versionText());
    return 0;
}

int cmdStatus(CliEnv& env, const Args&) {
    env.loadState();
    const GameState& st = env.st;
    if (st.empires.empty()) fail(ExitCode::Internal, "状态中没有帝国");
    const Empire& p = st.empires[kPlayerId];

    out(style("═══ 灰域纪元 · " + st.epochName + " ═══", Style::Heading));
    // 败亡状态必须在最上方显眼提示 —— 旧版本玩家失去全部领土后
    // 界面仍显示"存活"，没有任何提示告诉他这一局已经结束了。
    if (st.defeated) out(defeatText(st));
    TextTable t;
    t.header({"项目", "值"});
    t.row({"回合 (tick)", std::to_string(st.tick) + " 季"});
    t.row({"纪元", "#" + std::to_string(st.epochIndex) + (st.endless ? "（无尽）" : "")});
    // 幕次进度必须显示缺口，否则玩家无从知道主线为何不前进。
    // 旧版只显示"第 1 幕 / 7"，而推进条件是「本幕解锁/提交 N 个结论」——
    // 这个 N 从未在游戏内出现过，实测玩家 154 季卡在第一幕却看不到任何线索。
    {
        const ActInfo& act = actInfo(static_cast<int>(st.plot.act));
        int committed = 0;
        for (u16 c : st.plot.committedConclusions)
            if (conclusionDef(static_cast<int>(c)).act == st.plot.act) ++committed;
        int reached = 0;
        for (u16 c : st.plot.conclusionsReached)
            if (conclusionDef(static_cast<int>(c)).act == st.plot.act) ++reached;
        const int progress = std::max(committed, reached);
        std::string actLine = "第 " + std::to_string(static_cast<int>(st.plot.act)) + " 幕 / " +
                              std::to_string(kActCount) + "（本幕 " + std::to_string(progress) + " / " +
                              std::to_string(act.requiredConclusions) + " 项结论）";
        if (st.plot.act >= kActCount) actLine = "全 " + std::to_string(kActCount) + " 幕已完成";
        t.row({"当前幕", actLine});
    }
    t.row({"全局词缀", st.modifierName.empty() ? "无" : st.modifierName});
    t.row({"难度", std::to_string(st.difficulty) + "（AI foresight " + std::to_string(st.aiForesight) + "）"});
    t.row({"帝国", p.name + "（" + std::string(speciesInfo(p.species).nameZh) + " / " +
                       std::string(governmentInfo(p.government).nameZh) + "）"});
    t.row({"国库", fixedStr(p.treasury, 1) + " cr"});
    t.row({"国力指数", fixedStr(p.score, 3)});
    t.row({"行动点", std::to_string(p.apLeft) + " / " + std::to_string(p.apMax)});
    t.row({"待抉择事件", std::to_string(st.pending.size())});
    t.row({"星系 / 行星", std::to_string(p.systems.size()) + " / " +
                              std::to_string([&] {
                                  std::size_t n = 0;
                                  for (const auto& pl : st.planets)
                                      if (pl.owner == p.id && pl.colonized) ++n;
                                  return n;
                              }())});
    t.row({"舰队", std::to_string(p.fleets.size()) + " 支"});
    t.row({"民怨 / 合法性", fixedStr(p.domestic.unrest, 2) + " / " + fixedStr(p.domestic.legitimacy, 2)});
    t.row({"信用评级", fixedStr(p.creditRating, 2)});
    t.row({"读档次数", std::to_string(st.rollbackCount) + "（AI 会看到）"});
    {
        VictoryStatus vs = checkVictory(st);
        t.row({"胜利进度", vs.won ? "★ 已达成征服胜利" :
               ("连续治理 " + std::to_string(vs.sustainedQuarters) + " / " +
                std::to_string(vs.rules.requiredQuarters) + " 季，" +
                std::to_string(vs.unmet.size()) + " 项未达成（victory 查看）")});
    }
    t.row({"活跃帝国", std::to_string(st.aliveEmpires()) + " / " + std::to_string(st.empires.size())});
    out(t.render());

    if (!st.pending.empty()) {
        out("");
        out(style("待抉择事件（advance 会以退出码 5 中止）", Style::Warn));
        TextTable pt;
        pt.header({"#", "事件", "目标"});
        for (std::size_t i = 0; i < st.pending.size(); ++i) {
            const PendingChoice& c = st.pending.items[i];
            pt.row({std::to_string(i), std::string(eventInfo(c.eventId).title),
                    c.scopeTarget < st.empires.size() ? st.empires[c.scopeTarget].name : "-"});
        }
        out(pt.render());
    }
    return 0;
}

int cmdSlots(CliEnv& env, const Args&) {
    std::vector<SlotInfo> slots = env.slots.list();
    if (slots.empty()) {
        out("（没有存档槽）数据目录：" + env.dataDir);
        return 0;
    }
    TextTable t;
    t.header({"槽", "tick", "schema", "大小", "回退", "备份", "检查点", "状态"},
             {Align::Left, Align::Right, Align::Right, Align::Right, Align::Right, Align::Center,
              Align::Center, Align::Left});
    for (const auto& s : slots) {
        t.row({s.id, std::to_string(s.header.createdTick), std::to_string(s.header.schemaVersion),
               humanBytes(s.size), std::to_string(s.header.rollbackCount), s.hasBackup ? "有" : "-",
               s.hasCheckpoints ? "有" : "-", s.valid ? "有效" : "损坏/未知"});
    }
    out(t.render());
    out("数据目录：" + env.dataDir);
    return 0;
}

int cmdVerify(CliEnv& env, const Args& args) {
    std::string id = args.posCount() > 0 ? args.pos(0) : env.slotId;
    if (!env.slots.exists(id)) fail(ExitCode::NoSave, "存档槽 '" + id + "' 不存在");
    std::vector<u8> bytes;
    if (!env.slots.readSlot(id, bytes)) fail(ExitCode::Integrity, "无法读取槽 '" + id + "'");
    SaveHeaderInfo hdr = peekSave(bytes);

    TextTable t;
    t.header({"检查项", "结果"});
    t.row({"文件", env.slots.slotPath(id)});
    t.row({"大小", humanBytes(bytes.size())});
    t.row({"magic", hdr.formatMajor == static_cast<u8>(kFormatMajor) ? "GFAV ✓" : "✗"});
    t.row({"format", std::to_string(hdr.formatMajor) + "." + std::to_string(hdr.formatMinor)});
    t.row({"schemaVersion", std::to_string(hdr.schemaVersion)});
    t.row({"flags", "LZ77=" + std::string((hdr.flags & kFlagLz77) ? "1" : "0") +
                        " 池=" + std::string((hdr.flags & kFlagPool) ? "1" : "0") +
                        " 明文载荷=" + std::string((hdr.flags & kFlagPlainPayload) ? "1" : "0")});
    t.row({"createdTick", std::to_string(hdr.createdTick)});
    t.row({"lastCommitTick", std::to_string(hdr.lastCommitTick)});
    t.row({"rollbackCount", std::to_string(hdr.rollbackCount)});
    t.row({"plainLen / cipherLen", std::to_string(hdr.plainLen) + " / " + std::to_string(hdr.cipherLen)});
    t.row({"压缩率", fixedStrPlain(lz77Ratio(hdr.plainLen, hdr.cipherLen), 3)});
    t.row({"chronicleHead", hexEncode(std::string_view(reinterpret_cast<const char*>(&hdr.chronicleHeadHash), 8))});

    GameState st;
    std::string detail;
    DecodeStatus s = decodeSave(bytes, st, env.passphrase, &detail);
    t.row({"CRC + fastReject + HMAC", s == DecodeStatus::Ok ? "通过 ✓" : std::string(decodeStatusName(s))});
    if (s != DecodeStatus::Ok) {
        out(t.render());
        fail(s == DecodeStatus::VersionTooHigh ? ExitCode::VersionMismatch : ExitCode::Integrity,
             "校验失败：" + detail);
    }
    t.row({"反序列化", "通过 ✓（帝国 " + std::to_string(st.empires.size()) + "，星系 " +
                            std::to_string(st.map.systems.size()) + "，订单 " +
                            std::to_string([&] {
                                std::size_t n = 0;
                                for (const auto& ex : st.market.exchanges)
                                    for (const auto& b : ex.books) n += b.orders.size();
                                return n;
                            }()) + "）"});
    t.row({"状态指纹", hexEncode(std::string_view(reinterpret_cast<const char*>(&st.tick), 0)) +
                            std::to_string(st.fingerprint() % 1000000007ull)});

    bool chainExists = false;
    std::string cfile = Chronicle::pathFor(env.dataDir + "/epoch-" + std::to_string(st.epochIndex));
    (void)Chronicle::head(cfile, &chainExists);
    if (chainExists) {
        std::string err;
        bool ok = Chronicle::verify(cfile, &err);
        t.row({"chronicle 链", ok ? "完整 ✓" : ("断裂：" + err)});
    } else {
        t.row({"chronicle 链", "缺失（触发「档案焚毁」，各方视为背约者）"});
    }
    out(t.render());
    return 0;
}

int cmdResume(CliEnv& env, const Args& args) {
    std::vector<SlotInfo> slots = env.slots.list();
    if (slots.empty()) fail(ExitCode::NoSave, "没有可恢复的存档槽");
    // 选修改时间最新的槽
    std::string best = slots.front().id;
    long long bestTime = -1;
    for (const auto& s : slots) {
        std::filesystem::path p(s.path);
        std::error_code ec;
        auto t = std::filesystem::last_write_time(p, ec);
        long long v = ec ? 0 : static_cast<long long>(t.time_since_epoch().count());
        if (v > bestTime) {
            bestTime = v;
            best = s.id;
        }
    }
    if (args.has("slot")) best = args.get("slot", best);
    env.slotId = best;
    env.loadState();
    out("已恢复槽 '" + best + "'");
    return cmdStatus(env, args);
}

int cmdNew(CliEnv& env, const Args& args) {
    if (env.slots.exists(env.slotId) && !args.has("force")) {
        fail(ExitCode::IllegalAction,
             "槽 '" + env.slotId + "' 已存在。用 --force 覆盖，或 --slot 指定新槽。");
    }
    u64 seed = 0;
    if (args.has("seed")) {
        seed = parseSeed(args.get("seed"));
    } else {
        seed = static_cast<u64>(
            std::chrono::steady_clock::now().time_since_epoch().count());
    }
    int difficulty = static_cast<int>(args.getInt("difficulty", 2));
    if (difficulty < 1 || difficulty > 5) fail(ExitCode::BadArgs, "--difficulty 必须在 1..5");
    int empires = static_cast<int>(args.getInt("empires", 12));
    if (empires < 8 || empires > 16) fail(ExitCode::BadArgs, "--empires 必须在 8..16");

    WorldGenOptions opts;
    opts.seed = seed;
    opts.difficulty = difficulty;
    opts.empireCount = empires;
    opts.systemCount = static_cast<int>(args.getInt("systems", 56 + empires * 2));
    opts.endless = args.has("endless");
    env.st = GameState{};
    generateWorld(env.st, opts);
    env.hasState = true;
    env.chronicleTick(seed);
    env.commit("new", true);

    out(style("新纪元已开启", Style::Good));
    TextTable t;
    t.header({"项目", "值"});
    t.row({"种子", "0x" + [&] {
               char buf[32];
               std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(seed));
               return std::string(buf);
           }()});
    t.row({"纪元名", env.st.epochName});
    t.row({"难度", std::to_string(difficulty) + " / AI foresight " + std::to_string(env.st.aiForesight)});
    t.row({"帝国", std::to_string(env.st.empires.size())});
    t.row({"星系 / 行星", std::to_string(env.st.map.systems.size()) + " / " +
                             std::to_string(env.st.planets.size())});
    t.row({"玩家", env.st.empires[kPlayerId].name + "（" +
                        std::string(speciesInfo(env.st.empires[kPlayerId].species).nameZh) + "）"});
    t.row({"词缀", env.st.modifierName});
    t.row({"存档", env.slots.slotPath(env.slotId)});
    out(t.render());
    return 0;
}

int cmdSelftest(CliEnv& env, const Args& args) {
    (void)env;
    SelfTestResult r = runSelfTest(args.has("verbose"));
    for (const auto& line : r.report) out(line);
    if (r.ok()) {
        out(style("selftest: " + std::to_string(r.passed) + " 项通过 ✓", Style::Good));
        return 0;
    }
    out(style("selftest: " + std::to_string(r.passed) + " 项通过，" + std::to_string(r.failed) + " 项失败 ✗",
              Style::Bad));
    for (const auto& f : r.failures) out("  ✗ " + f);
    return static_cast<int>(ExitCode::Internal);
}

}  // namespace gf
