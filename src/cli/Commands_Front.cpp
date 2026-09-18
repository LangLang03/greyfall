// 前线系统 CLI
#include "cli/Commands.h"
#include "util/TextTable.h"
#include "combat/Front.h"
#include "core/Errors.h"
#include "domain/Treaty.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {

int cmdFront(CliEnv& env, const Args& args) {
    env.loadState();

    // --detail <emp>
    if (args.has("detail")) {
        i64 id = args.getInt("detail", -1);
        if (id < 0 || id >= static_cast<i64>(env.st.empires.size())) {
            fail(ExitCode::BadArgs, "帝国编号越界");
        }
        out(frontDetailText(env.st, kPlayerId, static_cast<u32>(id)));
        return 0;
    }

    out(style("═══ 战线总览 ═══", Style::Heading));
    out(frontsText(env.st, kPlayerId));
    out("");
    out("用法：greyfall front --detail <emp>   查看对某国的战线分析");
    return 0;
}

}  // namespace gf
