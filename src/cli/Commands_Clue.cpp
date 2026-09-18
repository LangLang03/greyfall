#include <algorithm>
#include <string>

#include "cli/Commands.h"
#include "util/TextTable.h"
#include "clue/ClueGraph.h"
#include "core/Errors.h"
#include "plot/BeatResolver.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {
namespace {

u16 requireClue(const GameState& st, std::string_view key) {
    int id = clueIndexByName(key);
    if (id < 0) fail(ExitCode::BadArgs, "未知线索【" + std::string(key) + "】（支持 clue137 / #137 / 137）");
    if (!st.clues[static_cast<std::size_t>(id)].known) {
        fail(ExitCode::IllegalAction, "线索【" + std::string(clueDef(id).nameZh) + "】尚未发现");
    }
    return static_cast<u16>(id);
}

}  // namespace

int cmdClues(CliEnv& env, const Args& args) {
    env.loadState();
    bool unlinked = args.has("unlinked");
    std::string tag = args.get("tag", "");
    std::string subject = args.get("subject", "");
    int known = 0;
    for (const auto& c : env.st.clues)
        if (c.known) ++known;
    out(style("═══ 线索超图 ═══", Style::Heading));
    out("  已发现 " + std::to_string(known) + " / " + std::to_string(kClueCount) + "    超边 " +
        std::to_string(env.st.clueEdges.size()) + " 条    未裁定矛盾 " +
        std::to_string([&] {
            int n = 0;
            for (const auto& e : env.st.clueEdges)
                if (e.kind == ClueEdgeKind::Contradict && !e.adjudicated) ++n;
            return n;
        }()) +
        " 条");
    out("");
    out(clueListText(env.st, unlinked, tag, subject));

    // 边概览
    if (!env.st.clueEdges.empty()) {
        out(style("最近的超边", Style::Sub));
        TextTable t;
        t.header({"A", "B", "类型", "权重", "裁定"});
        int shown = 0;
        for (auto it = env.st.clueEdges.rbegin(); it != env.st.clueEdges.rend() && shown < 10; ++it, ++shown) {
            const ClueEdge& e = *it;
            t.row({std::string(clueDef(e.a).idName), std::string(clueDef(e.b).idName),
                   std::string(clueEdgeKindName(e.kind)), fixedStrPlain(e.weight, 2),
                   e.adjudicated ? ("胜:" + std::string(clueDef(e.winner).idName)) : "未裁定"});
        }
        out(t.render());
    }
    return 0;
}

int cmdLink(CliEnv& env, const Args& args) {
    env.loadState();
    if (args.posCount() < 2) fail(ExitCode::BadArgs, "用法：greyfall link <A> <B> [--kind corroborate|...]");
    u16 a = requireClue(env.st, args.pos(0));
    u16 b = requireClue(env.st, args.pos(1));
    ClueEdgeKind kind = ClueEdgeKind::Corroborate;
    if (args.has("kind")) kind = clueEdgeKindFromName(args.get("kind"));
    std::string err;
    if (!clueLink(env.st, a, b, kind, &err)) fail(ExitCode::IllegalAction, err);
    env.commit("link");
    out("已连接 " + std::string(clueDef(a).idName) + " ↔ " + std::string(clueDef(b).idName) + "（" +
        std::string(clueEdgeKindName(kind)) + "）");
    out("  提示：矛盾边需要裁定；未裁定的矛盾会阻断结论提交。");
    return 0;
}

int cmdUnlink(CliEnv& env, const Args& args) {
    env.loadState();
    if (args.posCount() < 2) fail(ExitCode::BadArgs, "用法：greyfall unlink <A> <B>");
    u16 a = requireClue(env.st, args.pos(0));
    u16 b = requireClue(env.st, args.pos(1));
    std::string err;
    if (!clueUnlink(env.st, a, b, &err)) fail(ExitCode::IllegalAction, err);
    env.commit("unlink");
    out("已断开 " + std::string(clueDef(a).idName) + " ↔ " + std::string(clueDef(b).idName));
    return 0;
}

int cmdArchive(CliEnv& env, const Args& args) {
    env.loadState();
    if (args.posCount() < 1) fail(ExitCode::BadArgs, "用法：greyfall archive <clue>");
    u16 id = requireClue(env.st, args.pos(0));
    std::string err;
    if (!clueArchive(env.st, id, &err)) fail(ExitCode::IllegalAction, err);
    env.commit("archive");
    out("已归档线索【" + std::string(clueDef(id).nameZh) + "】");
    return 0;
}

