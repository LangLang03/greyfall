// 和平会议与领土割让 CLI
#include <algorithm>

#include "cli/Commands.h"
#include "util/TextTable.h"
#include "core/Errors.h"
#include "domain/Peace.h"
#include "domain/Treaty.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {

int cmdPeace(CliEnv& env, const Args& args) {
    env.loadState();
    const bool hasOp = args.has("convene") || args.has("annex") || args.has("reparations") ||
                       args.has("tech") || args.has("manpower") || args.has("remove") ||
                       args.has("conclude") || args.has("abandon");

    if (!hasOp) {
        out(style("═══ 和平会议 ═══", Style::Heading));
        out("  战争分数兑换要求。占优方提出要求 → 消耗分数 → 缔结条约并结束战争。");
        out("  一方首都被占领时召开「无条件」会议，战败方无权拒绝任何要求。");
        out("");
        out(peaceText(env.st, kPlayerId));
        out("");
        out("用法：greyfall peace --convene <敌国>           召开和平会议");
        out("      greyfall peace --annex <星系>             要求割让星系");
        out("      greyfall peace --reparations <金额>       要求战争赔款");
        out("      greyfall peace --tech <科技>              要求技术转移");
        out("      greyfall peace --manpower <千人>          要求人力征调");
        out("      greyfall peace --remove <编号>            撤销某项要求");
        out("      greyfall peace --conclude                 缔结条约并结束战争");
        out("      greyfall peace --abandon                  放弃会议（战争继续）");
        return 0;
    }

    if (args.has("convene")) {
        i64 tgt = args.getInt("convene", -1);
        if (tgt < 0 || tgt >= static_cast<i64>(env.st.empires.size())) {
            fail(ExitCode::BadArgs, "帝国编号越界");
        }
        std::string err;
        // 自动判定谁是占优方：战争分数高或占领对方首都者
        u32 other = static_cast<u32>(tgt);
        u32 winner = kPlayerId;
        const Empire* me = &env.player();
        const Empire* them = env.st.empire(other);
        bool myCapFallen = env.st.system(me->capital) != nullptr &&
                           env.st.system(me->capital)->owner == other;
        bool theirCapFallen = them != nullptr && env.st.system(them->capital) != nullptr &&
                              env.st.system(them->capital)->owner == kPlayerId;
        if (myCapFallen && !theirCapFallen) winner = other;
        else if (!theirCapFallen &&
                 env.st.relation(other, kPlayerId).warScore > env.st.relation(kPlayerId, other).warScore)
            winner = other;
        if (!convenePeace(env.st, winner, winner == kPlayerId ? other : kPlayerId, &err)) {
            fail(ExitCode::IllegalAction, err);
        }
        env.commit("peace");
        out(style("和平会议已召开", Style::Good));
        out(peaceText(env.st, kPlayerId));
        return 0;
    }

    if (args.has("annex")) {
        i64 sys = args.getInt("annex", -1);
        if (sys < 0) fail(ExitCode::BadArgs, "用法：--annex <星系编号>");
        std::string err;
        if (!addDemand(env.st, kPlayerId, makeAnnex(env.st, static_cast<u32>(sys)), &err)) {
            fail(ExitCode::IllegalAction, err);
        }
        env.commit("peace");
        out(style("已提出割让要求", Style::Good));
        out(peaceText(env.st, kPlayerId));
        return 0;
    }

    if (args.has("reparations")) {
        i64 amt = args.getInt("reparations", 0);
        if (amt <= 0) fail(ExitCode::BadArgs, "用法：--reparations <金额>");
        std::string err;
        if (!addDemand(env.st, kPlayerId, makeReparations(Fixed(amt)), &err)) {
            fail(ExitCode::IllegalAction, err);
        }
        env.commit("peace");
        out(style("已提出赔款要求", Style::Good));
        out(peaceText(env.st, kPlayerId));
        return 0;
    }

    if (args.has("tech")) {
        // 必须用科技表查科技。旧代码调用的是 resolutionIndexByName
        //（决议表），当某条决议名恰好等于科技名时会返回错误索引 ——
        // 这是一个确定性错误：`peace --tech <名称>` 可能索要一项
        // 完全不相干的科技，或误报「未知科技」。
        int idx = techIndexByName(args.get("tech"));
        // 也允许直接给科技编号
        i64 n = parseInt(args.get("tech"), -1);
        if (n >= 0 && n < kTechCount) idx = static_cast<int>(n);
        if (idx < 0) fail(ExitCode::BadArgs, "未知科技【" + args.get("tech") + "】");
        std::string err;
        if (!addDemand(env.st, kPlayerId, makeTech(static_cast<u32>(idx)), &err)) {
            fail(ExitCode::IllegalAction, err);
        }
        env.commit("peace");
        out(style("已提出技术转移要求", Style::Good));
        out(peaceText(env.st, kPlayerId));
        return 0;
    }

    if (args.has("manpower")) {
        i64 amt = args.getInt("manpower", 0);
        if (amt <= 0) fail(ExitCode::BadArgs, "用法：--manpower <千人>");
        std::string err;
        if (!addDemand(env.st, kPlayerId, makeManpower(Fixed(amt)), &err)) {
            fail(ExitCode::IllegalAction, err);
        }
        env.commit("peace");
        out(style("已提出人力征调要求", Style::Good));
        out(peaceText(env.st, kPlayerId));
        return 0;
    }

    if (args.has("remove")) {
        i64 idx = args.getInt("remove", -1);
        if (idx < 0) fail(ExitCode::BadArgs, "用法：--remove <编号>");
        std::string err;
        if (!removeDemand(env.st, kPlayerId, static_cast<std::size_t>(idx), &err)) {
            fail(ExitCode::IllegalAction, err);
        }
        env.commit("peace");
        out("已撤销该要求");
        out(peaceText(env.st, kPlayerId));
        return 0;
    }

    if (args.has("conclude")) {
        std::string err;
        if (!concludePeace(env.st, kPlayerId, &err)) fail(ExitCode::IllegalAction, err);
        env.commit("peace");
        out(style("和平条约已缔结，战争结束", Style::Good));
        return 0;
    }

    if (args.has("abandon")) {
        std::string err;
        if (!abandonPeace(env.st, kPlayerId, &err)) fail(ExitCode::IllegalAction, err);
        env.commit("peace");
        out(style("已放弃和平会议，战争继续", Style::Warn));
        return 0;
    }

    fail(ExitCode::BadArgs, "未知操作。用 `greyfall peace` 查看全部用法");
}

}  // namespace gf
