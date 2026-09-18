#include "domain/Personnel.h"
#include "domain/Government.h"

#include <algorithm>
#include <string>

#include "util/TextTable.h"
#include "core/GameState.h"
#include "domain/Commander.h"
#include "domain/Empire.h"
#include "domain/Fleet.h"
#include "domain/Tech.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {
namespace {

/// 改选周期（民主政体）
constexpr u32 kElectionTerm = 40;

/// 招募科学家的影响力成本
constexpr i64 kScientistCost = 150;

std::string randomName(GameState& st, u32 empire, RngStream stream) {
    static const char* kFirst[] = {"阿", "贝", "柯", "德", "恩", "法", "格", "海",
                                   "伊", "杰", "卡", "洛", "米", "诺", "奥", "佩"};
    static const char* kLast[] = {"恩", "斯", "尔", "顿", "森", "华", "理", "文",
                                  "德", "拉", "克", "姆", "诺", "维", "奇", "亚"};
    std::string s;
    s += kFirst[st.rng.pick(stream, sizeof(kFirst) / sizeof(kFirst[0]))];
    s += kLast[st.rng.pick(stream, sizeof(kLast) / sizeof(kLast[0]))];
    if (st.rng.chance(stream, Fixed::pct(50))) {
        s += "·";
        s += kLast[st.rng.pick(stream, sizeof(kLast) / sizeof(kLast[0]))];
    }
    (void)empire;
    return s;
}

}  // namespace

std::string_view rulerTraitName(RulerTrait t) {
    switch (t) {
        case RulerTrait::Administrator: return "行政干才";
        case RulerTrait::Warlord: return "军事强人";
        case RulerTrait::Scientist: return "学者出身";
        case RulerTrait::Merchant: return "商界背景";
        case RulerTrait::Demagogue: return "煽动家";
        case RulerTrait::Reformer: return "改革者";
        case RulerTrait::None:
        case RulerTrait::Count: break;
    }
    return "无";
}

std::string rulerTraitDesc(RulerTrait t) {
    switch (t) {
        case RulerTrait::Administrator:
            return "出身官僚体系：建造速率 +10%、稳定度 +8%。施政稳健，但缺乏号召力。";
        case RulerTrait::Warlord:
            return "军人出身：军事力量 +12%、战争疲劳增长减半。国内主战派拥戴，商会不安。";
        case RulerTrait::Scientist:
            return "学者从政：研究速率 +15%。任内科技推进明显更快。";
        case RulerTrait::Merchant:
            return "商界巨子：贸易毛利 +10%、信用评级 +10%。国库充盈但军备常被忽视。";
        case RulerTrait::Demagogue:
            return "擅长煽动：影响力增益 +15%、民粹满意度 +10。但合法性积累较慢。";
        case RulerTrait::Reformer:
            return "改革派领袖：政策过渡期缩短、合法性 +10%。推行变革时阻力更小。";
        default: break;
    }
    return "";
}

void rulerModifiers(const GameState& st, u32 empire, std::vector<std::pair<int, Fixed>>* out) {
    const Empire* e = st.empire(empire);
    if (e == nullptr || out == nullptr) return;
    // 技能放大修正：技能 50% 为基准，每高 10% 让主修正多 20%
    Fixed scale = Fixed(1) + (e->ruler.skill - Fixed::pct(50)) * Fixed(2);
    auto push = [&](ModKind k, Fixed v) { out->push_back({static_cast<int>(k), v * scale}); };
    switch (e->ruler.trait) {
        case RulerTrait::Administrator:
            push(ModKind::BuildRate, Fixed::pct(10));
            push(ModKind::Stability, Fixed::pct(8));
            break;
        case RulerTrait::Warlord:
            push(ModKind::MilitaryPower, Fixed::pct(12));
            break;
        case RulerTrait::Scientist:
            push(ModKind::ResearchRate, Fixed::pct(15));
            break;
        case RulerTrait::Merchant:
            push(ModKind::TradeMargin, Fixed::pct(10));
            push(ModKind::CreditRating, Fixed::pct(10));
            break;
        case RulerTrait::Demagogue:
            push(ModKind::InfluenceGain, Fixed::pct(15));
            break;
        case RulerTrait::Reformer:
            push(ModKind::Stability, Fixed::pct(6));
            break;
        default: break;
    }
}

