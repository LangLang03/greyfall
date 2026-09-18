#include <filesystem>

#include "gen/WorldGen.h"
#include <string>

#include "ai/AiCore.h"
#include "cli/Commands.h"
#include "cli/TextTable.h"
#include "clue/ClueGraph.h"
#include "core/Errors.h"
#include "core/TickPipeline.h"
#include "mkt/OrderBook.h"
#include "plot/EventSystem.h"
#include "plot/BeatResolver.h"
#include "save/Chronicle.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {

int cmdAdvance(CliEnv& env, const Args& args) {
    env.loadState();
    i64 n = args.has("ticks") ? args.getInt("ticks", 1) : (args.posCount() > 0 ? args.posInt(0, 1) : 1);
    if (n <= 0) fail(ExitCode::BadArgs, "--ticks 必须为正");
    if (n > 400) fail(ExitCode::BadArgs, "--ticks 过大（上限 400）");

    if (env.st.pending.hasBlocking(env.st.tick)) {
        out(style("存在待抉择事件，advance 中止（退出码 5）", Style::Warn));
        for (std::size_t i = 0; i < env.st.pending.size(); ++i) out(pendingText(env.st, env.st.pending.items[i]));
        out("用 `greyfall choose <index>` 结算，或 `greyfall defer --ticks N` 延后。");
        return static_cast<int>(ExitCode::PendingChoice);
    }

    u64 actionDigest = env.st.fingerprint();
    int rc = 0;
    int ticksDone = 0;
    for (i64 i = 0; i < n; ++i) {
        if (env.st.pending.hasBlocking(env.st.tick)) {
            rc = static_cast<int>(ExitCode::PendingChoice);
            break;
        }
        TickReport rep = advanceOneTick(env.st);
        ++ticksDone;
        env.chronicleTick(actionDigest);
        if (verboseLevel() >= 1) {
            std::string line = "  tick " + std::to_string(env.st.tick) + "：成交 " + std::to_string(rep.fills) +
                               " 笔，名义额 " + fixedStr(rep.notional, 0) + "，AI 动作 " +
                               std::to_string(rep.aiActions) + "，事件 " + std::to_string(rep.eventsFired) +
                               "，新线索 " + std::to_string(rep.cluesDiscovered);
            out(line);
        }
        if (env.st.pending.hasBlocking(env.st.tick)) {
            rc = static_cast<int>(ExitCode::PendingChoice);
            break;
        }
    }

    env.commit("advance");

    if (rc == static_cast<int>(ExitCode::PendingChoice)) {
        out(style("推进 " + std::to_string(ticksDone) + " 季后出现待抉择事件（退出码 5）", Style::Warn));
        for (std::size_t i = 0; i < env.st.pending.size(); ++i) out(pendingText(env.st, env.st.pending.items[i]));
        out("用 `greyfall choose <index>` 结算。");
        return rc;
    }

    out(stateSummaryLine(env.st));
    out("已完成 " + std::to_string(ticksDone) + " 季推进。下一季：" +
        std::string(actTitle(static_cast<int>(env.st.plot.act))));
    // 失败优先于剧情结局：两者可能同时成立（例如在败亡当季也触发了剧情结局）。
    if (env.st.defeated) {
        out(defeatText(env.st));
    } else if (env.st.ended) {
        const EndingInfo& en = endingInfo(env.st.endingId);
        out(style("【结局】" + std::string(en.nameZh), Style::Heading));
        out(wrapJoin(en.text, 86, "  "));
    }
    return 0;
}

