// 起义与党派斗争
#include "cli/Commands.h"
#include "cli/TextTable.h"
#include "core/Errors.h"
#include "domain/Revolt.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {

int cmdRevolt(CliEnv& env, const Args& args) {
    env.loadState();
    std::string msg;

    if (args.has("suppress")) {
        i64 sys = args.getInt("suppress", -1);
        if (sys < 0) fail(ExitCode::BadArgs, "用法：--suppress <星系>");
        if (!suppressRevolt(env.st, kPlayerId, static_cast<u32>(sys), &msg))
            fail(ExitCode::IllegalAction, msg);
        env.commit("revolt");
        out(style(msg, Style::Good));
        return 0;
    }
    if (args.has("concede")) {
        if (!answerUltimatum(env.st, kPlayerId, true, &msg)) fail(ExitCode::IllegalAction, msg);
        env.commit("revolt");
        out(style(msg, Style::Good));
        return 0;
    }
    if (args.has("refuse")) {
        if (!answerUltimatum(env.st, kPlayerId, false, &msg)) fail(ExitCode::IllegalAction, msg);
        env.commit("revolt");
        out(style(msg, Style::Warn));
        return 0;
    }

    out(style("═══ 境内动乱 ═══", Style::Heading));
    out("  动乱分四个阶段：平静 → 不安 → 叛乱 → **割据**。");
    out("  割据状态持续 8 季未平息，该星系将脱离控制（自立或倒向邻国）。");
    out("");
    out(revoltReport(env.st, kPlayerId));
    out(style("═══ 派系格局 ═══", Style::Heading));
    out(factionStruggleReport(env.st, kPlayerId));

    FactionKind which = FactionKind::Count;
    if (factionUltimatumPending(env.st, kPlayerId, &which)) {
        out(style("⚠ 最后通牒", Style::Warn));
        out("  【" + std::string(factionKindName(which)) + "】发出了最后通牒。");
        out("    greyfall revolt --concede   满足诉求（30,000 cr；其满意度 +35%，其他派系 -10%）");
        out("    greyfall revolt --refuse    拒绝（其满意度 -25%，民怨 +8%，政变风险 +12%）");
        out("");
    }
    out("用法：greyfall revolt --suppress <星系>   镇压动乱（20,000 cr + 300 军力）");
    out("      greyfall revolt --concede          满足派系诉求");
    out("      greyfall revolt --refuse           拒绝最后通牒");
    return 0;
}

}  // namespace gf
