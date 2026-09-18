// 正当战争理由与战争疲劳
#include "cli/Commands.h"
#include "util/TextTable.h"
#include "core/Errors.h"
#include "domain/CasusBelli.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {

int cmdCasus(CliEnv& env, const Args& args) {
    env.loadState();
    out(style("═══ 正当战争理由 ═══", Style::Heading));
    out("  没有正当理由也可以开战，但会遭全体第三方谴责、国内反弹。");
    out("");
    out(casusBelliReport(env.st, kPlayerId));
    out("");
    out(style("═══ 战争疲劳 ═══", Style::Heading));
    out("  疲劳随战争持续上升；超过 40% 开始扣稳定、加民怨，");
    out("  超过 70% 派系会公开要求停战（继续打则满意度 -18%）。停战后自动消除。");
    out("");
    out(wearinessReport(env.st, kPlayerId));
    if (args.has("all")) {
        out("");
        out(style("═══ 各国理由概览 ═══", Style::Heading));
        TextTable t;
        t.header({"帝国", "持有理由数", "说明"});
        for (const auto& e : env.st.empires) {
            if (!e.alive) continue;
            int live = 0;
            for (const auto& c : e.casusBelli)
                if (c.expireTick < 0 || env.st.tick <= static_cast<u64>(c.expireTick)) ++live;
            t.row({e.name.substr(0, 10), std::to_string(live), live > 0 ? "可随时开战" : "无"});
        }
        out(t.render());
    }
    return 0;
}

}  // namespace gf
