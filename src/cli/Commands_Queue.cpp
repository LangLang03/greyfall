// 建造队列：查看 / 取消 / 清空
#include "cli/Commands.h"
#include "util/TextTable.h"
#include "core/Errors.h"
#include "domain/Construction.h"
#include "domain/Fleet.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {

int cmdQueue(CliEnv& env, const Args& args) {
    env.loadState();
    std::string msg;

    if (args.has("cancel-project")) {
        const i64 index = args.getInt("cancel-project", -1);
        if (index < 0) fail(ExitCode::BadArgs, "用法：queue --cancel-project <项目号>");
        if (!cancelDevelopment(env.st, kPlayerId, static_cast<std::size_t>(index), &msg)) fail(ExitCode::IllegalAction, msg);
        env.commit("queue"); out(msg); return 0;
    }
    if (args.has("cancel")) {
        i64 pid = args.getInt("cancel", -1);
        i64 idx = args.getInt("index", 0);
        if (pid < 0) fail(ExitCode::BadArgs, "用法：--cancel <行星> [--index n]");
        if (!cancelBuildOrder(env.st, kPlayerId, static_cast<u32>(pid),
                              static_cast<std::size_t>(idx < 0 ? 0 : idx), &msg))
            fail(ExitCode::IllegalAction, msg);
        env.commit("queue");
        out(style(msg, Style::Warn));
        return 0;
    }
    if (args.has("clear")) {
        i64 pid = args.getInt("clear", -1);
        if (pid < 0) fail(ExitCode::BadArgs, "用法：--clear <行星>");
        if (!clearBuildQueue(env.st, kPlayerId, static_cast<u32>(pid), &msg))
            fail(ExitCode::IllegalAction, msg);
        env.commit("queue");
        out(style(msg, Style::Warn));
        return 0;
    }

    out(style("═══ 建造队列 ═══", Style::Heading));
    out("  建筑不再即时完成：动工时付款，逐季推进，完工后自动启用。");
    out("  建造速度受**建造速率修正**、行星稳定度与政体效率影响。");
    out("");
    out(buildQueueReport(env.st, kPlayerId));
    out(style("殖民与国家工程", Style::Sub));
    out(developmentReport(env.st, kPlayerId));
    out("");
    out(style("在建舰船", Style::Sub));
    for (const auto& ship : env.player().shipQueue)
        out("  设计 #" + std::to_string(ship.design) + "，星系 #" + std::to_string(ship.system) +
            "，剩余 " + std::to_string(ship.ticksLeft) + "/" + std::to_string(ship.totalTicks) + " 季");
    out(style("造舰工期参考", Style::Sub));
    TextTable t;
    t.header({"舰体", "基础工期"});
    for (int i = 0; i < static_cast<int>(HullClass::Count); ++i) {
        t.row({std::string(hullInfo(static_cast<HullClass>(i)).nameZh),
               std::to_string(hullBaseTicks(i)) + " 季"});
    }
    out(t.render());
    out("  造舰需要首都或目标星系建有【轨道船坞】；速度受建造速率修正影响。");
    out("");
    out("用法：greyfall queue                     查看队列");
    out("      greyfall queue --cancel <行星> [--index n]  取消一项（退还 40%）");
    out("      greyfall queue --cancel-project <项目号>  取消国家工程（退 40% 启动资金，物资不退）");
    out("      greyfall queue --clear <行星>       清空该行星队列");
    return 0;
}

}  // namespace gf
