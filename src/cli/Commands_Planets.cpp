// 行星星表：给出 build / colony 所需的行星编号
#include <algorithm>

#include "cli/Commands.h"
#include "cli/TextTable.h"
#include "core/Errors.h"
#include "domain/Planet.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {

int cmdPlanets(CliEnv& env, const Args& args) {
    env.loadState();
    const u32 who = args.has("empire") ? static_cast<u32>(args.getInt("empire", 0)) : kPlayerId;
    const Empire* e = env.st.empire(who);
    if (e == nullptr) fail(ExitCode::BadArgs, "帝国编号越界");

    out(style("═══ 行星 · " + e->name + " ═══", Style::Heading));

    TextTable t;
    t.header({"#", "行星", "所属星系", "类型", "规模", "人口", "开发", "民怨", "稳定", "建筑"},
             {Align::Right, Align::Left, Align::Left, Align::Left, Align::Right, Align::Right,
              Align::Right, Align::Right, Align::Right, Align::Right});

    int shown = 0;
    i64 totalPops = 0;
    for (const auto& p : env.st.planets) {
        if (p.owner != who) continue;
        const SystemNode* s = env.st.system(p.system);
        t.row({std::to_string(p.id), p.name, s ? s->name : "?", std::string(planetTypeName(p.type)),
               std::to_string(p.size), std::to_string(p.pops) + "k", fixedStrPlain(p.development, 1),
               fixedStrPlain(p.unrest * Fixed(100), 0) + "%",
               fixedStrPlain(p.stability * Fixed(100), 0) + "%",
               std::to_string(p.buildings.size())});
        totalPops += p.pops;
        ++shown;
    }
    if (shown == 0) {
        out("  （没有殖民行星）");
        return 0;
    }
    out(t.render());
    out("  合计 " + std::to_string(shown) + " 颗行星，总人口 " + std::to_string(totalPops) + "k");

    // 单颗详情
    if (args.has("id")) {
        i64 id = args.getInt("id", -1);
        const Planet* p = env.st.planet(static_cast<u32>(id));
        if (p == nullptr) fail(ExitCode::BadArgs, "行星编号越界");
        const SystemNode* s = env.st.system(p->system);
        out("");
        out(style("行星详情", Style::Sub));
        TextTable d;
        d.header({"项目", "值"});
        d.row({"行星", p->name + " (#" + std::to_string(p->id) + ")"});
        d.row({"星系", (s ? s->name : "?") + " (#" + std::to_string(p->system) + ")"});
        d.row({"归属", p->owner == kNoEmpire
                            ? std::string("无主")
                            : (env.st.empire(p->owner) ? env.st.empire(p->owner)->name : std::string("?"))});
        d.row({"类型", std::string(planetTypeName(p->type))});
        d.row({"规模", std::to_string(p->size)});
        d.row({"人口", std::to_string(p->pops) + "k"});
        if (p->settlementTicksLeft > 0)
            d.row({"殖民发展", "剩余 " + std::to_string(p->settlementTicksLeft) + " 季，自然产量逐步恢复至完整水平"});
        d.row({"宜居度", fixedStrPlain(p->habitability * Fixed(100), 0) + "%"});
        d.row({"开发度", fixedStrPlain(p->development, 2) + " / 10"});
        d.row({"民怨", fixedStrPlain(p->unrest * Fixed(100), 1) + "%"});
        d.row({"稳定度", fixedStrPlain(p->stability * Fixed(100), 1) + "%"});
        {
            std::string bs;
            for (u32 b : p->buildings) {
                const BuildingInfo& bi = buildingInfo(static_cast<int>(b & 0xFFu));
                if (!bs.empty()) bs += "、";
                bs += bi.nameZh;
            }
            d.row({"建筑", bs.empty() ? "（无）" : bs});
        }
        // 产出
        std::string ys;
        for (int c = 0; c < kCommodityCount; ++c) {
            Fixed y = p->yield[static_cast<std::size_t>(c)];
            if (y.rawValue() <= 0) continue;
            if (!ys.empty()) ys += "、";
            ys += std::string(commodityName(c)) + " " + fixedStr(y, 0);
        }
        d.row({"基础产出", ys.empty() ? "（无）" : ys});
        out(d.render());
    }

    out("");
    out("用法：greyfall planets [--empire N] [--id <行星>]");
    out("      greyfall build <建筑> <行星>     在指定行星建造（编号取自上表）");
    return 0;
}

}  // namespace gf
