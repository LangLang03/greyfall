// 腐败
#include "cli/Commands.h"
#include "cli/TextTable.h"
#include "util/Fmt.h"
#include "core/Errors.h"
#include "domain/Corruption.h"
#include "util/Str.h"

namespace gf {

int cmdCorruption(CliEnv& env, const Args& args) {
    env.loadState();
    if (args.has("purge")) {
        std::string msg;
        if (!antiCorruption(env.st, kPlayerId, &msg)) fail(ExitCode::IllegalAction, msg);
        env.commit("corruption");
        out(style(msg, Style::Good));
        return 0;
    }
    out(style("═══ 腐败 ═══", Style::Heading));
    out("  腐败随疆域扩张而上升：行星与星系越多，行政越难覆盖。");
    out("  它**直接抽走收入**并推高民怨 —— 这是帝国规模的真实代价。");
    out("  政体决定倾向：资本主义 +30%，民主主义 -12%，社会主义 -4%。");
    out("");
    out(corruptionReport(env.st, kPlayerId));
    out("");
    out("用法：greyfall corruption           查看腐败态势");
    out("      greyfall corruption --purge   反腐运动（25,000 cr，腐败 -35%）");
    return 0;
}

}  // namespace gf
