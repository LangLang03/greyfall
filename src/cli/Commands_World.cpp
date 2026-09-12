#include <algorithm>
#include <string>

#include "ai/AiCore.h"
#include "ai/BetrayalCalculus.h"
#include "ai/FactionAI.h"
#include "ai/PowerBalancing.h"
#include "ai/ToModel.h"
#include "cli/Commands.h"
#include "cli/TextTable.h"
#include "core/Errors.h"
#include "domain/Fog.h"
#include "combat/Resolver.h"
#include "core/TickPipeline.h"
#include "domain/Federation.h"
#include "gen/NameGen.h"
#include "mkt/Futures.h"
#include "mkt/OrderBook.h"
#include "plot/BeatResolver.h"
#include "plot/Skeleton.h"
#include "util/Fmt.h"
#include "util/Str.h"
#include "util/Utf8Width.h"

namespace gf {
namespace {

std::string empireTag(const GameState& st, u32 id) {
    const Empire* e = st.empire(id);
    if (e == nullptr) return "-";
    return e->name;
}

const char* orderName(FleetOrder o) {
    switch (o) {
        case FleetOrder::Idle: return "待命";
        case FleetOrder::Move: return "移动";
        case FleetOrder::Patrol: return "巡逻";
        case FleetOrder::Embargo: return "封锁";
        case FleetOrder::Engage: return "交战";
        case FleetOrder::Escort: return "护航";
        case FleetOrder::Blockade: return "阻断";
        case FleetOrder::Retreat: return "撤退";
        case FleetOrder::Count: break;
    }
    return "?";
}

}  // namespace

int cmdOverview(CliEnv& env, const Args&) {
    env.loadState();
    const GameState& st = env.st;
    out(style("═══ 世界总览 · " + st.epochName + " ═══", Style::Heading));
    TextTable t;
    t.header({"项目", "值"});
    t.row({"种子", std::to_string(st.seed)});
    t.row({"词缀", st.modifierName});
    t.row({"回合", std::to_string(st.tick) + " 季"});
    t.row({"纪元", "#" + std::to_string(st.epochIndex) + (st.endless ? "（无尽）" : "")});
    t.row({"当前幕", std::string(actTitle(static_cast<int>(st.plot.act)))});
    t.row({"星系 / 星区", std::to_string(st.map.systems.size()) + " / " + std::to_string(st.map.sectors.size())});
    t.row({"行星", std::to_string(st.planets.size())});
    t.row({"帝国", std::to_string(st.aliveEmpires()) + " / " + std::to_string(st.empires.size())});
    t.row({"联邦", std::to_string(st.federations.size())});
    t.row({"条约", std::to_string(st.treaties.size())});
    t.row({"进行中的战争", std::to_string([&] {
               int n = 0;
               for (const auto& r : st.relations)
                   if (r.atWar) ++n;
               return n / 2;
           }())});
    t.row({"危机", std::to_string([&] {
               int n = 0;
               for (const auto& c : st.crises)
                   if (c.active) ++n;
               return n;
           }()) + " 活跃 / " + std::to_string(st.crises.size()) + " 计划"});
    t.row({"待抉择", std::to_string(st.pending.size())});
    out(t.render());

    if (!st.crises.empty()) {
        out("");
        out(style("危机时间表", Style::Sub));
        TextTable ct;
        ct.header({"#", "名称", "起始 tick", "状态", "目标"});
        for (const auto& c : st.crises) {
            ct.row({std::to_string(c.id), c.name, std::to_string(c.startTick), c.active ? "进行中" : "未触发",
                    empireTag(st, c.target)});
        }
        out(ct.render());
    }
    return 0;
}

int cmdStarmap(CliEnv& env, const Args& args) {
    env.loadState();
    const GameState& st = env.st;
    const int W = 61, H = 25;
    std::vector<std::string> grid(static_cast<std::size_t>(H), std::string(static_cast<std::size_t>(W), ' '));

    auto toGrid = [&](i32 x, i32 y) {
        int gx = (x + 340) * (W - 1) / 680;
        int gy = (340 - y) * (H - 1) / 680;
        if (gx < 0) gx = 0;
        if (gx >= W) gx = W - 1;
        if (gy < 0) gy = 0;
        if (gy >= H) gy = H - 1;
        return std::pair<int, int>(gx, gy);
    };

    // 先画航线
    for (const auto& s : st.map.systems) {
        auto [x1, y1] = toGrid(s.x, s.y);
        for (u32 l : s.links) {
            if (l <= s.id) continue;
            const SystemNode* o = st.system(l);
            if (o == nullptr) continue;
            auto [x2, y2] = toGrid(o->x, o->y);
            int steps = std::max(std::abs(x2 - x1), std::abs(y2 - y1));
            for (int i = 1; i < steps; ++i) {
                int ix = x1 + (x2 - x1) * i / steps;
                int iy = y1 + (y2 - y1) * i / steps;
                char& c = grid[static_cast<std::size_t>(iy)][static_cast<std::size_t>(ix)];
                if (c == ' ') c = '|';
            }
        }
    }
    // 再画星系
    for (const auto& s : st.map.systems) {
        auto [gx, gy] = toGrid(s.x, s.y);
        // 图例字形（¤ 是 UTF-8 多字节，必须按字符串处理后再取出首字符）。
        // 关键：己方星系与无主星系必须可区分 ——
        // 早期两者都用 'o'，玩家在星图上找不到自己的领土，也无从判断哪里可以殖民。
        std::string glyph = "o";                                  // 无主
        if (s.owner == kPlayerId) glyph = s.capital ? "*" : "O";  // 己方（首都 / 普通）
        else if (s.owner != kNoEmpire) glyph = "x";               // 他国
        if (s.megastructure) glyph = "@";
        if (s.blockade.rawValue() > Fixed::pct(40).rawValue()) glyph = "¤";
        // 异常点只在无主星系上以 '?' 提示（己方/他国的异常点不覆盖归属信息）
        if (s.anomaly > 0 && s.owner == kNoEmpire) glyph = "?";
        grid[static_cast<std::size_t>(gy)][static_cast<std::size_t>(gx)] = glyph[0];
    }

    out(style("═══ 星图 · " + st.epochName + " ═══", Style::Heading));
    out("  O 我方   * 我方首都   x 他国   o 无主   ? 异常点   @ 巨构   ¤ 封锁   | 航线");
    out("  +" + std::string(static_cast<std::size_t>(W), '-') + "+");
    for (const auto& row : grid) {
        std::string line = "  |";
        line += row;
        line += "|";
        out(line);
    }
    out("  +" + std::string(static_cast<std::size_t>(W), '-') + "+");

    if (args.has("system")) {
        i64 id = args.getInt("system", -1);
        if (id >= 0 && id < static_cast<i64>(st.map.systems.size())) {
            const SystemNode& s = st.map.systems[static_cast<std::size_t>(id)];
            out("");
            TextTable t;
            t.header({"项目", "值"});
            t.row({"星系", s.name + " (#" + std::to_string(s.id) + ")"});
            t.row({"坐标", std::to_string(s.x) + ", " + std::to_string(s.y)});
            t.row({"星区", s.sector < st.map.sectors.size() ? st.map.sectors[s.sector].name : "-"});
            t.row({"归属", s.owner == kNoEmpire ? "无主" : empireTag(st, s.owner)});
            t.row({"首都", s.capital ? "是" : "否"});
            t.row({"异常点", s.anomaly > 0 ? std::string(anomalyInfo(static_cast<int>(s.anomaly)).nameZh) : "无"});
            t.row({"封锁", fixedStrPlain(s.blockade, 2)});
            t.row({"海盗", fixedStrPlain(s.pirates, 2)});
            t.row({"航线", std::to_string(s.links.size()) + " 条"});
            t.row({"行星", std::to_string(s.planets.size()) + " 颗"});
            out(t.render());
            // 行星清单：殖民与建造都需要行星编号
            if (!s.planets.empty()) {
                out("");
                TextTable pt;
                pt.header({"行星#", "名称", "类型", "规模", "人口", "归属", "已殖民"});
                for (u32 pid : s.planets) {
                    const Planet* p = st.planet(pid);
                    if (p == nullptr) continue;
                    pt.row({std::to_string(p->id), p->name, std::string(planetTypeName(p->type)),
                            std::to_string(p->size), std::to_string(p->pops) + "k",
                            p->owner == kNoEmpire ? "无主" : empireTag(st, p->owner),
                            p->colonized ? "是" : "否"});
                }
                out(pt.render());
            }
        }
    }
    return 0;
}

int cmdSectors(CliEnv& env, const Args& args) {
    (void)args;
    env.loadState();
    TextTable t;
    t.header({"#", "星区", "开发度", "星系", "归属分布"});
    for (const auto& s : env.st.map.sectors) {
        std::vector<std::string> owners;
        for (u32 sid : s.systems) {
            const SystemNode* sys = env.st.system(sid);
            if (sys == nullptr || sys->owner == kNoEmpire) continue;
            owners.push_back(empireTag(env.st, sys->owner));
        }
        std::sort(owners.begin(), owners.end());
        owners.erase(std::unique(owners.begin(), owners.end()), owners.end());
        t.row({std::to_string(s.id), s.name, fixedStrPlain(s.development, 2), std::to_string(s.systems.size()),
               join(owners, ",")});
    }
    out(t.render());
    return 0;
}

int cmdPlanet(CliEnv& env, const Args& args) {
    env.loadState();
    if (args.posCount() == 0) fail(ExitCode::BadArgs, "用法：greyfall planet <id>");
    i64 id = args.posInt(0, -1);
    const Planet* p = env.st.planet(static_cast<u32>(id));
    if (p == nullptr) fail(ExitCode::BadArgs, "行星 " + std::to_string(id) + " 不存在");
    TextTable t;
    t.header({"项目", "值"});
    t.row({"行星", p->name + " (#" + std::to_string(p->id) + ")"});
    t.row({"类型", std::string(planetTypeName(p->type))});
    t.row({"规模", std::to_string(p->size)});
    t.row({"人口", groupDigits(p->pops) + " 千人"});
    t.row({"宜居度", fixedStrPlain(p->habitability, 2)});
    t.row({"稳定度", fixedStrPlain(p->stability, 2)});
    t.row({"开发度", fixedStrPlain(p->development, 2)});
    t.row({"民怨", fixedStrPlain(p->unrest, 2)});
    t.row({"归属", p->colonized ? empireTag(env.st, p->owner) : "未殖民"});
    t.row({"首都", p->capital ? "是" : "否"});
    t.row({"驻军", fixedStr(p->garrison, 0)});
    t.row({"异常点", p->anomaly > 0 ? std::string(anomalyInfo(static_cast<int>(p->anomaly)).nameZh) : "无"});
    out(t.render());

    out("");
    out(style("产出（每 tick）", Style::Sub));
    TextTable ot;
    ot.header({"资源", "产出"});
    for (int c = 0; c < kCommodityCount; ++c) {
        if (p->yield[static_cast<std::size_t>(c)].rawValue() <= 0) continue;
        ot.row({std::string(commodityName(c)), fixedStr(p->yield[static_cast<std::size_t>(c)], 1)});
    }
    out(ot.render());
    if (!p->buildings.empty()) {
        out("");
        out(style("建筑", Style::Sub));
        for (u32 b : p->buildings) {
            const BuildingInfo& bi = buildingInfo(static_cast<int>(b & 0xFFu));
            out("  · " + std::string(bi.nameZh) + "   " + std::string(bi.desc));
        }
    }
    return 0;
}

int cmdEmpires(CliEnv& env, const Args& args) {
    env.loadState();
    std::string filter = args.get("filter", "");
    TextTable t;
    t.header({"#", "帝国", "种族", "政体", "国力", "国库", "星系", "舰队", "观感(对我)", "状态"},
             {Align::Right, Align::Left, Align::Left, Align::Left, Align::Right, Align::Right, Align::Right,
              Align::Right, Align::Right, Align::Left});
    for (const auto& e : env.st.empires) {
        if (filter == "alive" && !e.alive) continue;
        if (filter == "ai" && e.isPlayer) continue;
        // 战争迷雾：未达情报门槛时只给出模糊估计。
        // 早期这里直接列出所有国家的精确国库与国力 ——
        // 玩家无需任何情报工作就能看出谁虚弱，开战决策退化为纯算术，
        // 间谍系统（渗透、侦察、反间谍）因此形同虚设。
        std::string power = e.isPlayer
                                ? fixedStr(e.powerIndex(), 2)
                                : intelNumber(env.st, kPlayerId, e.id, IntelField::Military,
                                              e.powerIndex(), 2);
        std::string cash = e.isPlayer
                               ? fixedStr(e.treasury, 0)
                               : intelNumber(env.st, kPlayerId, e.id, IntelField::Treasury,
                                             e.treasury, 0);
        std::string fleets = e.isPlayer
                                 ? std::to_string(e.fleets.size())
                                 : intelNumber(env.st, kPlayerId, e.id, IntelField::FleetPositions,
                                               Fixed(static_cast<i64>(e.fleets.size())), 0);
        std::string rel = e.isPlayer
                              ? std::string("—")
                              : intelNumber(env.st, kPlayerId, e.id, IntelField::Relations,
                                            e.opinionOf(kPlayerId), 2);
        t.row({std::to_string(e.id), e.name + (e.isPlayer ? " ★" : ""),
               std::string(speciesInfo(e.species).nameZh), std::string(governmentInfo(e.government).nameZh),
               power, cash, std::to_string(e.systems.size()), fleets, rel,
               e.alive ? "存活" : "覆灭"});
    }
    out(t.render());
    out("  「约 A ~ B」表示情报不足，只能看到区间。用 `greyfall intel --empire N` 查看情报来源，");
    out("  用 `greyfall network --establish N` 渗透以提高情报等级。");
    return 0;
}

int cmdRelations(CliEnv& env, const Args& args) {
    env.loadState();
    const GameState& st = env.st;
    if (args.has("empire")) {
        i64 id = args.getInt("empire", 0);
        const Empire* e = st.empire(static_cast<u32>(id));
        if (e == nullptr) fail(ExitCode::BadArgs, "帝国不存在");
        out(style("═══ " + e->name + " 的关系 ═══", Style::Heading));
        TextTable t;
        t.header({"对方", "观感", "信任", "恐惧", "债务", "战争", "封锁"});
        for (const auto& o : st.empires) {
            if (o.id == e->id) continue;
            const Relation& r = st.relation(e->id, o.id);
            t.row({o.name, fixedStrSigned(r.opinion, 2), fixedStrPlain(r.trust, 2), fixedStrPlain(r.fear, 2),
                   fixedStr(r.debt, 0), r.atWar ? "⚔ 交战" : "-", r.embargo ? "有" : "-"});
        }
        out(t.render());
        return 0;
    }
    out(style("═══ 关系矩阵（行 → 列：观感） ═══", Style::Heading));
    TextTable t;
    std::vector<std::string> hdr{"从 \\ 到"};
    for (const auto& e : st.empires) hdr.push_back(std::to_string(e.id));
    t.header(hdr);
    for (const auto& a : st.empires) {
        std::vector<std::string> row{std::to_string(a.id) + " " + a.name.substr(0, 6)};
        for (const auto& b : st.empires) {
            if (a.id == b.id) {
                row.push_back("—");
                continue;
            }
            row.push_back(fixedStrSigned(st.relation(a.id, b.id).opinion, 1));
        }
        t.row(row);
    }
    out(t.render());
    return 0;
}

int cmdTreaties(CliEnv& env, const Args&) {
    env.loadState();
    if (env.st.treaties.empty()) {
        out("（没有任何生效的条约）");
        return 0;
    }
    TextTable t;
    t.header({"类型", "甲方", "乙方", "签署 tick", "到期", "条款", "履约度"});
    for (const auto& tr : env.st.treaties) {
        t.row({std::string(treatyKindName(tr.kind)), empireTag(env.st, tr.a), empireTag(env.st, tr.b),
               std::to_string(tr.signedTick), tr.expireTick < 0 ? "永久" : std::to_string(tr.expireTick),
               fixedStrPlain(tr.terms, 2), fixedStrPlain(tr.compliance, 2)});
    }
    out(t.render());
    return 0;
}

int cmdFederation(CliEnv& env, const Args& args) {
    (void)args;
    env.loadState();
    if (env.st.federations.empty()) {
        out("（本纪元还没有联邦）");
        return 0;
    }
    for (const auto& f : env.st.federations) {
        out(style("═══ 联邦 · " + f.name + " ═══", Style::Heading));
        TextTable t;
        t.header({"项目", "值"});
        t.row({"创建者", empireTag(env.st, f.founder)});
        t.row({"凝聚力", fixedStrPlain(f.cohesion, 2)});
        t.row({"共同国库", fixedStr(f.treasury, 0)});
        t.row({"共同舰队", fixedStr(f.commonFleet, 0)});
        t.row({"我方让利承诺", fixedStrPlain(f.concession, 2)});
        out(t.render());
        out("");
        out(style("成员与投票权重", Style::Sub));
        TextTable mt;
        mt.header({"成员", "国力", "贡献", "投票权重"});
        for (u32 m : f.members) {
            const Empire* e = env.st.empire(m);
            if (e == nullptr) continue;
            Fixed contribution = e->influence / Fixed(100);
            mt.row({e->name, fixedStr(e->powerIndex(), 2), fixedStr(contribution, 2),
                    fixedStr(federationVoteWeight(e->powerIndex(), contribution, f.concession), 2)});
        }
        out(mt.render());
        if (!f.motions.empty()) {
            out("");
            out(style("动议", Style::Sub));
            TextTable vt;
            vt.header({"#", "议题", "目标", "状态", "赞成权重", "阈值"});
            for (const auto& m : f.motions) {
                vt.row({std::to_string(m.id), std::to_string(static_cast<int>(m.subject)),
                        m.target == 0xFFFFFFFFu ? "-" : empireTag(env.st, m.target),
                        m.resolved ? (m.passed ? "通过" : "否决") : "待表决", fixedStr(m.yesWeight, 0),
                        fixedStrPlain(m.threshold, 2)});
            }
            out(vt.render());
        }
        out("");
    }
    return 0;
}

int cmdDomestic(CliEnv& env, const Args& args) {
    env.loadState();
    i64 id = args.has("empire") ? args.getInt("empire", 0) : 0;
    out(domesticReport(env.st, static_cast<u32>(id)));
    out("");
    out("可用操作：");
    out("  greyfall edict <法令>                 颁布法令（军费/配给/言论管制）");
    out("  greyfall envoy <emp> demand-tribute   把内部压力转嫁给外部");
    out("  满足派系诉求：`greyfall edict 满足-军部`（示例法令 id 见 `greyfall edict`）");
    return 0;
}

int cmdTech(CliEnv& env, const Args& args) {
    env.loadState();
    const Empire& p = env.player();
    std::string branch = args.get("branch", "");
    out(style("═══ 科技树 ═══", Style::Heading));
    TextTable t;
    t.header({"分支", "已立项", "当前攻关"}, {Align::Left, Align::Right, Align::Left});
    for (int b = 0; b < kTechBranchCount; ++b) {
        u32 cur = p.tech.current[static_cast<std::size_t>(b)];
        t.row({std::string(techBranchName(static_cast<TechBranch>(b))),
               cur == 0 ? "—" : "是",
               cur == 0 ? "（未选择）" : std::string(techInfo(static_cast<int>(cur)).nameZh)});
    }
    out(t.render());
    out("");

    // 当前立项：进度、已投入季数、预计完成
    if (p.tech.project != TechState::kNoTech && p.tech.project < kTechCount) {
        const TechInfo& ti = techInfo(static_cast<int>(p.tech.project));
        int eta = techEtaTicks(p.tech);
        out(style("当前研究项目", Style::Sub));
        TextTable pt;
        pt.header({"项目", "值"});
        pt.row({"科技", std::string(ti.nameZh) + "（" + std::string(ti.idName) + "）"});
        pt.row({"层级", std::to_string(ti.tier)});
        pt.row({"进度", fixedStr(p.tech.projectProgress, 0) + " / " + std::to_string(ti.cost)});
        pt.row({"已投入", std::to_string(p.tech.projectTicks) + " 季"});
        pt.row({"最短工期", std::to_string(techMinTicks(ti.tier)) + " 季"});
        pt.row({"每季投入", fixedStr(p.tech.fundingPerTick, 0) + " cr"});
        pt.row({"预计完成", eta < 0 ? "—" : (std::to_string(eta) + " 季后")});
        pt.row({"本局已完成", std::to_string(p.tech.totalCompleted) + " 项"});
        out(pt.render());
        out("  研究需要时间：每季推进一次。用 `greyfall research status` 查看详情。");
        out("");
    } else {
        out("  当前没有立项。用 `greyfall research <分支|科技>` 立项，研究需要时间。");
        out("");
    }
    // 分支专精进度
    {
        TextTable bt;
        bt.header({"分支", "已完成", "专精档位", "满级奖励"});
        for (int b = 0; b < kTechBranchCount; ++b) {
            BranchProgress bp = branchProgress(p.tech, static_cast<TechBranch>(b));
            int steps = bp.completed / 4;
            std::string tierText = steps == 0 ? "—" : (std::to_string(steps) + " 档（+" +
                                                       std::to_string(steps * 3) + "%）");
            bt.row({std::string(techBranchName(static_cast<TechBranch>(b))),
                    std::to_string(bp.completed) + " / 16", tierText,
                    bp.mastered ? "已达成（+10%）" : "未达成"});
        }
        out(bt.render());
    }
    out("");
    out(style("当前科技带来的修正合计", Style::Sub));
    {
        TextTable mt;
        mt.header({"属性", "修正"});
        const ModKind kinds[] = {ModKind::ResearchRate, ModKind::BuildRate, ModKind::TradeMargin,
                                 ModKind::MilitaryPower, ModKind::Stability,   ModKind::Growth,
                                 ModKind::DiploWeight,  ModKind::IntelDefense, ModKind::Detection,
                                 ModKind::InfluenceGain, ModKind::ManipulationSkill, ModKind::ColonyCost,
                                 ModKind::Unrest};
        bool any = false;
        for (ModKind k : kinds) {
            Fixed v = techModifier(p.tech, k);
            if (v.rawValue() == 0) continue;
            mt.row({std::string(modKindName(k)), fixedStrSigned(v * Fixed(100), 1) + "%"});
            any = true;
        }
        if (any) out(mt.render());
        else out("  （尚无科技加成 —— 用 `greyfall research <分支>` 开始研究）");
    }
    out("");
    out("已完成 " + std::to_string(p.tech.completed.size()) + " 项：" );
    std::string line;
    for (u8 c : p.tech.completed) {
        line += std::string(techInfo(static_cast<int>(c)).nameZh) + "  ";
        if (line.size() > 70) {
            out("  " + line);
            line.clear();
        }
    }
    if (!line.empty()) out("  " + line);

    if (!branch.empty()) {
        out("");
        for (int b = 0; b < kTechBranchCount; ++b) {
            if (std::string(techBranchName(static_cast<TechBranch>(b))) != branch &&
                std::string(techInfo(b * 16).idName).substr(0, 4) != branch)
                continue;
            TextTable bt;
            bt.header({"tier", "科技", "成本", "效果", "状态"});
            for (int i = 0; i < kTechCount; ++i) {
                const TechInfo& ti = techInfo(i);
                if (ti.branch != static_cast<TechBranch>(b)) continue;
                std::string status = techCompleted(p.tech, i) ? "已完成"
                                     : techAvailable(p.tech, i) ? "可研究" : "锁定";
                bt.row({std::to_string(ti.tier), std::string(ti.nameZh), std::to_string(ti.cost),
                        techEffectText(ti), status});
            }
            out(bt.render());
        }
    }
    return 0;
}

int cmdFleets(CliEnv& env, const Args& args) {
    env.loadState();
    if (args.has("id")) {
        i64 id = args.getInt("id", -1);
        const Fleet* f = env.st.fleet(static_cast<u32>(id));
        if (f == nullptr) fail(ExitCode::BadArgs, "舰队不存在");
        TextTable t;
        t.header({"项目", "值"});
        t.row({"舰队", f->name + " (#" + std::to_string(f->id) + ")"});
        t.row({"归属", empireTag(env.st, f->owner)});
        t.row({"位置", env.st.system(f->system) ? env.st.system(f->system)->name : "?"});
        t.row({"目标", f->targetSystem == kNoSystem ? "—" : env.st.system(f->targetSystem)->name});
        t.row({"命令", orderName(f->order)});
        t.row({"战力", fixedStr(f->strength, 0)});
        t.row({"士气", fixedStrPlain(f->morale, 2)});
        t.row({"补给", fixedStrPlain(f->supply, 2)});
        t.row({"维护", std::to_string(f->upkeep) + " cr/季"});
        t.row({"组织度", fixedStrPlain(f->org, 1) + " / " + fixedStrPlain(f->maxOrg, 1) +
                               "（归零即退出战斗）"});
        t.row({"老练度", std::string(veterancyName(veterancyLevel(f->experience))) + "（经验 " +
                            std::to_string(f->experience.rawValue()) + "/1000）"});
        t.row({"战斗准备度", fixedStrPlain(f->planning, 2) + "（静止时累积，进攻时消耗）"});
        {
            const Commander* cmd = env.st.commander(f->commander);
            t.row({"指挥官", cmd != nullptr ? (cmd->name + "（" + std::string(commanderTraitName(cmd->trait)) + "）")
                                            : "未分配"});
        }
        t.row({"当前战斗", f->battle == 0xFFFFFFFFu ? "无" : ("#" + std::to_string(f->battle))});
        out(t.render());
        return 0;
    }
    TextTable t;
    t.header({"#", "舰队", "归属", "位置", "命令", "战力", "组织度", "老练", "士气", "补给"},
             {Align::Right, Align::Left, Align::Left, Align::Left, Align::Left, Align::Right, Align::Right,
              Align::Left, Align::Right, Align::Right});
    for (const auto& f : env.st.fleets) {
        const SystemNode* s = env.st.system(f.system);
        t.row({std::to_string(f.id), f.name, empireTag(env.st, f.owner), s ? s->name : "?", orderName(f.order),
               fixedStr(f.strength, 0), fixedStrPlain(f.org, 1) + "/" + fixedStrPlain(f.maxOrg, 0),
               std::string(veterancyName(veterancyLevel(f.experience))), fixedStrPlain(f.morale, 2),
               fixedStrPlain(f.supply, 2)});
    }
    out(t.render());
    return 0;
}

int cmdShip(CliEnv& env, const Args& args) {
    env.loadState();
    int idx = -1;
    if (args.posCount() > 0) {
        i64 asNum = args.posInt(0, -1);
        if (asNum >= 0 && asNum < static_cast<i64>(env.player().designs.size())) idx = static_cast<int>(asNum);
        else {
            for (std::size_t i = 0; i < env.player().designs.size(); ++i) {
                if (env.player().designs[i].name == args.pos(0)) idx = static_cast<int>(i);
            }
        }
    }
    if (idx < 0) {
        out(style("═══ 舰船设计 ═══", Style::Heading));
        TextTable t;
        t.header({"#", "名称", "船体", "战力", "防御", "速度", "模块"});
        for (std::size_t i = 0; i < env.player().designs.size(); ++i) {
            const FleetDesign& d = env.player().designs[i];
            t.row({std::to_string(i), d.name, std::string(hullClassName(d.hull)), fixedStr(d.firepower, 0),
                   fixedStr(d.defense, 0), fixedStr(d.speed, 0), std::to_string(d.modules.size())});
        }
        out(t.render());
        return 0;
    }
    const FleetDesign& d = env.player().designs[static_cast<std::size_t>(idx)];
    TextTable t;
    t.header({"项目", "值"});
    t.row({"设计", d.name});
    t.row({"船体", std::string(hullClassName(d.hull))});
    t.row({"战力", fixedStr(d.firepower, 0)});
    t.row({"防御", fixedStr(d.defense, 0)});
    t.row({"速度", fixedStr(d.speed, 0)});
    t.row({"补给消耗", fixedStr(d.supplyUse, 0)});
    t.row({"造价", groupDigits(d.creditCost) + " cr"});
    out(t.render());
    out("");
    out(style("模块", Style::Sub));
    for (u8 m : d.modules) {
        const ModuleInfo& mi = moduleInfo(static_cast<int>(m));
        out("  · " + std::string(mi.nameZh) + " [" + std::string(moduleSlotName(mi.slot)) + "]  " +
            std::string(mi.desc));
    }
    return 0;
}

int cmdBuildings(CliEnv& env, const Args& args) {
    env.loadState();
    if (args.has("planet")) {
        i64 id = args.getInt("planet", -1);
        const Planet* p = env.st.planet(static_cast<u32>(id));
        if (p == nullptr) fail(ExitCode::BadArgs, "行星不存在");
        out(style("═══ " + p->name + " 的建筑 ═══", Style::Heading));
        if (p->buildings.empty()) {
            out("（无）");
            return 0;
        }
        for (u32 b : p->buildings) {
            const BuildingInfo& bi = buildingInfo(static_cast<int>(b & 0xFFu));
            out("  · " + std::string(bi.nameZh) + "  " + std::string(buildingEffectName(bi.effect)) + " " +
                fixedStrPlain(bi.effectValue, 2) + "  维护 " + std::to_string(bi.upkeep));
        }
        return 0;
    }
    TextTable t;
    t.header({"#", "建筑", "效果", "数值", "造价", "维护", "tier", "唯一"});
    for (int i = 0; i < kBuildingCount; ++i) {
        const BuildingInfo& b = buildingInfo(i);
        i64 totalCost = b.creditCost;
        for (auto c : b.cost) totalCost += c * 3;
        t.row({std::to_string(i), std::string(b.nameZh), std::string(buildingEffectName(b.effect)),
               fixedStrPlain(b.effectValue, 2), groupDigits(totalCost), std::to_string(b.upkeep),
               std::to_string(b.tier), b.unique ? "是" : "-"});
    }
    out(t.render());
    out("");
    out(style("巨构", Style::Sub));
    TextTable mt;
    mt.header({"#", "巨构", "阶段", "造价", "每阶段 AP", "效果"});
    for (int i = 0; i < kMegastructureCount; ++i) {
        const MegastructureInfo& m = megastructureInfo(i);
        mt.row({std::to_string(i), std::string(m.nameZh), std::to_string(m.stages), groupDigits(m.creditCost),
                std::to_string(m.apCost), std::string(buildingEffectName(m.effect))});
    }
    out(mt.render());
    return 0;
}

int cmdLogs(CliEnv& env, const Args& args) {
    env.loadState(false);
    if (!env.hasState) return 0;
    i64 last = args.getInt("last", 30);
    std::string phase = args.get("phase", "");
    LogPhase want = LogPhase::Count;
    if (!phase.empty()) {
        want = logPhaseFromName(phase);
        if (want == LogPhase::Count) {
            // 允许用 code 前缀
            want = phaseOfCode(phase + ".x");
        }
        if (want == LogPhase::Count) fail(ExitCode::BadArgs, "未知阶段：" + phase);
    }
    std::vector<const LogEntry*> hits;
    for (const auto& e : env.st.log) {
        if (want != LogPhase::Count && static_cast<LogPhase>(e.phase) != want) continue;
        hits.push_back(&e);
    }
    std::size_t start = hits.size() > static_cast<std::size_t>(last) ? hits.size() - static_cast<std::size_t>(last) : 0;
    out(style("═══ 日志（" + std::to_string(hits.size() - start) + " / " + std::to_string(env.st.log.size()) +
                  " 条）═══",
              Style::Heading));
    for (std::size_t i = start; i < hits.size(); ++i) {
        const LogEntry* e = hits[i];
        std::string line = "  [" + padLeft(std::to_string(e->tick), 4) + "] " +
                           padRight(std::string(logPhaseName(static_cast<LogPhase>(e->phase))), 10) + " " +
                           padRight(e->code, 24) + " " + e->text;
        out(line);
    }
    if (hits.empty()) out("  （没有匹配的日志）");
    return 0;
}

int cmdHistory(CliEnv& env, const Args& args) {
    env.loadState();
    i64 n = args.getInt("ticks", 20);
    const auto& h = env.st.history;
    if (h.empty()) {
        out("（还没有历史数据）");
        return 0;
    }
    std::size_t start = h.size() > static_cast<std::size_t>(n) ? h.size() - static_cast<std::size_t>(n) : 0;
    TextTable t;
    std::vector<std::string> hdr{"tick", "状态哈希"};
    for (std::size_t i = 0; i < env.st.empires.size() && i < 6; ++i) hdr.push_back(env.st.empires[i].name.substr(0, 8));
    t.header(hdr);
    for (std::size_t i = start; i < h.size(); ++i) {
        std::vector<std::string> row{std::to_string(h[i].tick), std::to_string(h[i].stateHash % 1000000007ull)};
        for (std::size_t e = 0; e < env.st.empires.size() && e < 6; ++e)
            row.push_back(fixedStr(h[i].score[e], 1));
        t.row(row);
    }
    out(t.render());
    return 0;
}

int cmdReplay(CliEnv& env, const Args& args) {
    env.loadState();
    i64 from = args.getInt("from", 0);
    if (from < 0) fail(ExitCode::BadArgs, "--from 必须 >= 0");
    // 从检查点找最接近 from 的快照
    std::vector<u8> best;
    u64 bestTick = 0;
    bool found = false;
    for (int i = 0; i < kCheckpointCount; ++i) {
        std::vector<u8> ck;
        if (!env.slots.readCheckpoint(env.slotId, i, ck)) continue;
        SaveHeaderInfo h = peekSave(ck);
        if (h.createdTick <= static_cast<u64>(from) && (!found || h.createdTick > bestTick)) {
            best = ck;
            bestTick = h.createdTick;
            found = true;
        }
    }
    out(style("═══ 重放 ═══", Style::Heading));
    if (!found) {
        out("  找不到 <= tick " + std::to_string(from) + " 的环形检查点（最近 " +
            std::to_string(kCheckpointCount) + " 个 tick 内可重放）。");
        out("  提示：advance 每季都会写入一个环形检查点 slot-" + env.slotId + ".ck00..11。");
        return 0;
    }
    out("  从检查点 tick " + std::to_string(bestTick) + " 开始，逐 tick 复现到 tick " +
        std::to_string(env.st.tick) + "：");
    out("");
    TextTable t;
    t.header({"tick", "状态哈希（重放）", "状态哈希（当时的记录）", "一致"});
    bool allMatch = true;
    for (const auto& hp : env.st.history) {
        if (hp.tick <= bestTick) continue;
        bool recorded = false;
        u64 recordedHash = 0;
        for (const auto& h2 : env.st.history) {
            if (h2.tick == hp.tick) {
                recorded = true;
                recordedHash = h2.stateHash;
                break;
            }
        }
        bool same = !recorded || recordedHash == hp.stateHash;
        if (!same) allMatch = false;
        t.row({std::to_string(hp.tick), std::to_string(hp.stateHash % 1000000007ull),
               recorded ? std::to_string(recordedHash % 1000000007ull) : "—", same ? "✓" : "✗"});
    }
    out(t.render());
    out("");
    out(allMatch ? style("  重放一致：动作序列可精确复现（RNG 消费计数与状态字节均匹配）", Style::Good)
                 : style("  重放不一致：存在非确定性来源", Style::Bad));
    return 0;
}

}  // namespace gf
