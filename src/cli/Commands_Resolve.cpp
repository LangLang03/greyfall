// 决议系统与胜利条件的 CLI
#include <algorithm>

#include "cli/Commands.h"
#include "cli/TextTable.h"
#include "core/Errors.h"
#include "core/ResolutionEngine.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {

int cmdResolve(CliEnv& env, const Args& args) {
    env.loadState();

    // resolve --detail <id>
    if (args.has("detail") || (args.posCount() >= 1 && args.pos(0) == "info")) {
        std::string key = args.has("detail") ? args.get("detail") : args.pos(1);
        int idx = resolutionIndexByName(key);
        if (idx < 0) fail(ExitCode::BadArgs, "未知决议【" + key + "】");
        out(resolutionDetailText(env.st, kPlayerId, static_cast<u16>(idx)));
        return 0;
    }

    // resolve --activate <id>
    if (args.has("activate") || (args.posCount() >= 2 && args.pos(0) == "activate")) {
        std::string key = args.has("activate") ? args.get("activate") : args.pos(1);
        int idx = resolutionIndexByName(key);
        if (idx < 0) fail(ExitCode::BadArgs, "未知决议【" + key + "】");
        std::string err;
        if (!resolutionActivate(env.st, kPlayerId, static_cast<u16>(idx), &err)) {
            fail(ExitCode::IllegalAction, err);
        }
        env.commit("resolve");
        const ResolutionDef& d = resolutionDef(idx);
        out(style("已发起决议【" + std::string(d.nameZh) + "】", Style::Good));
        out("  效果：" + resEffectText(d.onActivate));
        if (d.duration > 0)
            out("  持续：" + resEffectText(d.onTick) + "，共 " + std::to_string(d.duration) + " 季");
        if (d.kind == ResolutionKind::Tradeoff && d.cost.sacrificeTarget != ResTarget::Count) {
            out(style("  永久牺牲：" + std::string(resTargetName(d.cost.sacrificeTarget)) + " " +
                          fixedStrSigned(d.cost.sacrificeValue * Fixed(100), 1) + "%",
                      Style::Bad));
        }
        out("  剩余 AP " + std::to_string(env.player().apLeft) + "/" + std::to_string(env.player().apMax));
        return 0;
    }

    // 默认：列出全部决议
    out(style("═══ 决议总览 ═══", Style::Heading));
    out("  共 " + std::to_string(kResolutionCount) + " 项。五类：主动决议 / 自动触发 / 可阻止 / 倒计时 / 牺牲换利");
    if (args.has("available")) out("  （仅显示当前可主动发起的项）");
    out(resolutionListText(env.st, kPlayerId, args.has("available")));

    // 生效中的效果
    const Empire& p = env.player();
    if (!p.resolutions.active.empty()) {
        out("");
        out(style("生效中的效果", Style::Sub));
        TextTable t;
        t.header({"来源", "效果", "剩余"});
        for (const auto& a : p.resolutions.active) {
            ResEffect ef;
            ef.target = a.target;
            ef.value = a.value;
            std::string left = a.ticksLeft < 0 ? "永久" : (std::to_string(a.ticksLeft) + " 季");
            t.row({a.source, resEffectText(ef), left});
        }
        out(t.render());
    }
    if (!p.resolutions.countdowns.empty()) {
        out("");
        out(style("进行中的倒计时", Style::Warn));
        TextTable t;
        t.header({"决议", "剩余", "目标"});
        for (const auto& c : p.resolutions.countdowns) {
            const ResolutionDef& d = resolutionDef(c.defId);
            bool isMod = resTargetToMod(c.target) != ModKind::Count;
            std::string goalText =
                isMod ? (fixedStrPlain(c.goal * Fixed(100), 0) + "%") : fixedStr(c.goal, 0);
            t.row({std::string(d.nameZh), std::to_string(c.ticksLeft) + " 季",
                   std::string(resTargetName(c.target)) + " ≥ " + goalText});
        }
        out(t.render());
    }
    out("");
    out("用法：greyfall resolve --detail <id>     查看详情");
    out("      greyfall resolve --activate <id>   发起主动决议 / 牺牲换利");
    out("      greyfall resolve --available       只列出当前可发起的");
    return 0;
}

int cmdVictory(CliEnv& env, const Args&) {
    env.loadState();
    out(victoryReport(env.st));
    out("");
    out("说明：其余帝国须全部覆灭或失去全部领土，盟友与附庸也计入对手。");
    out("      剧情结局不等于征服胜利；征服后的治理考验按实际季末结算累计。");
    return 0;
}

}  // namespace gf