void rulerPhase(GameState& st) {
    for (auto& e : st.empires) {
        if (!e.alive) continue;
        const bool democratic = isElective(e.government);
        e.ruler.elected = democratic;

        // 每 10 季长一岁；过世即更替
        if (st.tick % 10 == 0) ++e.ruler.age;
        bool died = e.ruler.age > 70 && st.rng.chance(RngStream::Empire, Fixed::pct(6));
        // 任期届满由 governmentPhase 的完整选举流程处理。
        if (!died) continue;

        std::string oldName = e.ruler.name;
        e.ruler.name = randomName(st, e.id, RngStream::Empire);
        e.ruler.trait = static_cast<RulerTrait>(1 + static_cast<int>(
                                                        st.rng.pick(RngStream::Empire,
                                                                    static_cast<std::size_t>(RulerTrait::Count) - 1)));
        e.ruler.skill = Fixed::pct(30) + Fixed::pct(static_cast<i64>(st.rng.range(RngStream::Empire, 0, 50)));
        e.ruler.age = 40 + static_cast<u32>(st.rng.range(RngStream::Empire, 0, 15));
        e.ruler.reignStart = st.tick;
        if (democratic) {
            e.ruler.termEnd = static_cast<u32>(st.tick) + kElectionTerm;
            e.ruler.electionsWon = 0;
        } else {
            e.ruler.termEnd = 0;
        }
        // 更替会带来短期动荡（民主改选较轻）
        Fixed shock = Fixed::pct(6);
        e.stability = fxClamp(e.stability - shock, Fixed(0), Fixed(1));
        st.logEvent(LogPhase::Domestic, "ruler.change",
                    e.name + " 的领袖由 " + oldName + " 变更为 " + e.ruler.name + "（" +
                        std::string(rulerTraitName(e.ruler.trait)) + "）" +
                        "，前任过世",
                    e.id);
    }
}

std::string_view scientistFieldName(ScientistField f) {
    switch (f) {
        case ScientistField::Physics: return "物理";
        case ScientistField::Society: return "社会";
        case ScientistField::Engineering: return "工程";
        case ScientistField::Biology: return "生物";
        case ScientistField::Computing: return "计算";
        case ScientistField::Psionics: return "灵能";
        case ScientistField::Count: break;
    }
    return "?";
}

bool scientistRecruit(GameState& st, u32 empire, std::string* msg) {
    Empire* e = st.empire(empire);
    if (e == nullptr) {
        if (msg) *msg = "非法主体";
        return false;
    }
    if (e->influence.rawValue() < Fixed(kScientistCost).rawValue()) {
        if (msg) *msg = "影响力不足：招募科学家需要 " + std::to_string(kScientistCost);
        return false;
    }
    if (e->scientists.size() >= 6) {
        if (msg) *msg = "科研部最多同时容纳 6 位科学家";
        return false;
    }
    e->influence -= Fixed(kScientistCost);
    Scientist s;
    s.id = st.nextScientistId++;
    s.name = randomName(st, empire, RngStream::Empire);
    s.owner = empire;
    s.field = static_cast<ScientistField>(st.rng.pick(RngStream::Empire, static_cast<std::size_t>(ScientistField::Count)));
    s.skill = Fixed::pct(25) + Fixed::pct(static_cast<i64>(st.rng.range(RngStream::Empire, 0, 55)));
    s.recruitedTick = st.tick;
    if (msg)
        *msg = "招募了科学家 " + s.name + "（专长 " +
               std::string(scientistFieldName(s.field)) + "，能力 " +
               fixedStrPlain(s.skill * Fixed(100), 0) + "%）";
    e->scientists.push_back(std::move(s));
    return true;
}

bool scientistAssign(GameState& st, u32 empire, u32 scientistId, int branch, std::string* msg) {
    Empire* e = st.empire(empire);
    if (e == nullptr) {
        if (msg) *msg = "非法主体";
        return false;
    }
    if (branch < 0 || branch >= kTechBranchCount) {
        if (msg) *msg = "非法分支";
        return false;
    }
    for (auto& s : e->scientists) {
        if (s.id != scientistId) continue;
        s.assignedBranch = static_cast<u8>(branch);
        if (msg)
            *msg = s.name + " 已派往【" + std::string(techBranchName(static_cast<TechBranch>(branch))) +
                   "】分支";
        return true;
    }
    if (msg) *msg = "找不到该科学家";
    return false;
}

Fixed scientistBonus(const GameState& st, u32 empire, int branch) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return Fixed(0);
    Fixed best = Fixed(0);
    for (const auto& s : e->scientists) {
        if (s.assignedBranch != static_cast<u8>(branch)) continue;
        // 专长对口额外加成
        Fixed v = s.skill * Fixed::pct(40);
        if (static_cast<int>(s.field) == branch) v += Fixed::pct(15);
        if (v.rawValue() > best.rawValue()) best = v;
    }
    return best;
}

