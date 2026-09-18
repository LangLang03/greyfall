// 战斗与指挥官 CLI
#include <algorithm>

#include "cli/Commands.h"
#include "util/TextTable.h"
#include "combat/Resolver.h"
#include "core/Errors.h"
#include "domain/Commander.h"
#include "gen/NameGen.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {

int cmdBattles(CliEnv& env, const Args& args) {
    env.loadState();
    out(style("═══ 战斗态势 ═══", Style::Heading));
    u32 filter = args.has("empire") ? static_cast<u32>(args.getInt("empire", 0)) : 0xFFFFFFFFu;
    out(battlesText(env.st, filter));
    out("");
    out("说明：战斗以**组织度**判定胜负 —— 组织度归零的一方撤退。");
    out("      兵力（战力）只决定伤害承受；组织度先崩的一方输掉战斗。");
    out("      交战中的舰队组织度恢复很慢，撤退后才能重整。");
    return 0;
}

int cmdCommanders(CliEnv& env, const Args& args) {
    env.loadState();

    // 招募
    if (args.has("recruit")) {
        std::string traitName = args.get("trait", "");
        CommanderTrait tr = traitName.empty() ? CommanderTrait::None : commanderTraitFromName(traitName);
        if (tr == CommanderTrait::None) {
            // 随机特质
            u32 r = static_cast<u32>(env.st.rng.nextU64(RngStream::Empire) %
                                     (static_cast<std::size_t>(CommanderTrait::Count) - 1));
            tr = static_cast<CommanderTrait>(1 + r);
        }
        Fixed cost = Fixed(8000);
        if (env.player().treasury.rawValue() < cost.rawValue()) {
            fail(ExitCode::IllegalAction, "国库不足：招募需要 " + fixedStr(cost, 0));
        }
        if (env.player().apLeft < apcost::kRecruit) fail(ExitCode::IllegalAction, "行动点不足");
        env.player().treasury -= cost;
        env.st.market.margin.cash = env.player().treasury;
        env.player().apLeft -= apcost::kRecruit;
        NameGen names(env.st.seed ^ (static_cast<u64>(env.st.commanders.size()) * 7919ull) ^ env.st.tick);
        u32 id = recruitCommander(env.st, kPlayerId, names.ruler(), tr);
        env.commit("commanders --recruit");
        const Commander* c = env.st.commander(id);
        out(style("已招募指挥官【" + c->name + "】", Style::Good));
        out("  特质：" + std::string(commanderTraitName(c->trait)));
        out("  经验 " + std::to_string(c->experience.rawValue()) + "/1000\n");
        out("  攻 " + fixedStrPlain(c->attack, 2) + "  防 " + fixedStrPlain(c->defense, 2) + "  勤 " +
            fixedStrPlain(c->logistics, 2) + "  谋 " + fixedStrPlain(c->planning, 2));
        return 0;
    }

    // 军校深造
    if (args.has("train")) {
        i64 id = parseInt(args.get("train"), -1);
        if (id < 0) fail(ExitCode::BadArgs, "用法：--train <指挥官编号>");
        Fixed cost = Fixed(300);
        std::string err;
        if (!commanderTrain(env.st, static_cast<u32>(id), cost, &err)) {
            fail(ExitCode::IllegalAction, err);
        }
        env.commit("commanders --train");
        const Commander* c = env.st.commander(static_cast<u32>(id));
        out(style("已完成军校深造（花费影响力 " + fixedStr(cost, 0) + "）", Style::Good));
        if (c != nullptr) {
            out("  " + commanderRankText(*c));
            out("  攻 " + fixedStrPlain(c->attack, 2) + "  防 " + fixedStrPlain(c->defense, 2) + "  勤 " +
                fixedStrPlain(c->logistics, 2) + "  谋 " + fixedStrPlain(c->planning, 2));
        }
        return 0;
    }

    // 分配
    if (args.has("assign")) {
        std::string spec = args.get("assign", "");
        std::size_t colon = spec.find(':');
        if (colon == std::string::npos) {
            fail(ExitCode::BadArgs, "用法：--assign <指挥官编号>:<舰队编号>");
        }
        u32 cid = static_cast<u32>(parseInt(spec.substr(0, colon), 0));
        u32 fid = static_cast<u32>(parseInt(spec.substr(colon + 1), 0));
        std::string err;
        if (!assignCommander(env.st, cid, fid, &err)) fail(ExitCode::IllegalAction, err);
        env.commit("commanders --assign");
        out("已把指挥官 #" + std::to_string(cid) + " 分配给舰队 #" + std::to_string(fid));
        return 0;
    }

    out(style("═══ 指挥官 ═══", Style::Heading));
    out("  #  姓名        军衔   特质             攻击  防御  后勤  谋划   战绩      战功  隶属");
    out("  ──────────────────────────────────────────────────────────────────────────────────");
    int shown = 0;
    for (const auto& c : env.st.commanders) {
        if (c.owner != kPlayerId) continue;
        const Fleet* f = (c.fleet != 0xFFFFFFFFu) ? env.st.fleet(c.fleet) : nullptr;
        std::string traits = std::string(commanderTraitName(c.trait));
        for (CommanderTrait t : c.traits) traits += "/" + std::string(commanderTraitName(t));
        out("  " + padRight(std::to_string(c.id), 3) + padRight(c.name, 12) +
            padRight(std::string(commanderRankName(c.rank)), 7) + padRight(traits, 16) +
            padLeft(fixedStrPlain(c.attack, 2), 5) + padLeft(fixedStrPlain(c.defense, 2), 6) +
            padLeft(fixedStrPlain(c.logistics, 2), 6) + padLeft(fixedStrPlain(c.planning, 2), 6) + "   " +
            padRight(std::to_string(c.battlesWon) + "胜" + std::to_string(c.battlesLost) + "负", 10) +
            padLeft(fixedStr(c.merit, 0), 6) + "  " + (f != nullptr ? f->name : "（未分配）"));
        out("    " + style(commanderRankText(c), Style::Dim));
        out("");
        ++shown;
    }
    if (shown == 0) {
        out("  （没有指挥官）");
    }
    out("");
    out(style("特质效果", Style::Sub));
    TextTable t;
    t.header({"特质", "效果"});
    t.row({"攻势", "进攻 +15%，防御 -5%"});
    t.row({"防御", "防御 +18%，进攻 -5%"});
    t.row({"后勤", "补给消耗 -25%，防御 +4%"});
    t.row({"机动", "进攻 +8%，展开宽度 +8%"});
    t.row({"诡道", "进攻 +6%，压缩对方展开宽度 15%"});
    t.row({"鼓舞", "组织度恢复加快，攻防 +5~6%"});
    t.row({"攻坚", "进攻 +10%，对要塞/巨构额外有效"});
    out(t.render());
    out("");
    out("用法：greyfall commanders --recruit [--trait 攻势]");
    out("      greyfall commanders --assign <指挥官编号>:<舰队编号>");
    out("      greyfall commanders --train <指挥官编号>     军校深造（花费影响力）");
    out("");
    out(style("军衔与晋升", Style::Sub));
    out("  指挥官通过**战斗**积累战功，达到阈值自动晋升。晋升提升技能上限、");
    out("  授予新特质，并提高统率加成（影响战斗展开宽度）。");
    {
        TextTable rt;
        rt.header({"军衔", "所需战功", "技能上限", "统率加成", "特质数"});
        for (int i = 0; i < kCommanderRankCount; ++i) {
            auto r = static_cast<CommanderRank>(i);
            rt.row({std::string(commanderRankName(r)), fixedStr(rankMeritRequired(r), 0),
                    fixedStrPlain(rankSkillCap(r) * Fixed(100), 0) + "%",
                    "+" + fixedStrPlain(rankCommandBonus(r) * Fixed(100), 0) + "%",
                    std::to_string(rankTraitSlots(r))});
        }
        out(rt.render());
    }
    return 0;
}

}  // namespace gf
