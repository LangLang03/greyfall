#include <algorithm>
#include <string>

#include "ai/FactionAI.h"
#include "cli/Commands.h"
#include "cli/TextTable.h"
#include "combat/Resolver.h"
#include "combat/Military.h"
#include "core/Errors.h"
#include "domain/Construction.h"
#include "core/TickPipeline.h"
#include "domain/Empire.h"
#include "domain/Fleet.h"
#include "gen/EmpireGen.h"
#include "gen/NameGen.h"
#include "items/RuleEngine.h"
#include "rng/Streams.h"
#include "mkt/OrderBook.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {
namespace {

std::vector<std::string> edictNames() {
    return {"战时配给", "言论管制", "军费扩张", "研究总动员", "边境戒严", "市场自由化",
            "满足军部", "满足商会", "满足技术官僚", "满足原教旨", "满足劳工", "满足旧贵族",
            "满足民粹", "满足辛迪加"};
}

FactionKind edictFaction(const std::string& name) {
    static const std::pair<const char*, FactionKind> kMap[] = {
        {"满足军部", FactionKind::Military},       {"满足商会", FactionKind::Merchant},
        {"满足技术官僚", FactionKind::Technocrat}, {"满足原教旨", FactionKind::Fundamentalist},
        {"满足劳工", FactionKind::Labor},          {"满足旧贵族", FactionKind::Nobility},
        {"满足民粹", FactionKind::Populist},       {"满足辛迪加", FactionKind::Syndicate},
    };
    for (const auto& kv : kMap)
        if (name == kv.first) return kv.second;
    return FactionKind::Count;
}

}  // namespace

int cmdColony(CliEnv& env, const Args& args) {
    env.loadState();
    if (args.has("detail")) {
        i64 sid = args.getInt("detail", -1);
        if (sid < 0 || !env.st.system(static_cast<u32>(sid))) fail(ExitCode::BadArgs, "星系不存在");
        out(colonyReport(env.st, kPlayerId, static_cast<u32>(sid)));
        return 0;
    }
    if (args.posCount() == 0) { out(colonyReport(env.st, kPlayerId)); return 0; }
    if (env.player().apLeft < apcost::kColony) fail(ExitCode::IllegalAction, "行动点不足（需要 2 AP）");
    std::string message;
    if (!startColony(env.st, kPlayerId, static_cast<u32>(args.posInt(0, -1)), &message))
        fail(ExitCode::IllegalAction, message);
    env.player().apLeft -= apcost::kColony;
    env.commit("colony");
    out(message);
    out("使用 queue 查看工程和补给；完工后建立一处据点，随后 12 季逐步形成产能。");
    return 0;
}

int cmdShipBuild(CliEnv& env, const Args& args) {
    env.loadState();
    if (args.posCount() < 2) fail(ExitCode::BadArgs, "用法：greyfall ship-build <design> <system>");
    int designIdx = -1;
    i64 asNum = parseInt(args.pos(0), -1);
    if (asNum >= 0 && asNum < static_cast<i64>(env.player().designs.size())) designIdx = static_cast<int>(asNum);
    else {
        for (std::size_t i = 0; i < env.player().designs.size(); ++i)
            if (env.player().designs[i].name == args.pos(0)) designIdx = static_cast<int>(i);
    }
    if (designIdx < 0) fail(ExitCode::BadArgs, "未知设计【" + args.pos(0) + "】（用 `greyfall ship` 查看）");
    i64 sid = args.posInt(1, -1);
    const SystemNode* sys = env.st.system(static_cast<u32>(sid));
    if (sys == nullptr) fail(ExitCode::BadArgs, "星系不存在");
    if (sys->owner != kPlayerId) fail(ExitCode::IllegalAction, "只能在自己的星系建造");
    if (env.player().apLeft < apcost::kShipBuild) fail(ExitCode::IllegalAction, "行动点不足");

    // 造舰不再即时完成：需要船坞与工期（见 domain/Construction.cpp）。
    // 早期这里直接把舰队塞进 st.fleets —— 玩家可以在同一季爆出整支舰队，
    // 「军力积累需要时间」这条纵深设计被完全绕过。
    const FleetDesign& d = env.player().designs[static_cast<std::size_t>(designIdx)];
    std::string msg;
    if (!enqueueShip(env.st, kPlayerId, sys->id, d.id, &msg))
        fail(ExitCode::IllegalAction, msg);
    env.player().apLeft -= apcost::kShipBuild;
    env.commit("ship-build");
    out(style(msg, Style::Good));
    out("  战力 " + fixedStr(d.firepower, 0) + "  用 `greyfall queue` 查看进度。");
    return 0;
}