bool formationCreate(GameState& st, u32 empire, const std::string& name, std::string* msg) {
    Empire* e = st.empire(empire);
    if (e == nullptr) {
        if (msg) *msg = "非法主体";
        return false;
    }
    if (e->formations.size() >= 8) {
        if (msg) *msg = "最多同时编成 8 个集团军";
        return false;
    }
    Formation f;
    f.id = st.nextFormationId++;
    f.name = name.empty() ? ("第 " + std::to_string(e->formations.size() + 1) + " 集团军") : name;
    f.owner = empire;
    f.createdTick = st.tick;
    const u32 id = f.id;
    e->formations.push_back(std::move(f));
    if (msg) *msg = "已编成【" + e->formations.back().name + "】(#" + std::to_string(id) + ")";
    return true;
}

bool formationAddFleet(GameState& st, u32 empire, u32 formationId, u32 fleetId, std::string* msg) {
    Empire* e = st.empire(empire);
    Fleet* fl = st.fleet(fleetId);
    if (e == nullptr || fl == nullptr || fl->owner != empire) {
        if (msg) *msg = "非法主体或舰队不属于你";
        return false;
    }
    // 一支舰队只能属于一个集团军
    for (auto& f : e->formations) {
        if (f.id == formationId) continue;
        auto it = std::find(f.fleets.begin(), f.fleets.end(), fleetId);
        if (it != f.fleets.end()) f.fleets.erase(it);
    }
    for (auto& f : e->formations) {
        if (f.id != formationId) continue;
        if (std::find(f.fleets.begin(), f.fleets.end(), fleetId) == f.fleets.end())
            f.fleets.push_back(fleetId);
        if (msg) *msg = "舰队已编入【" + f.name + "】（现辖 " + std::to_string(f.fleets.size()) + " 支）";
        return true;
    }
    if (msg) *msg = "找不到该集团军";
    return false;
}

bool formationRemoveFleet(GameState& st, u32 empire, u32 formationId, u32 fleetId,
                          std::string* msg) {
    Empire* e = st.empire(empire);
    if (e == nullptr) {
        if (msg) *msg = "非法主体";
        return false;
    }
    for (auto& f : e->formations) {
        if (f.id != formationId) continue;
        auto it = std::find(f.fleets.begin(), f.fleets.end(), fleetId);
        if (it == f.fleets.end()) {
            if (msg) *msg = "该舰队不在这个集团军里";
            return false;
        }
        f.fleets.erase(it);
        if (msg) *msg = "舰队已移出【" + f.name + "】";
        return true;
    }
    if (msg) *msg = "找不到该集团军";
    return false;
}

bool formationAssignCommander(GameState& st, u32 empire, u32 formationId, u32 commanderId,
                              std::string* msg) {
    Empire* e = st.empire(empire);
    if (e == nullptr) {
        if (msg) *msg = "非法主体";
        return false;
    }
    const Commander* c = st.commander(commanderId);
    if (c == nullptr || c->owner != empire) {
        if (msg) *msg = "该指挥官不存在或不属于你";
        return false;
    }
    for (auto& f : e->formations) {
        if (f.id != formationId) continue;
        f.commander = commanderId;
        if (msg)
            *msg = "任命 " + c->name + " 为【" + f.name + "】司令（" +
                   commanderRankText(*c) + "）";
        return true;
    }
    if (msg) *msg = "找不到该集团军";
    return false;
}

bool formationDisband(GameState& st, u32 empire, u32 formationId, std::string* msg) {
    Empire* e = st.empire(empire);
    if (e == nullptr) {
        if (msg) *msg = "非法主体";
        return false;
    }
    for (auto it = e->formations.begin(); it != e->formations.end(); ++it) {
        if (it->id != formationId) continue;
        if (msg) *msg = "已解散【" + it->name + "】";
        e->formations.erase(it);
        return true;
    }
    if (msg) *msg = "找不到该集团军";
    return false;
}

const Formation* formationOfFleet(const GameState& st, u32 fleetId) {
    const Fleet* fl = st.fleet(fleetId);
    if (fl == nullptr) return nullptr;
    const Empire* e = st.empire(fl->owner);
    if (e == nullptr) return nullptr;
    for (const auto& f : e->formations) {
        if (std::find(f.fleets.begin(), f.fleets.end(), fleetId) != f.fleets.end()) return &f;
    }
    return nullptr;
}

Fixed formationPowerMultiplier(const GameState& st, u32 fleetId) {
    const Formation* f = formationOfFleet(st, fleetId);
    if (f == nullptr) return Fixed(1);
    Fixed mult = Fixed(1);
    // 协同度：最高 +15%
    mult += f->coordination * Fixed::pct(15);
    // 司令加成：按指挥官的攻防与规划
    const Commander* c = st.commander(f->commander);
    if (c != nullptr) {
        mult += (c->attack + c->defense + c->planning) / Fixed(12);
    }
    // 编制规模：3 支以上才有完整协同，1 支形同虚设
    if (f->fleets.size() >= 3) mult += Fixed::pct(5);
    return mult;
}

