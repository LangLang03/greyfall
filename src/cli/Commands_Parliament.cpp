// 议会立法 CLI
#include <algorithm>

#include "cli/Commands.h"
#include "cli/TextTable.h"
#include "core/Errors.h"
#include "domain/Parliament.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {

int cmdParliament(CliEnv& env, const Args& args) {
    env.loadState();

    // 操作类选项优先判断；只有「没有任何操作」时才回退到列表视图。
    // 否则 `--propose X` 会因为 posCount()==0 而误入列表分支。
    const bool hasOp = args.has("propose") || args.has("detail") || args.has("persuade") ||
                       args.has("vote") || args.has("force") || args.has("withdraw");
    if (!hasOp) {
        out(style("═══ 议会 ═══", Style::Heading));
        out(parliamentText(env.st, kPlayerId));
        out("");
        out(style("可提交的法案", Style::Sub));
        TextTable t;
        t.header({"法案", "类别", "效果", "天然支持", "天然反对", "状态"},
                 {Align::Left, Align::Left, Align::Left, Align::Left, Align::Left, Align::Left});
        const Parliament& p = env.player().parliament;
        for (int i = 0; i < kBillCount; ++i) {
            const BillDef& b = billDef(i);
            std::string sup, opp;
            for (int k = 0; k < static_cast<int>(FactionKind::Count); ++k) {
                i8 base = b.baseStance[static_cast<std::size_t>(k)];
                if (base > 0) {
                    if (!sup.empty()) sup += ",";
                    sup += factionKindName(static_cast<FactionKind>(k));
                } else if (base < 0) {
                    if (!opp.empty()) opp += ",";
                    opp += factionKindName(static_cast<FactionKind>(k));
                }
            }
            bool done = std::find(p.passed.begin(), p.passed.end(), static_cast<u16>(i)) != p.passed.end();
            bool rej = std::find(p.rejected.begin(), p.rejected.end(), static_cast<u16>(i)) != p.rejected.end();
            std::string st = done ? style("已通过", Style::Good) : (rej ? style("曾否决", Style::Warn) : "");
            t.row({std::string(b.nameZh) + (b.radical ? "⚡" : ""), std::string(billCategoryName(b.category)),
                   billEffectText(b), sup, opp, st});
        }
        out(t.render());
        out("");
        out("用法：greyfall parliament --propose <法案>    提交表决");
        out("      greyfall parliament --detail <法案>     查看详情");
        out("      greyfall parliament --persuade <派系>   拉票");
        out("      greyfall parliament --vote              表决");
        out("      greyfall parliament --force             强行通过（代价高昂）");
        out("      greyfall parliament --withdraw          撤回");
        out("");
        out("  ⚡ 标记为激进改革：保守派强烈反对，但不满者更愿支持。");
        return 0;
    }

    // --detail
    if (args.has("detail")) {
        int idx = billIndexByName(args.get("detail"));
        if (idx < 0) fail(ExitCode::BadArgs, "未知法案【" + args.get("detail") + "】");
        out(billDetailText(env.st, kPlayerId, static_cast<u16>(idx)));
        return 0;
    }

    // --propose
    if (args.has("propose")) {
        int idx = billIndexByName(args.get("propose"));
        if (idx < 0) fail(ExitCode::BadArgs, "未知法案【" + args.get("propose") + "】");
        std::string err;
        if (!billPropose(env.st, kPlayerId, static_cast<u16>(idx), &err)) {
            fail(ExitCode::IllegalAction, err);
        }
        env.commit("parliament");
        const BillDef& b = billDef(idx);
        out(style("已提交【" + std::string(b.nameZh) + "】至议会", Style::Good));
        out("  门槛：" + std::string(voteThresholdName(env.player().parliament.session.thresholdKind)));
        out("");
        out(parliamentText(env.st, kPlayerId));
        return 0;
    }

    // --persuade
    if (args.has("persuade")) {
        std::string key = args.get("persuade");
        FactionKind fk = FactionKind::Count;
        for (int i = 0; i < static_cast<int>(FactionKind::Count); ++i) {
            if (factionKindName(static_cast<FactionKind>(i)) == key) fk = static_cast<FactionKind>(i);
        }
        if (fk == FactionKind::Count) fail(ExitCode::BadArgs, "未知派系【" + key + "】");
        std::string err;
        if (!billPersuade(env.st, kPlayerId, fk, &err)) fail(ExitCode::IllegalAction, err);
        env.commit("parliament");
        out(style("已拉拢【" + key + "】", Style::Good));
        out("");
        out(parliamentText(env.st, kPlayerId));
        return 0;
    }

    // --vote
    if (args.has("vote")) {
        std::string err;
        bool passed = billVote(env.st, kPlayerId, &err);
        if (!err.empty()) fail(ExitCode::IllegalAction, err);
        env.commit("parliament");
        const Parliament& p = env.player().parliament;
        out(passed ? style("表决通过", Style::Good) : style("表决否决", Style::Bad));
        out("  " + p.session.lastResult);
        if (passed) {
            const BillDef& b = billDef(p.session.billId);
            out("  效果：" + billEffectText(b));
        }
        return 0;
    }

    // --force
    if (args.has("force")) {
        std::string err;
        if (!billForcePass(env.st, kPlayerId, &err)) fail(ExitCode::IllegalAction, err);
        env.commit("parliament");
        out(style("已强行通过法案", Style::Warn));
        out("  " + env.player().parliament.session.lastResult);
        return 0;
    }

    // --withdraw
    if (args.has("withdraw")) {
        std::string err;
        if (!billWithdraw(env.st, kPlayerId, &err)) fail(ExitCode::IllegalAction, err);
        env.commit("parliament");
        out("已撤回法案");
        return 0;
    }

    fail(ExitCode::BadArgs, "未知操作。用 greyfall parliament 查看全部用法");
}

}  // namespace gf