int cmdFleet(CliEnv& env, const Args& args) {
    env.loadState();
    if (!args.has("id")) fail(ExitCode::BadArgs, "用法：greyfall fleet --id X --order <o> [--to <system>]");
    i64 id = args.getInt("id", -1);
    Fleet* f = env.st.fleet(static_cast<u32>(id));
    if (f == nullptr) fail(ExitCode::BadArgs, "舰队不存在");
    if (f->owner != kPlayerId) fail(ExitCode::IllegalAction, "只能指挥自己的舰队");
    std::string order = toLower(args.get("order", ""));
    if (order.empty()) {
        out("舰队 " + f->name + " 当前命令：" + std::string(fleetOrderName(f->order)));
        out("可用命令：move|patrol|embargo|engage|escort|blockade|retreat");
        return 0;
    }
    FleetOrder o = FleetOrder::Idle;
    if (order == "move") o = FleetOrder::Move;
    else if (order == "patrol") o = FleetOrder::Patrol;
    else if (order == "embargo") o = FleetOrder::Embargo;
    else if (order == "engage") o = FleetOrder::Engage;
    else if (order == "escort") o = FleetOrder::Escort;
    else if (order == "blockade") o = FleetOrder::Blockade;
    else if (order == "retreat") o = FleetOrder::Retreat;
    else fail(ExitCode::BadArgs, "未知命令【" + order + "】");
    if (env.player().apLeft < apcost::kFleetOrder) fail(ExitCode::IllegalAction, "行动点不足");
    if (args.has("to")) {
        i64 to = args.getInt("to", -1);
        if (env.st.system(static_cast<u32>(to)) == nullptr) fail(ExitCode::BadArgs, "目标星系不存在");
        f->targetSystem = static_cast<u32>(to);
    } else if (o == FleetOrder::Move) {
        fail(ExitCode::BadArgs, "move 命令需要 --to <system>");
    }
    f->order = o;
    env.player().apLeft -= apcost::kFleetOrder;
    env.st.logEvent(LogPhase::Combat, "fleet.order",
                    f->name + " 命令 → " + std::string(fleetOrderName(o)), kPlayerId);
    env.commit("fleet");
    out(f->name + " 命令已设为【" + std::string(fleetOrderName(o)) + "】" +
        (f->targetSystem != kNoSystem ? ("，目标 " + env.st.system(f->targetSystem)->name) : ""));
    if (o == FleetOrder::Blockade) {
        // 封锁不再是「下达时一次性 +30%」，而是由舰队**每季维持**：
        // 舰队停留在目标星系期间封锁强度逐步建立（上限 80%，由战力决定），
        // 撤离后每季消退 20%。封锁会提高途经该星系的**贸易路线**风险并削减运量。
        SystemNode* s = env.st.system(f->targetSystem != kNoSystem ? f->targetSystem : f->system);
        if (s != nullptr) {
            out("  该舰队将每季维持 " + s->name + " 的封锁（按战力建立，上限 80%）。");
            out("  封锁会提高途经此地的贸易路线风险并削减运量；撤离后封锁逐季消退。");
        }
    }
    return 0;
}

