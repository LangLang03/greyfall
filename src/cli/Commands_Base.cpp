// 恒星基地：星系级永久设施
#include "cli/Commands.h"
#include "cli/TextTable.h"
#include "core/Errors.h"
#include "domain/Starbase.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {

int cmdBase(CliEnv& env, const Args& args) {
    env.loadState();
    std::string msg;

    if (args.has("found")) {
        i64 sys = args.getInt("found", -1);
        if (sys < 0) fail(ExitCode::BadArgs, "用法：--found <星系>");
        if (!starbaseFound(env.st, kPlayerId, static_cast<u32>(sys), &msg))
            fail(ExitCode::IllegalAction, msg);
        env.commit("base");
        out(style(msg, Style::Good));
        return 0;
    }
    if (args.has("upgrade")) {
        i64 sys = args.getInt("upgrade", -1);
        if (sys < 0) fail(ExitCode::BadArgs, "用法：--upgrade <星系>");
        if (!starbaseUpgrade(env.st, kPlayerId, static_cast<u32>(sys), &msg))
            fail(ExitCode::IllegalAction, msg);
        env.commit("base");
        out(style(msg, Style::Good));
        return 0;
    }
    if (args.has("dismantle")) {
        i64 sys = args.getInt("dismantle", -1);
        if (sys < 0) fail(ExitCode::BadArgs, "用法：--dismantle <星系>");
        if (!starbaseDismantle(env.st, kPlayerId, static_cast<u32>(sys), &msg))
            fail(ExitCode::IllegalAction, msg);
        env.commit("base");
        out(style(msg, Style::Warn));
        return 0;
    }

    out(style("═══ 恒星基地 ═══", Style::Heading));
    out("  与行星建筑不同：基地建在**星系**上，是主权的实体标志，");
    out("  提供防御（计入防守方战力）、舰队补给与维修。");
    out("");
    out(starbaseReport(env.st, kPlayerId));
    out("");
    out(style("升级链", Style::Sub));
    TextTable t;
    t.header({"等级", "信用点", "合金", "防御", "补给", "说明"});
    for (int i = 0; i < static_cast<int>(StarbaseTier::Count); ++i) {
        StarbaseTier tier = static_cast<StarbaseTier>(i);
        t.row({std::string(starbaseTierName(tier)),
               std::to_string(starbaseUpgradeCredits(tier)),
               std::to_string(starbaseUpgradeAlloys(tier)),
               fixedStr(starbaseDefense(tier), 0),
               "+" + fixedStrPlain(starbaseSupplyBonus(tier) * Fixed(100), 0) + "%",
               starbaseTierDesc(tier)});
    }
    out(t.render());
    out("用法：greyfall base --found <星系>      建立前哨站");
    out("      greyfall base --upgrade <星系>    升级一级");
    out("      greyfall base --dismantle <星系>  拆除（不返还资源）");
    return 0;
}

}  // namespace gf
