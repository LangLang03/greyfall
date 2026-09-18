// 人事：领袖、科学家、集团军
#include "cli/Commands.h"
#include "util/TextTable.h"
#include "core/Errors.h"
#include "domain/Government.h"
#include "domain/Personnel.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {

int cmdGov(CliEnv& env, const Args& args) {
    env.loadState();
    if (args.has("elect")) {
        std::string msg;
        if (!callElection(env.st, kPlayerId, &msg)) fail(ExitCode::IllegalAction, msg);
        env.commit("gov");
        out(style(msg, Style::Good));
        out(electionText(env.st, kPlayerId));
        return 0;
    }
    if (args.has("support")) {
        i64 idx = args.getInt("support", -1);
        if (idx < 0) fail(ExitCode::BadArgs, "用法：--support <候选人编号>");
        std::string msg;
        if (!endorseCandidate(env.st, kPlayerId, static_cast<int>(idx), &msg))
            fail(ExitCode::IllegalAction, msg);
        env.commit("gov");
        out(style(msg, Style::Good));
        out(electionText(env.st, kPlayerId));
        return 0;
    }
    if (args.has("suppress")) {
        std::string msg;
        if (!suppress(env.st, kPlayerId, &msg)) fail(ExitCode::IllegalAction, msg);
        env.commit("gov");
        out(style(msg, Style::Warn));
        return 0;
    }
    out(style("═══ 政体 ═══", Style::Heading));
    out(governmentReport(env.st, kPlayerId));
    out(style("选举", Style::Sub));
    out(electionText(env.st, kPlayerId));
    out("用法：greyfall gov --elect          提前改选（仅选举制）");
    out("      greyfall gov --support <编号> 公开支持某位候选人（200 影响力）");
    out("      greyfall gov --suppress       镇压（仅威权制；合法性 +5%，怨恨 +12%）");
    return 0;
}

int cmdPersonnel(CliEnv& env, const Args& args) {
    env.loadState();
    const bool op = args.has("recruit") || args.has("assign") || args.has("form") ||
                    args.has("add") || args.has("remove") || args.has("lead") ||
                    args.has("disband");

    if (!op) {
        out(style("═══ 领袖 ═══", Style::Heading));
        out(rulerReport(env.st, kPlayerId));
        out(style("═══ 科研部 ═══", Style::Heading));
        out(scientistReport(env.st, kPlayerId));
        out(style("═══ 集团军 ═══", Style::Heading));
        out(formationReport(env.st, kPlayerId));
        out("");
        out("用法：greyfall personnel --recruit              招募科学家（150 影响力）");
        out("      greyfall personnel --assign <科学家> <分支>  派往某研究分支");
        out("      greyfall personnel --form [名称]            新建集团军");
        out("      greyfall personnel --add <集团军> <舰队>     编入舰队");
        out("      greyfall personnel --remove <集团军> <舰队>  移出舰队");
        out("      greyfall personnel --lead <集团军> <指挥官>  任命集团军司令");
        out("      greyfall personnel --disband <集团军>        解散");
        return 0;
    }

    std::string msg;
    if (args.has("recruit")) {
        if (!scientistRecruit(env.st, kPlayerId, &msg)) fail(ExitCode::IllegalAction, msg);
        env.commit("personnel");
        out(style(msg, Style::Good));
        return 0;
    }
    if (args.has("assign")) {
        i64 sci = args.getInt("assign", -1);
        i64 branch = args.getInt("branch", -1);
        if (sci < 0 || branch < 0) fail(ExitCode::BadArgs, "用法：--assign <科学家> --branch <0-5>");
        if (!scientistAssign(env.st, kPlayerId, static_cast<u32>(sci), static_cast<int>(branch), &msg))
            fail(ExitCode::IllegalAction, msg);
        env.commit("personnel");
        out(style(msg, Style::Good));
        return 0;
    }
    if (args.has("form")) {
        if (!formationCreate(env.st, kPlayerId, args.get("form"), &msg))
            fail(ExitCode::IllegalAction, msg);
        env.commit("personnel");
        out(style(msg, Style::Good));
        return 0;
    }
    if (args.has("add")) {
        i64 fid = args.getInt("add", -1);
        i64 fl = args.getInt("fleet", -1);
        if (fid < 0 || fl < 0) fail(ExitCode::BadArgs, "用法：--add <集团军> --fleet <舰队>");
        if (!formationAddFleet(env.st, kPlayerId, static_cast<u32>(fid), static_cast<u32>(fl), &msg))
            fail(ExitCode::IllegalAction, msg);
        env.commit("personnel");
        out(style(msg, Style::Good));
        return 0;
    }
    if (args.has("remove")) {
        i64 fid = args.getInt("remove", -1);
        i64 fl = args.getInt("fleet", -1);
        if (fid < 0 || fl < 0) fail(ExitCode::BadArgs, "用法：--remove <集团军> --fleet <舰队>");
        if (!formationRemoveFleet(env.st, kPlayerId, static_cast<u32>(fid), static_cast<u32>(fl), &msg))
            fail(ExitCode::IllegalAction, msg);
        env.commit("personnel");
        out(style(msg, Style::Warn));
        return 0;
    }
    if (args.has("lead")) {
        i64 fid = args.getInt("lead", -1);
        i64 cid = args.getInt("commander", -1);
        if (fid < 0 || cid < 0) fail(ExitCode::BadArgs, "用法：--lead <集团军> --commander <指挥官>");
        if (!formationAssignCommander(env.st, kPlayerId, static_cast<u32>(fid), static_cast<u32>(cid), &msg))
            fail(ExitCode::IllegalAction, msg);
        env.commit("personnel");
        out(style(msg, Style::Good));
        return 0;
    }
    if (args.has("disband")) {
        i64 fid = args.getInt("disband", -1);
        if (fid < 0) fail(ExitCode::BadArgs, "用法：--disband <集团军>");
        if (!formationDisband(env.st, kPlayerId, static_cast<u32>(fid), &msg))
            fail(ExitCode::IllegalAction, msg);
        env.commit("personnel");
        out(style(msg, Style::Warn));
        return 0;
    }
    fail(ExitCode::BadArgs, "未知操作。用 `greyfall personnel` 查看用法");
}

}  // namespace gf