void formationPhase(GameState& st) {
    for (auto& e : st.empires) {
        if (!e.alive) continue;
        for (auto& f : e.formations) {
            // 清理已被摧毁的舰队
            f.fleets.erase(std::remove_if(f.fleets.begin(), f.fleets.end(),
                                          [&](u32 fid) {
                                              const Fleet* fl = st.fleet(fid);
                                              return fl == nullptr || fl->owner != e.id;
                                          }),
                           f.fleets.end());
            // 协同度：同集团军内有多支舰队同处一个星系时上升，否则缓慢衰减
            bool together = false;
            if (f.fleets.size() >= 2) {
                const Fleet* first = st.fleet(f.fleets.front());
                if (first != nullptr) {
                    together = true;
                    for (u32 fid : f.fleets) {
                        const Fleet* fl = st.fleet(fid);
                        if (fl == nullptr || fl->system != first->system) together = false;
                    }
                }
            }
            if (together)
                f.coordination = fxClamp(f.coordination + Fixed::pct(3), Fixed(0), Fixed(1));
            else
                f.coordination = fxClamp(f.coordination - Fixed::pct(1), Fixed(0), Fixed(1));
        }
    }
}

std::string rulerReport(const GameState& st, u32 empire) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return "非法主体\n";
    std::string out;
    TextTable t;
    t.header({"项目", "值"});
    t.row({"领袖", e->ruler.name});
    t.row({"出身", std::string(rulerTraitName(e->ruler.trait))});
    t.row({"能力", fixedStrPlain(e->ruler.skill * Fixed(100), 0) + "%"});
    t.row({"年龄", std::to_string(e->ruler.age)});
    t.row({"在位", std::to_string(st.tick - e->ruler.reignStart) + " 季"});
    if (e->ruler.elected)
        t.row({"任期至", std::to_string(e->ruler.termEnd) + " 季（到期改选）"});
    else
        t.row({"任期", "终身制"});
    out += t.render();
    out += "\n  " + rulerTraitDesc(e->ruler.trait) + "\n";
    return out;
}

std::string scientistReport(const GameState& st, u32 empire) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return "非法主体\n";
    if (e->scientists.empty()) {
        return "  （科研部暂无科学家。用 `greyfall personnel --recruit` 招募）\n";
    }
    std::string out;
    TextTable t;
    t.header({"#", "姓名", "专长", "能力", "分管分支", "完成"});
    for (const auto& s : e->scientists) {
        std::string branch = (s.assignedBranch == 0xFF)
                                 ? "（空闲）"
                                 : std::string(techBranchName(static_cast<TechBranch>(s.assignedBranch)));
        t.row({std::to_string(s.id), s.name, std::string(scientistFieldName(s.field)),
               fixedStrPlain(s.skill * Fixed(100), 0) + "%", branch,
               std::to_string(s.projectsCompleted)});
    }
    out += t.render();
    // 展示当前加成
    for (int b = 0; b < kTechBranchCount; ++b) {
        Fixed bonus = scientistBonus(st, empire, b);
        if (bonus.rawValue() <= 0) continue;
        (void)st;
        out += "  " + std::string(techBranchName(static_cast<TechBranch>(b))) + " 分支立项速度 +" +
               fixedStrPlain(bonus * Fixed(100), 0) + "%\n";
    }
    return out;
}

std::string formationReport(const GameState& st, u32 empire) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return "非法主体\n";
    if (e->formations.empty()) {
        return "  （尚未编成集团军。用 `greyfall personnel --form \"名称\"` 新建）\n";
    }
    std::string out;
    TextTable t;
    t.header({"#", "集团军", "舰队", "司令", "协同", "战力倍率"});
    for (const auto& f : e->formations) {
        const Commander* c = st.commander(f.commander);
        Fixed mult = f.fleets.empty() ? Fixed(1) : formationPowerMultiplier(st, f.fleets.front());
        t.row({std::to_string(f.id), f.name, std::to_string(f.fleets.size()),
               c ? c->name : std::string("（未任命）"),
               fixedStrPlain(f.coordination * Fixed(100), 0) + "%",
               "×" + fixedStrPlain(mult, 2)});
    }
    out += t.render();
    out += "  编队 3 支以上且同处一个星系时协同度上升（最高 +15%）；\n";
    out += "  司令的攻防与规划提供额外加成；单打独斗的舰队没有这些。\n";
    return out;
}

}  // namespace gf
