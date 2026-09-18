// 种族、奴役、太空生物、外交施压、基因改造
#include "cli/Commands.h"
#include "util/TextTable.h"
#include "core/Errors.h"
#include "domain/SpeciesAdv.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {

int cmdSpecies(CliEnv& env, const Args& args) {
    env.loadState();
    std::string msg;

    if (args.has("hunt")) {
        i64 sys = args.getInt("hunt", -1);
        if (sys < 0) fail(ExitCode::BadArgs, "用法：--hunt <星系>");
        if (!huntFauna(env.st, kPlayerId, static_cast<u32>(sys), &msg))
            fail(ExitCode::IllegalAction, msg);
        env.commit("species");
        out(style(msg, Style::Good));
        return 0;
    }
    if (args.has("can")) {
        i64 batches = args.getInt("can", -1);
        if (batches <= 0) fail(ExitCode::BadArgs, "用法：--can <批数>");
        if (!canStarJelly(env.st, kPlayerId, batches, &msg)) fail(ExitCode::IllegalAction, msg);
        env.commit("species");
        out(style(msg, Style::Good));
        return 0;
    }
    if (args.has("labor")) {
        std::string want = args.get("labor", "");
        int pick = -1;
        for (int i = 0; i < static_cast<int>(LaborPolicy::Count); ++i)
            if (iequals(std::string(laborPolicyName(static_cast<LaborPolicy>(i))), want))
                pick = i;
        if (pick < 0) fail(ExitCode::BadArgs, "未知劳役制度（自由民 / 种姓制 / 蓄奴制）");
        if (!setLaborPolicy(env.st, kPlayerId, static_cast<LaborPolicy>(pick), &msg))
            fail(ExitCode::IllegalAction, msg);
        env.commit("species");
        out(style(msg, Style::Warn));
        return 0;
    }
    if (args.has("gene")) {
        std::string want = args.get("gene", "");
        int pick = -1;
        for (int i = 0; i < static_cast<int>(GeneMod::Count); ++i)
            if (iequals(std::string(geneModName(static_cast<GeneMod>(i))), want)) pick = i;
        if (pick < 0) fail(ExitCode::BadArgs, "未知改造方向（强健 / 勤勉 / 睿智 / 坚韧 / 温顺）");
        if (!applyGeneMod(env.st, kPlayerId, static_cast<GeneMod>(pick), &msg))
            fail(ExitCode::IllegalAction, msg);
        env.commit("species");
        out(style(msg, Style::Good));
        return 0;
    }
    if (args.has("migrate")) {
        i64 sys = args.getInt("migrate", -1);
        if (sys < 0) fail(ExitCode::BadArgs, "用法：--migrate <星系>");
        if (!nomadicMigrate(env.st, kPlayerId, static_cast<u32>(sys), &msg))
            fail(ExitCode::IllegalAction, msg);
        env.commit("species");
        out(style(msg, Style::Good));
        return 0;
    }
    if (args.has("press")) {
        i64 tgt = args.getInt("press", -1);
        std::string kindName = args.get("kind", "经济施压");
        int kind = 0;
        for (int i = 0; i < static_cast<int>(PressureKind::Count); ++i)
            if (iequals(std::string(pressureKindName(static_cast<PressureKind>(i))), kindName))
                kind = i;
        if (tgt < 0) fail(ExitCode::BadArgs, "用法：--press <帝国> [--kind 经济施压|军事施压|外交孤立]");
        if (!applyPressure(env.st, kPlayerId, static_cast<u32>(tgt), static_cast<PressureKind>(kind),
                           &msg))
            fail(ExitCode::IllegalAction, msg);
        env.commit("species");
        out(style(msg, Style::Warn));
        return 0;
    }

    out(style("═══ 种族与劳役 ═══", Style::Heading));
    out(speciesReport(env.st, kPlayerId));
    out("");
    out(style("劳役制度", Style::Sub));
    {
        TextTable t;
        t.header({"制度", "产出", "民怨增量", "说明"});
        for (int i = 0; i < static_cast<int>(LaborPolicy::Count); ++i) {
            LaborPolicy p = static_cast<LaborPolicy>(i);
            t.row({std::string(laborPolicyName(p)),
                   "×" + fixedStrPlain(laborOutputMultiplier(p), 2),
                   "+" + fixedStrPlain(laborUnrestTarget(p) * Fixed(100), 0) + "%",
                   laborPolicyDesc(p)});
        }
        out(t.render());
    }
    out("");
    out(style("基因改造", Style::Sub));
    {
        TextTable t;
        t.header({"方向", "凝聚力", "效果", "状态"});
        for (int i = 0; i < static_cast<int>(GeneMod::Count); ++i) {
            GeneMod m = static_cast<GeneMod>(i);
            bool done = env.player().geneMods[static_cast<std::size_t>(i)];
            t.row({std::string(geneModName(m)), std::to_string(geneModCost(m)), geneModDesc(m),
                   done ? "已完成" : "可改造"});
        }
        out(t.render());
        out("  已有改造每 12 季产生一笔基因库维护费（每项 1,200 cr）。");
    }
    out("");
    out(style("游牧态势", Style::Heading));
    out(nomadicReport(env.st, kPlayerId));
    out("");
    out(style("太空生物", Style::Heading));
    out(faunaReport(env.st, kPlayerId));
    out("");
    out("用法：greyfall species --hunt <星系>            猎杀太空生物");
    out("      greyfall species --can <批数>             把食物加工成海星罐头");
    out("      greyfall species --labor <制度>           切换劳役制度");
    out("      greyfall species --gene <方向>            基因改造");
    out("      greyfall species --press <帝国> [--kind]  外交施压");
    out("      greyfall species --migrate <星系>         迁徙首都（仅游牧）");
    return 0;
}

}  // namespace gf
