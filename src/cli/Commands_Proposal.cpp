// 提案箱：回应 AI 主动提出的交易与协定
#include "cli/Commands.h"
#include "cli/TextTable.h"
#include "core/Errors.h"
#include "domain/Proposal.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {

int cmdProposal(CliEnv& env, const Args& args) {
    env.loadState();

    if (args.has("accept") || args.has("reject")) {
        bool accepting = args.has("accept");
        i64 id = args.getInt(accepting ? "accept" : "reject", -1);
        if (id < 0) fail(ExitCode::BadArgs, "用法：greyfall proposal --accept <编号> | --reject <编号>");
        std::string msg;
        bool ok = accepting ? proposalAccept(env.st, static_cast<u32>(id), &msg)
                            : proposalReject(env.st, static_cast<u32>(id), &msg);
        if (!ok) fail(ExitCode::IllegalAction, msg);
        env.commit("proposal");
        out(style(msg, accepting ? Style::Good : Style::Warn));
        return 0;
    }

    if (args.has("detail")) {
        i64 id = args.getInt("detail", -1);
        if (id < 0) fail(ExitCode::BadArgs, "用法：greyfall proposal --detail <编号>");
        out(style("═══ 提案详情 ═══", Style::Heading));
        out(proposalText(env.st, static_cast<u32>(id)));
        return 0;
    }

    out(style("═══ 待回应的提议 ═══", Style::Heading));
    out(proposalListText(env.st));
    if (!env.st.proposals.items.empty()) {
        out("  用 `greyfall proposal --detail <编号>` 查看细节");
        out("  用 `greyfall proposal --accept <编号>` / `--reject <编号>` 回应");
        out("  拒绝会小幅恶化关系；若提案本身很不合理，可考虑先谴责：");
        out("    greyfall envoy <帝国> condemn");
    }
    return 0;
}

}  // namespace gf