int cmdDeduce(CliEnv& env, const Args& args) {
    env.loadState();
    if (args.has("commit")) {
        if (!args.has("conclusion")) fail(ExitCode::BadArgs, "提交必须指定 --conclusion <id>");
        int c = conclusionIndexByName(args.get("conclusion"));
        if (c < 0) fail(ExitCode::BadArgs, "未知结论【" + args.get("conclusion") + "】");
        if (env.player().apLeft < apcost::kDeduceCommit) {
            fail(ExitCode::IllegalAction, "行动点不足（提交需要 " + std::to_string(apcost::kDeduceCommit) + "）");
        }
        std::string err;
        if (!commitConclusion(env.st, static_cast<u16>(c), &err)) fail(ExitCode::IllegalAction, err);
        env.player().apLeft -= apcost::kDeduceCommit;
        // 提交后重新评估幕次
        plotPhase(env.st);
        env.commit("deduce --commit");
        const ConclusionDef& def = conclusionDef(c);
        out(style("已提交结论【" + std::string(def.nameZh) + "】", def.trueConclusion ? Style::Good : Style::Bad));
        out("  " + std::string(def.trueConclusion ? "真相公开：AI 重估了对你的先验，相关标的发生价格跳跃"
                                                  : "误判：假剧情分支开启，外交信誉损失，市场恐慌"));
        out("  AP 剩余 " + std::to_string(env.player().apLeft) + "/" + std::to_string(env.player().apMax));
        return 0;
    }

    if (args.has("conclusion") || args.has("explain")) {
        std::string key = args.get("conclusion", "");
        if (key.empty()) {
            // 列出当前幕最接近可提交的结论
            out(style("═══ 可推断的结论（第 " + std::to_string(static_cast<int>(env.st.plot.act)) + " 幕） ═══",
                      Style::Heading));
            TextTable t;
            t.header({"结论", "名称", "最小充分集", "可提交", "陷阱?"});
            for (int i = 0; i < kConclusionCount; ++i) {
                const ConclusionDef& d = conclusionDef(i);
                if (d.act != env.st.plot.act) continue;
                auto sets = minimalSatisfyingSets(env.st, static_cast<u16>(i));
                std::string why;
                bool ok = conclusionUnlockable(env.st, static_cast<u16>(i), &why);
                t.row({std::string(d.idName), std::string(d.nameZh), std::to_string(sets.size()),
                       ok ? "✓" : "—", d.trueConclusion ? "" : "⚠"});
            }
            out(t.render());
            out("");
            out("用 `greyfall deduce --conclusion C-42 --explain` 查看详细推断报告。");
            return 0;
        }
        int c = conclusionIndexByName(key);
        if (c < 0) fail(ExitCode::BadArgs, "未知结论【" + key + "】");
        out(deduceReport(env.st, static_cast<u16>(c)));
        return 0;
    }

    // 默认：全部结论概览
    out(style("═══ 结论总览 ═══", Style::Heading));
    TextTable t;
    t.header({"结论", "名称", "幕", "已解锁", "已提交", "真相"});
    for (int i = 0; i < kConclusionCount; ++i) {
        const ConclusionDef& d = conclusionDef(i);
        bool reached = std::find(env.st.plot.conclusionsReached.begin(), env.st.plot.conclusionsReached.end(),
                                 static_cast<u16>(i)) != env.st.plot.conclusionsReached.end();
        bool committed = std::find(env.st.plot.committedConclusions.begin(),
                                   env.st.plot.committedConclusions.end(),
                                   static_cast<u16>(i)) != env.st.plot.committedConclusions.end();
        t.row({std::string(d.idName), std::string(d.nameZh), std::to_string(static_cast<int>(d.act)),
               reached ? "✓" : "—", committed ? "✓" : "—", d.trueConclusion ? "" : "陷阱"});
    }
    out(t.render());
    return 0;
}

int cmdConclusions(CliEnv& env, const Args&) {
    env.loadState();
    const PlotState& p = env.st.plot;
    out(style("═══ 已提交的结论 ═══", Style::Heading));
    if (p.committedConclusions.empty()) {
        out("（尚未提交任何结论）");
    } else {
        for (u16 c : p.committedConclusions) {
            const ConclusionDef& d = conclusionDef(c);
            out("  " + padRight(std::string(d.idName), 8) + padRight(std::string(d.nameZh), 26) + "第 " +
                std::to_string(static_cast<int>(d.act)) + " 幕  " +
                (d.trueConclusion ? "" : style("【误判】", Style::Bad)));
        }
    }
    if (!p.falseConclusions.empty()) {
        out("");
        out(style("误判记录（真代价）", Style::Bad));
        for (u16 c : p.falseConclusions) {
            out("  · " + std::string(conclusionDef(c).nameZh));
        }
    }
    out("");
    out("结局向量（8 维）：");
    static const char* kDims[] = {"霸权", "联邦", "资本", "种族", "知识", "信仰", "毁灭", "超脱"};
    for (int i = 0; i < 8; ++i) {
        Fixed v = p.endingVector[static_cast<std::size_t>(i)];
        out("  " + padRight(kDims[i], 6) + " " + bar(v / Fixed(8), 24) + " " + fixedStrPlain(v, 2));
    }
    return 0;
}

}  // namespace gf