int cmdEdict(CliEnv& env, const Args& args) {
    env.loadState();
    auto names = edictNames();
    if (args.posCount() == 0) {
        out(style("═══ 可用法令 ═══", Style::Heading));
        TextTable t;
        t.header({"法令", "效果"});
        t.row({"战时配给", "非信用点日常需求 -10%，民怨目标 +5%"});
        t.row({"言论管制", "情报防御 +10%，稳定目标 +5%，民怨目标 +3%"});
        t.row({"军费扩张", "军事修正 +15%，贸易毛利 -5%"});
        t.row({"研究总动员", "研究速率 +20%，民怨 +"});
        t.row({"边境戒严", "稳定目标 +8%，民怨目标 -5%，贸易毛利 -10%"});
        t.row({"市场自由化", "贸易毛利 +12%，民怨目标 +4%"});
        for (int i = 0; i < static_cast<int>(FactionKind::Count); ++i) {
            const char* fname = nullptr;
            switch (static_cast<FactionKind>(i)) {
                case FactionKind::Military: fname = "满足军部"; break;
                case FactionKind::Merchant: fname = "满足商会"; break;
                case FactionKind::Technocrat: fname = "满足技术官僚"; break;
                case FactionKind::Fundamentalist: fname = "满足原教旨"; break;
                case FactionKind::Labor: fname = "满足劳工"; break;
                case FactionKind::Nobility: fname = "满足旧贵族"; break;
                case FactionKind::Populist: fname = "满足民粹"; break;
                case FactionKind::Syndicate: fname = "满足辛迪加"; break;
                default: break;
            }
            if (fname) t.row({fname, "花费随领土规模增加，满意度 +25%；同派系间隔 8 季"});
        }
        out(t.render());
        out("");
        out(nationalEdictReport(env.st, kPlayerId));
        out("用法：greyfall edict <法令>");
        return 0;
    }
    std::string name = args.pos(0);
    if (std::find(names.begin(), names.end(), name) == names.end()) {
        fail(ExitCode::BadArgs, "未知法令【" + name + "】（运行 `greyfall edict` 查看全部）");
    }
    if (env.player().apLeft < apcost::kEdict) fail(ExitCode::IllegalAction, "行动点不足");

    // 派系诉求
    FactionKind fk = edictFaction(name);
    if (fk != FactionKind::Count) {
        std::string err;
        if (!satisfyFaction(env.st, kPlayerId, fk, &err)) fail(ExitCode::IllegalAction, err);
        env.player().apLeft -= apcost::kEdict;
        env.commit("edict");
        out(style("已颁布法令：" + name, Style::Good));
        out("  " + std::string(factionKindName(fk)) + " 满意度 +25%，诉求压力清零。");
        return 0;
    }

    std::string message;
    const int kind = static_cast<int>(std::find(names.begin(), names.end(), name) - names.begin());
    if (!activateNationalEdict(env.st, kPlayerId, kind, &message)) fail(ExitCode::IllegalAction, message);
    env.player().apLeft -= apcost::kEdict;
    env.commit("edict");
    out(message);
    return 0;
}

