// 舰船设计器：新建舰体、装卸模块、改造现役舰队
#include "cli/Commands.h"
#include "cli/TextTable.h"
#include "core/Errors.h"
#include "domain/Empire.h"
#include "domain/Fleet.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {

int cmdDesign(CliEnv& env, const Args& args) {
    env.loadState();
    Empire& me = env.player();
    std::string msg;

    if (args.has("new")) {
        std::string hullName = args.get("new");
        int h = -1;
        for (int i = 0; i < static_cast<int>(HullClass::Count); ++i)
            if (iequals(hullClassName(static_cast<HullClass>(i)), hullName) ||
                iequals(hullInfo(static_cast<HullClass>(i)).idName, hullName))
                h = i;
        if (h < 0) {
            fail(ExitCode::BadArgs,
                 "未知舰体【" + hullName + "】。可选：corvette/frigate/destroyer/cruiser/battleship/titan");
        }
        u32 id = designCreate(me, static_cast<HullClass>(h), args.get("name", ""), &msg);
        if (id == 0xFFFFFFFFu) fail(ExitCode::IllegalAction, msg);
        env.commit("design");
        out(style(msg, Style::Good));
        out("  用 `greyfall design --install <设计> <模块>` 装配模块。");
        return 0;
    }

    auto findDesign = [&](const std::string& key, u32* outId) {
        i64 num = parseInt(key, -1);
        if (num >= 0) {
            for (const auto& d : me.designs)
                if (d.id == static_cast<u32>(num)) {
                    *outId = d.id;
                    return true;
                }
        }
        for (const auto& d : me.designs)
            if (d.name == key) {
                *outId = d.id;
                return true;
            }
        return false;
    };

    if (args.has("install")) {
        u32 did = 0;
        if (!findDesign(args.get("install"), &did)) fail(ExitCode::BadArgs, "找不到该设计");
        i64 mod = args.getInt("module", -1);
        if (mod < 0) fail(ExitCode::BadArgs, "用法：--install <设计> --module <模块>");
        if (!designInstallModule(me, did, static_cast<int>(mod), &msg))
            fail(ExitCode::IllegalAction, msg);
        env.commit("design");
        out(style(msg, Style::Good));
        return 0;
    }
    if (args.has("remove")) {
        u32 did = 0;
        if (!findDesign(args.get("remove"), &did)) fail(ExitCode::BadArgs, "找不到该设计");
        i64 slot = args.getInt("at", -1);
        if (slot < 0) fail(ExitCode::BadArgs, "用法：--remove <设计> --at <槽位序号>");
        if (!designRemoveModule(me, did, static_cast<int>(slot), &msg))
            fail(ExitCode::IllegalAction, msg);
        env.commit("design");
        out(style(msg, Style::Warn));
        return 0;
    }
    if (args.has("clear")) {
        u32 did = 0;
        if (!findDesign(args.get("clear"), &did)) fail(ExitCode::BadArgs, "找不到该设计");
        if (!designClearModules(me, did, &msg)) fail(ExitCode::IllegalAction, msg);
        env.commit("design");
        out(style(msg, Style::Warn));
        return 0;
    }
    if (args.has("refit")) {
        i64 fleetId = args.getInt("refit", -1);
        std::string key = args.get("to", "");
        u32 did = 0;
        if (fleetId < 0 || !findDesign(key, &did))
            fail(ExitCode::BadArgs, "用法：--refit <舰队> --to <设计>");
        if (!fleetRefit(env.st, kPlayerId, static_cast<u32>(fleetId), did, &msg))
            fail(ExitCode::IllegalAction, msg);
        env.commit("design");
        out(style(msg, Style::Good));
        return 0;
    }

    // 总览
    out(style("═══ 舰体类型 ═══", Style::Heading));
    {
        TextTable t;
        t.header({"舰体", "定位", "造价", "火力", "防御", "速度", "槽位", "维护"});
        for (int i = 0; i < static_cast<int>(HullClass::Count); ++i) {
            const HullInfo& hi = hullInfo(static_cast<HullClass>(i));
            t.row({std::string(hi.nameZh) + "(" + std::string(hi.idName) + ")",
                   std::string(hullRoleName(hi.role)), std::to_string(hi.baseCost),
                   fixedStr(hi.baseFirepower, 0), fixedStr(hi.baseDefense, 0),
                   fixedStr(hi.baseSpeed, 0), std::to_string(hi.slots),
                   fixedStr(hi.maintenance, 0)});
        }
        out(t.render());
        out("  定位说明：");
        for (int i = 0; i < static_cast<int>(HullRole::Count); ++i) {
            HullRole r = static_cast<HullRole>(i);
            out("    " + std::string(hullRoleName(r)) + " — " + hullRoleDesc(r));
        }
    }
    out("");
    out(style("═══ 你的设计 ═══", Style::Heading));
    if (me.designs.empty()) {
        out("  （尚无设计。用 `greyfall design --new corvette --name 侦察型` 新建）");
    } else {
        TextTable t;
        t.header({"#", "名称", "舰体", "定位", "火力", "防御", "槽位", "自定义"});
        for (const auto& d : me.designs) {
            const HullInfo& hi = hullInfo(d.hull);
            t.row({std::to_string(d.id), d.name, std::string(hi.nameZh),
                   std::string(hullRoleName(hi.role)), fixedStr(d.firepower, 0),
                   fixedStr(d.defense, 0),
                   std::to_string(d.modules.size()) + "/" + std::to_string(hi.slots),
                   d.custom ? "是" : "-"});
        }
        out(t.render());
    }
    out("");
    out("用法：greyfall design --new <舰体> [--name 名称]      新建");
    out("      greyfall design --install <设计> --module <模块> 安装模块");
    out("      greyfall design --remove <设计> --at <序号>      卸下模块");
    out("      greyfall design --clear <设计>                  清空模块");
    out("      greyfall design --refit <舰队> --to <设计>       改造现役舰队");
    out("      greyfall equip <模块> <舰队>                    旧接口（等价于改设计）");
    return 0;
}

}  // namespace gf