int cmdChoose(CliEnv& env, const Args& args) {
    env.loadState();
    if (env.st.pending.empty()) fail(ExitCode::IllegalAction, "当前没有待抉择事件");
    i64 idx = args.posCount() > 0 ? args.posInt(0, 0) : args.getInt("index", 0);
    i64 opt = args.has("option") ? args.getInt("option", 0) : (args.posCount() > 1 ? args.posInt(1, 0) : 0);
    if (idx < 0 || idx >= static_cast<i64>(env.st.pending.size())) {
        fail(ExitCode::BadArgs, "抉择序号越界（0.." + std::to_string(env.st.pending.size() - 1) + "）");
    }
    if (args.posCount() == 1 && !args.has("option")) {
        // 只给了序号：打印选项让玩家选。
        //
        // 这种情况**必须返回非 0 退出码**：本命令的文档形式是
        // `choose <index> <option>`，缺 <option> 属于用法错误。
        // 旧实现返回 0，于是脚本无法区分「结算成功」与「只是打印了选项」——
        // 实测一个 `if [ $? -eq 5 ]; then choose 0; fi` 的自动化循环会
        // 静默空转 40 季（exit 0 既不是成功推进也不是待抉择信号）。
        // 项目自述「可安全写进脚本、放进 CI」，这个返回值是其中的关键一环。
        out(pendingText(env.st, env.st.pending.items[static_cast<std::size_t>(idx)]));
        out(style("缺少选项参数：结算请用 `greyfall choose " + std::to_string(idx) +
                      " <option>`（0.." + std::to_string(
                          env.st.pending.items[static_cast<std::size_t>(idx)].options.empty()
                              ? 1
                              : env.st.pending.items[static_cast<std::size_t>(idx)].options.size() - 1) +
                      "）",
                  Style::Warn));
        return static_cast<int>(ExitCode::BadArgs);
    }
    std::string err;
    if (!resolveChoice(env.st, static_cast<std::size_t>(idx), static_cast<int>(opt), &err)) {
        fail(ExitCode::IllegalAction, err);
    }
    env.commit("choose");
    out("已结算抉择 #" + std::to_string(idx) + " → 选项 [" + std::to_string(opt) + "]");
    if (env.st.pending.hasBlocking(env.st.tick)) {
        out("仍有 " + std::to_string(env.st.pending.size()) + " 个待抉择事件。");
        return static_cast<int>(ExitCode::PendingChoice);
    }
    return 0;
}

int cmdDefer(CliEnv& env, const Args& args) {
    env.loadState();
    if (env.st.pending.empty()) fail(ExitCode::IllegalAction, "当前没有待抉择事件");
    i64 idx = args.posCount() > 0 ? args.posInt(0, 0) : 0;
    i64 n = args.getInt("ticks", 1);
    if (n <= 0 || n > 400) fail(ExitCode::BadArgs, "--ticks 必须在 1..400");
    std::string err;
    if (!deferChoice(env.st, static_cast<std::size_t>(idx), static_cast<int>(n), &err)) {
        fail(ExitCode::IllegalAction, err);
    }
    env.commit("defer");
    out(style("已延后抉择 " + std::to_string(idx) + " 共 " + std::to_string(n) + " 季", Style::Warn));
    out("  代价已立即支付：民怨上升，相关方观感下降。拖延超过 3 季会持续加价。");
    return env.st.pending.hasBlocking(env.st.tick) ? static_cast<int>(ExitCode::PendingChoice) : 0;
}