int cmdResearch(CliEnv& env, const Args& args) {
    env.loadState();
    if (args.posCount() == 0) {
        out("当前攻关：");
        for (int b = 0; b < kTechBranchCount; ++b) {
            u32 cur = env.player().tech.current[static_cast<std::size_t>(b)];
            out("  " + padRight(std::string(techBranchName(static_cast<TechBranch>(b))), 8) + " " +
                (cur == 0 ? "（自动选择）" : std::string(techInfo(static_cast<int>(cur)).nameZh)) + "   进度 " +
                fixedStr(env.player().tech.progress[static_cast<std::size_t>(b)], 1));
        }
        out("");
        out("用法：greyfall research <物理|社会|工程|生物|计算|灵能>");
        out("      greyfall research <科技 idName，如 comp6>");
        return 0;
    }
    std::string key = args.pos(0);

    // 查看当前立项
    if (key == "status" || key == "当前") {
        const TechState& ts = env.player().tech;
        if (ts.project == TechState::kNoTech) {
            out("当前没有立项。用 `greyfall research <分支|科技>` 立项。");
            return 0;
        }
        const TechInfo& ti = techInfo(static_cast<int>(ts.project));
        out(style("═══ 当前研究项目 ═══", Style::Heading));
        TextTable t;
        t.header({"项目", "值"});
        t.row({"科技", std::string(ti.nameZh) + "（" + std::string(ti.idName) + "）"});
        t.row({"分支", std::string(techBranchName(ti.branch))});
        t.row({"层级", std::to_string(ti.tier)});
        t.row({"进度", fixedStr(ts.projectProgress, 0) + " / " + std::to_string(ti.cost)});
        t.row({"已投入", std::to_string(ts.projectTicks) + " 季"});
        t.row({"最短工期", std::to_string(techMinTicks(ti.tier)) + " 季（投入再多也不能更快）"});
        t.row({"每季投入", fixedStr(ts.fundingPerTick, 0) + " cr"});
        int eta = techEtaTicks(ts);
        t.row({"预计完成", eta < 0 ? "—" : (std::to_string(eta) + " 季后")});
        out(t.render());
        return 0;
    }

    if (env.player().apLeft < apcost::kResearch) fail(ExitCode::IllegalAction, "行动点不足");

    // 解析目标：分支名 / 科技 idName
    int pick = -1;
    i64 funding = 8000;
    if (args.has("fund")) funding = args.getInt("fund", 8000);
    if (funding < 0) funding = 0;
    for (int b = 0; b < kTechBranchCount && pick < 0; ++b) {
        if (std::string(techBranchName(static_cast<TechBranch>(b))) != key &&
            std::string(techInfo(b * 16).idName).substr(0, 4) != key)
            continue;
        for (int i = 0; i < kTechCount; ++i) {
            const TechInfo& ti = techInfo(i);
            if (ti.branch != static_cast<TechBranch>(b)) continue;
            if (!techAvailable(env.player().tech, i)) continue;
            if (pick < 0 || ti.tier < techInfo(pick).tier) pick = i;
        }
        if (pick < 0) fail(ExitCode::IllegalAction, "该分支已无可研究项");
    }
    if (pick < 0) {
        int byName = techIndexByName(key);
        if (byName < 0) fail(ExitCode::BadArgs, "未知分支或科技【" + key + "】");
        pick = byName;
    }

    std::string err;
    if (!techStartProject(env.player().tech, pick, &err)) fail(ExitCode::IllegalAction, err);

    // 立项本身只花行动点；资金是**每季投入**，由 advance 逐季扣除并推进进度。
    // 这样研究才是一条需要时间的长期投入，而不是一次性买断。
    if (env.player().treasury.rawValue() < Fixed(funding).rawValue()) {
        fail(ExitCode::IllegalAction,
             "国库不足：设定的每季投入为 " + fixedStr(Fixed(funding), 0) + " cr");
    }
    env.player().tech.fundingPerTick = Fixed(funding);
    env.player().apLeft -= apcost::kResearch;
    env.commit("research");

    const TechInfo& ti = techInfo(pick);
    out(style("已立项：【" + std::string(ti.nameZh) + "】", Style::Good));
    TextTable t;
    t.header({"项目", "值"});
    t.row({"分支", std::string(techBranchName(ti.branch))});
    t.row({"层级", std::to_string(ti.tier)});
    t.row({"所需研究点", std::to_string(ti.cost)});
    t.row({"最短工期", std::to_string(techMinTicks(ti.tier)) + " 季"});
    t.row({"每季投入", fixedStr(Fixed(funding), 0) + " cr（由 advance 逐季扣除）"});
    t.row({"预计完成", std::to_string(techEtaTicks(env.player().tech)) + " 季后"});
    out(t.render());
    out("  研究需要时间：每季推进一次，`greyfall research status` 可查看进度。");
    out("  同时只能攻关一项；中途换项会丢弃既有进度。");
    return 0;
}

int cmdMilitary(CliEnv& env, const Args& args) {
    (void)args;
    env.loadState();
    out(style("═══ 军备 ═══", Style::Heading));
    out("  军力不是开局写死的数字：上限由经济、人口与船坞建筑决定，");
    out("  实际军力以每季 5% 的速率向上限靠拢 —— 造舰需要时间，被打残也要时间恢复。");
    out("");
    out(militaryReport(env.st, kPlayerId));
    out("");
    out("  建造【恒星基地/船坞】可显著提升军力上限。");
    return 0;
}

int cmdBuild(CliEnv& env, const Args& args) {
    env.loadState();
    if (args.posCount() < 2) fail(ExitCode::BadArgs, "用法：greyfall build <building> <planet>");
    int b = buildingIndexByName(args.pos(0));
    if (b < 0) {
        i64 num = parseInt(args.pos(0), -1);
        if (num >= 0 && num < kBuildingCount) b = static_cast<int>(num);
        else fail(ExitCode::BadArgs, "未知建筑【" + args.pos(0) + "】（用 `greyfall buildings` 查看）");
    }
    i64 pid = args.posInt(1, -1);
    Planet* p = env.st.planet(static_cast<u32>(pid));
    if (p == nullptr) fail(ExitCode::BadArgs, "行星不存在");
    if (p->owner != kPlayerId) fail(ExitCode::IllegalAction, "只能在自己的行星上建造");
    if (env.player().apLeft < apcost::kBuild) fail(ExitCode::IllegalAction, "行动点不足");
    const BuildingInfo& bi = buildingInfo(b);
    if (bi.requireTech >= 0 && !techCompleted(env.player().tech, bi.requireTech)) {
        fail(ExitCode::IllegalAction, "需要先完成科技【" +
                                          std::string(techInfo(static_cast<int>(bi.requireTech)).nameZh) + "】");
    }
    if (bi.unique) {
        for (u32 existing : p->buildings) {
            if (static_cast<int>(existing & 0xFFu) == b) {
                fail(ExitCode::IllegalAction, "该行星已有【" + std::string(bi.nameZh) + "】（唯一建筑）");
            }
        }
    }
    // 费用与门槛检查全部交给 enqueueBuilding —— 建筑不再即时完成，
    // 而是进入行星的建造队列，逐季推进（见 domain/Construction.cpp）。
    std::string msg;
    if (!enqueueBuilding(env.st, kPlayerId, p->id, b, &msg))
        fail(ExitCode::IllegalAction, msg);
    env.player().apLeft -= apcost::kBuild;
    env.commit("build");
    out(style(msg, Style::Good));
    out("  效果：" + std::string(buildingEffectName(bi.effect)) + " +" + fixedStrPlain(bi.effectValue, 2) +
        "  维护 " + std::to_string(bi.upkeep) + " cr/季");
    out("  用 `greyfall queue` 查看进度。");
    return 0;
}

