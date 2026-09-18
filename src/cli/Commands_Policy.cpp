// 政策系统 CLI
#include <algorithm>

#include "cli/Commands.h"
#include "util/TextTable.h"
#include "core/Errors.h"
#include "domain/Policy.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {

int cmdPolicy(CliEnv& env, const Args& args) {
    env.loadState();

    // --detail <政策名>
    if (args.has("detail")) {
        const PolicyOption* p = policyFind(args.get("detail"));
        if (p == nullptr) fail(ExitCode::BadArgs, "未知政策【" + args.get("detail") + "】");
        out(policyDetailText(env.st, kPlayerId, *p));
        return 0;
    }

    // --enact <政策名>
    if (args.has("enact") || (args.posCount() >= 2 && args.pos(0) == "enact")) {
        std::string key = args.has("enact") ? args.get("enact") : args.pos(1);
        const PolicyOption* p = policyFind(key);
        if (p == nullptr) fail(ExitCode::BadArgs, "未知政策【" + key + "】");
        std::string err;
        if (!policyEnact(env.st, kPlayerId, *p, &err)) fail(ExitCode::IllegalAction, err);
        env.commit("policy");
        out(style("已开始推行【" + std::string(p->nameZh) + "】", Style::Good));
        out("  所属组：" + std::string(policyGroupName(p->group)));
        out("  效果：" + policyEffectText(*p));
        out("  过渡期：" + std::to_string(p->transition) + " 季（期间效果线性切换）");
        out("  每季维护：" + std::to_string(p->upkeep) + " cr");
        out("  影响力 → " + fixedStr(env.player().influence, 0));
        if (p->favored != FactionKind::Count)
            out("  " + std::string(factionKindName(p->favored)) + " 满意度 +" +
                fixedStrPlain(p->factionDelta * Fixed(100), 0) + "%");
        if (p->harmed != FactionKind::Count)
            out("  " + std::string(factionKindName(p->harmed)) + " 满意度 -" +
                fixedStrPlain(p->factionDelta * Fixed(100), 0) + "%");
        return 0;
    }

    // 默认：列出全部
    out(style("═══ 政策 ═══", Style::Heading));
    out("  与 `edict`（一次性法令）不同：政策是**持久化**的，按季收维护费，");
    out("  同组内互斥，切换需要影响力并经历过渡期。");
    out(policyText(env.st, kPlayerId));
    return 0;
}

}  // namespace gf