int cmdEpoch(CliEnv& env, const Args& args) {
    if (args.has("seed") && !args.has("next")) {
        // 以指定种子重开一个纪元
        Args a = args;
        a.setAction("new");
        return cmdNew(env, a);
    }

    env.loadState(false);
    if (!env.hasState) {
        fail(ExitCode::NoSave, "没有纪元可报告。先运行：greyfall new --seed <S>");
    }

    if (args.has("report") || (!args.has("next") && !args.has("endless"))) {
        out(epochReport(env.st));
        return 0;
    }

    if (args.has("next")) {
        // 通关判定
        int committed = static_cast<int>(env.st.plot.committedConclusions.size());
        if (committed < 8 && !args.has("force")) {
            fail(ExitCode::IllegalAction,
                 "尚未通关（已提交结论 " + std::to_string(committed) +
                     " / 至少 8）。用 --force 强制开启下一纪元。");
        }
        u64 legacy = 0;
        if (args.has("inherit-legacy")) {
            legacy = kLegacyInstitutions | kLegacyDebt | kLegacyEnemies | kLegacyTech | kLegacyReputation |
                     kLegacyBrokenMinds;
        }
        GameState old = env.st;
        WorldGenOptions o;
        o.seed = old.seed ^ (0x9E3779B97F4A7C15ull * (old.epochIndex + 1));
        o.difficulty = std::min(5, old.difficulty + 1);
        o.empireCount = static_cast<int>(old.empires.size());
        o.systemCount = static_cast<int>(old.map.systems.size());
        o.endless = old.endless || args.has("endless");
        o.epochIndex = old.epochIndex + 1;
        o.legacyMask = legacy;
        o.legacyNote = old.epochName;

        GameState fresh;
        generateWorld(fresh, o);

        // 遗产继承
        if (legacy & kLegacyInstitutions) {
            fresh.empires[kPlayerId].tech.completed = old.empires[kPlayerId].tech.completed;
            fresh.empires[kPlayerId].influence = old.empires[kPlayerId].influence;
        }
        if (legacy & kLegacyTech) {
            fresh.empires[kPlayerId].tech.progress = old.empires[kPlayerId].tech.progress;
        }
        if (legacy & kLegacyDebt) {
            fresh.market.debts = old.market.debts;
            fresh.empires[kPlayerId].treasury -= old.empires[kPlayerId].treasury / Fixed(2);
            fresh.market.margin.cash = fresh.empires[kPlayerId].treasury;
        }
        if (legacy & kLegacyEnemies) {
            for (std::size_t i = 0; i < fresh.empires.size() && i < old.empires.size(); ++i) {
                fresh.empires[i].setOpinion(kPlayerId, old.empires[i].opinionOf(kPlayerId));
                fresh.relations[i * kMaxEmpires + kPlayerId].atWar = old.relations[i * kMaxEmpires + kPlayerId].atWar;
            }
        }
        if (legacy & kLegacyReputation) {
            for (std::size_t i = 0; i < fresh.empires.size() && i < old.empires.size(); ++i) {
                fresh.empires[i].mind.reputation[kPlayerId] = old.empires[i].mind.reputation[kPlayerId];
                fresh.empires[i].mind.grudge[kPlayerId] = old.empires[i].mind.grudge[kPlayerId];
                fresh.empires[i].mind.playerModel = old.empires[i].mind.playerModel;
            }
        }
        if (legacy & kLegacyBrokenMinds) {
            // 被攻破的心智模型：AI 从上一纪元就认识你
            for (auto& e : fresh.empires) {
                if (e.isPlayer) continue;
                e.mind.playerModel.modelConfidence = old.empires[kPlayerId].score.rawValue() > 0 ? Fixed::pct(70) : Fixed::pct(40);
                e.mind.foresight = static_cast<u8>(std::min(4, static_cast<int>(e.mind.foresight) + 1));
            }
        }
        fresh.rollbackCount = 0;
        fresh.chronicleHead = 0;
        fresh.logEvent(LogPhase::Chronicle, kLogNewGame,
                       "纪元 #" + std::to_string(fresh.epochIndex) + " 开启：继承自【" + o.legacyNote + "】" +
                           (legacy ? "（含遗产继承）" : "（不继承遗产）"));

        env.st = std::move(fresh);
        env.hasState = true;
        env.chronicleTick(o.seed);
        // 新纪元 -> 新的 chronicle 文件
        std::error_code ec;
        std::filesystem::create_directories(env.epochDir(), ec);
        env.commit("epoch --next", true);
        out(style("新纪元已开启：#" + std::to_string(env.st.epochIndex) + " " + env.st.epochName, Style::Good));
        out("难度 → " + std::to_string(env.st.difficulty) + "，AI foresight → " +
            std::to_string(env.st.aiForesight) + "，词缀：" + env.st.modifierName);
        if (legacy) out("已继承遗产：制度 / 债务 / 仇敌 / 技术 / 信誉 / 被攻破的心智模型");
        return 0;
    }

    if (args.has("endless")) {
        env.st.endless = true;
        env.commit("epoch --endless", true);
        out("已切换为无尽模式：结局不会终止纪元。");
        return 0;
    }
    return 0;
}

}  // namespace gf
