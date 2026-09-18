// 间谍网络 CLI
#include <algorithm>

#include "cli/Commands.h"
#include "util/TextTable.h"
#include "core/Errors.h"
#include "domain/SpyNetwork.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {
namespace {

int parseTarget(CliEnv& env, const std::string& s) {
    i64 n = parseInt(s, -1);
    if (n >= 0 && n < static_cast<i64>(env.st.empires.size())) return static_cast<int>(n);
    for (const auto& e : env.st.empires)
        if (e.name == s) return static_cast<int>(e.id);
    return -1;
}

}  // namespace

int cmdNetwork(CliEnv& env, const Args& args) {
    env.loadState();
    const bool hasOp = args.has("establish") || args.has("agents") || args.has("mission") ||
                       args.has("disband") || args.has("detail") || args.has("counter");

    if (!hasOp) {
        out(style("═══ 情报网络 ═══", Style::Heading));
        out("  与 `spy`（一次性行动）不同：网络是**持续性**的情报基础设施。");
        out("  渗透度随时间累积，解锁更强任务；但渗透越深、特工越多，越容易被反间谍破获。");
        out("  被破获会损失声望与关系，并作废该网络。");
        out("");
        out(spyAgencyText(env.st, kPlayerId));
        out("");
        out(style("可执行任务", Style::Sub));
        TextTable t;
        t.header({"任务", "最低渗透度", "消耗渗透", "经费", "效果"});
        t.row({"侦察", "10%", "5%", "200", "获得目标国力/军力/经济明细"});
        t.row({"煽动", "25%", "10%", "600", "目标民怨 +9%"});
        t.row({"窃取科技", "40%", "14%", "800", "获得研究点"});
        t.row({"破坏", "55%", "18%", "1,200", "目标经济 -6%、稳定度 -4%"});
        t.row({"潜伏", "65%", "8%", "1,500", "渗透度 +18%"});
        out(t.render());
        out("");
        out(style("反渗透态势", Style::Sub));
        out("  正在渗透**我国**的外国网络（用 `--counter` 清剿）：");
        out(counterIntelReport(env.st, kPlayerId));
        out("");
        out("用法：greyfall network --establish <帝国>          建立网络（5000 cr）");
        out("      greyfall network --counter                 反渗透：清剿境内外国网络");
        out("      greyfall network --agents <帝国> <±n>        增派 / 撤回特工");
        out("      greyfall network --mission <帝国> <任务>      执行任务");
        out("      greyfall network --detail <帝国>             网络详情");
        out("      greyfall network --disband <帝国>            撤销网络");
        return 0;
    }

    // --establish <emp>
    if (args.has("counter")) {
        std::string msg;
        if (!counterInfiltrate(env.st, kPlayerId, &msg)) fail(ExitCode::IllegalAction, msg);
        env.commit("network");
        out(style(msg, Style::Good));
        return 0;
    }
    if (args.has("establish")) {
        int tgt = parseTarget(env, args.get("establish"));
        if (tgt < 0) fail(ExitCode::BadArgs, "未知帝国【" + args.get("establish") + "】");
        std::string err;
        if (!spyEstablish(env.st, kPlayerId, static_cast<u32>(tgt), &err)) {
            fail(ExitCode::IllegalAction, err);
        }
        env.commit("network");
        out(style("已在 " + env.st.empires[static_cast<std::size_t>(tgt)].name + " 建立情报网络",
                  Style::Good));
        out("  当前渗透度 5%（每季增长，受对方情报防御压制）");
        out("  暴露风险随渗透度与特工数上升，达到 100% 即被破获。");
        return 0;
    }

    // --agents <emp> <±n>
    if (args.has("agents")) {
        int tgt = parseTarget(env, args.get("agents"));
        if (tgt < 0) fail(ExitCode::BadArgs, "未知帝国【" + args.get("agents") + "】");
        if (args.posCount() < 1) fail(ExitCode::BadArgs, "用法：--agents <帝国> <±n>");
        i64 delta = parseInt(args.pos(0), 0);
        std::string err;
        if (!spyAssignAgents(env.st, kPlayerId, static_cast<u32>(tgt), static_cast<int>(delta), &err)) {
            fail(ExitCode::IllegalAction, err);
        }
        env.commit("network");
        out("已调整派驻特工 " + std::string(delta >= 0 ? "+" : "") + std::to_string(delta));
        out(spyNetworkText(env.st, kPlayerId, static_cast<u32>(tgt)));
        return 0;
    }

    // --mission <emp> <mission>
    if (args.has("mission")) {
        int tgt = parseTarget(env, args.get("mission"));
        if (tgt < 0) fail(ExitCode::BadArgs, "未知帝国【" + args.get("mission") + "】");
        if (args.posCount() < 1) fail(ExitCode::BadArgs, "用法：--mission <帝国> <任务>");
        SpyMission m = spyMissionFromName(args.pos(0));
        if (m == SpyMission::Count) fail(ExitCode::BadArgs, "未知任务【" + args.pos(0) + "】");
        std::string err;
        bool ok = spyRunMission(env.st, kPlayerId, static_cast<u32>(tgt), m, &err);
        if (!err.empty() && !ok) fail(ExitCode::IllegalAction, err);
        env.commit("network");
        out(ok ? style("任务成功", Style::Good) : style("任务失败", Style::Bad));
        out(spyNetworkText(env.st, kPlayerId, static_cast<u32>(tgt)));
        if (ok) {
            const Empire* t = env.st.empire(static_cast<u32>(tgt));
            if (t != nullptr)
                out("  目标国力 " + fixedStr(t->score, 1) + "   军力 " + fixedStr(t->military, 0) +
                    "   经济 " + fixedStr(t->economy, 0));
        }
        return 0;
    }

    // --detail <emp>
    if (args.has("detail")) {
        int tgt = parseTarget(env, args.get("detail"));
        if (tgt < 0) fail(ExitCode::BadArgs, "未知帝国【" + args.get("detail") + "】");
        const Empire* t = env.st.empire(static_cast<u32>(tgt));
        out(style("情报网络：" + std::string(t ? t->name : "?"), Style::Heading));
        out(spyNetworkText(env.st, kPlayerId, static_cast<u32>(tgt)));
        out("  对方情报防御：" + fixedStrPlain(counterIntelOf(env.st, static_cast<u32>(tgt)) * Fixed(100), 0) +
            "%（越高则渗透越慢、暴露越快）");
        return 0;
    }

    // --disband <emp>
    if (args.has("disband")) {
        int tgt = parseTarget(env, args.get("disband"));
        if (tgt < 0) fail(ExitCode::BadArgs, "未知帝国【" + args.get("disband") + "】");
        std::string err;
        if (!spyDisband(env.st, kPlayerId, static_cast<u32>(tgt), &err)) {
            fail(ExitCode::IllegalAction, err);
        }
        env.commit("network");
        out("已撤销网络，特工已回收");
        return 0;
    }

    fail(ExitCode::BadArgs, "未知操作。用 `greyfall network` 查看全部用法");
}

}  // namespace gf