int cmdMega(CliEnv& env, const Args& args) {
    env.loadState();
    if (args.posCount() < 2) fail(ExitCode::BadArgs, "用法：greyfall mega <id> <system>");
    int definition = megastructureIndexByName(args.pos(0));
    if (definition < 0) definition = static_cast<int>(args.posInt(0, -1));
    if (definition < 0 || definition >= kMegastructureCount) fail(ExitCode::BadArgs, "未知巨构");
    const auto& info = megastructureInfo(definition);
    if (env.player().apLeft < info.apCost) fail(ExitCode::IllegalAction, "行动点不足");
    std::string message;
    if (!startMegaStage(env.st, kPlayerId, definition, static_cast<u32>(args.posInt(1, -1)), &message))
        fail(ExitCode::IllegalAction, message);
    env.player().apLeft -= info.apCost;
    env.commit("mega");
    out(message);
    out("本阶段完成后，再用相同命令启动下一阶段；全部完成才提供收益。");
    return 0;
}

int cmdAscend(CliEnv& env, const Args& args) {
    env.loadState();
    if (args.posCount() == 0) {
        out("飞升：每条路径限完成一次，每纪元最多两条，同时只能进行一项。需要 3 AP。");
        out("心灵 / 机械 / 基因 / 虚空：对应灵能 / 工程 / 生物 / 物理分支完成 4 项科技，工期 12 季。");
        out("普通路径启动 50000 cr、凝聚力 2000（虚空 2500）；超脱需灵能 12 项、120000 cr、凝聚力 4000、24 季。");
        out("各路径另需数据晶体 200、部件 150；每季维护 1000 cr、能源 20、数据晶体 3。");
        out("心灵：情报防御 +15%；机械：建造速率 +25%、人口增长 -10%；基因：人口增长 +30%、稳定目标 +10%。");
        out("虚空：殖民启动资金 -40%（总折扣最多 50%）、稳定目标 -15%；超脱：推进超脱结局维度。");
        out("用法：greyfall ascend <心灵|机械|基因|虚空|超脱>");
        return 0;
    }
    int path = -1;
    for (int i = 0; i < 5; ++i) if (args.pos(0) == ascensionName(i)) path = i;
    if (path < 0) fail(ExitCode::BadArgs, "未知飞升路径");
    if (env.player().apLeft < apcost::kAscend) fail(ExitCode::IllegalAction, "行动点不足");
    std::string message;
    if (!startAscension(env.st, kPlayerId, path, &message)) fail(ExitCode::IllegalAction, message);
    env.player().apLeft -= apcost::kAscend;
    env.commit("ascend");
    out(message);
    return 0;
}

int cmdRecruit(CliEnv& env, const Args&) {
    env.loadState();
    if (env.player().apLeft < apcost::kRecruit) fail(ExitCode::IllegalAction, "行动点不足");
    std::string message;
    if (!startRecruitment(env.st, kPlayerId, &message)) fail(ExitCode::IllegalAction, message);
    env.player().apLeft -= apcost::kRecruit;
    env.commit("recruit");
    out(message);
    out("训练完成时，为报名且仍在本国境内、未参战的舰队补充士气 10%、补给 15%。");
    return 0;
}

}  // namespace gf
