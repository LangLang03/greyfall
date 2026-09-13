#include "domain/Peace.h"

#include <algorithm>

#include "cli/TextTable.h"
#include "core/GameState.h"
#include "domain/Empire.h"
#include "domain/Planet.h"
#include "domain/Treaty.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {
namespace {

/// 战争分数的「有限胜利」门槛：达到即可召开和平会议
constexpr i64 kPeaceScoreThreshold = 3;

/// 战争疲劳上限（季）。任何战争拖过这个长度都会被强制送进和平会议。
///
/// 为什么必须有这条：原先 `shouldConvenePeace` 只看战争分数与首都失守 ——
/// 当双方舰队互相僵持、谁也刷不到 3 分时，战争**永不结束**。实测玩家与
/// 霜羽殖民地联盟从 t=17 打到 t=52，对方每季重复入侵同一星系，
/// 战争疲劳飙到 71%，玩家必然被磨死且没有任何停战通道。
/// 有了这条上限，任何战争的持续时间都有硬边界（≤ 20 季）。
constexpr u64 kMaxWarQuarters = 20;

/// 涉及玩家的和平会议在被自动清算前可挂起的季数。
/// 玩家不回应时，和平按占优方的意志执行 —— 与 AI 之间的战争同口径。
constexpr u64 kAutoConcludeQuarters = 8;

/// 由占优方自动填写和平要求：优先割让价值最高的星系，其余分数换赔款。
///
/// 提取成函数是因为「AI 之间的战争」与「玩家久不回应的战争」必须走同一条路径 ——
/// 旧实现只给前者写了这段逻辑，后者只能永远挂着。
void autoFillDemands(GameState& st, u32 winner, u32 loser) {
    PeaceConference* c = nullptr;
    for (auto& x : st.peace)
        if (x.active && x.winner == winner && x.loser == loser) c = &x;
    if (c == nullptr) return;
    const Empire* l = st.empire(loser);
    if (l == nullptr) return;
    std::vector<u32> cands = l->systems;
    std::sort(cands.begin(), cands.end(), [&](u32 x, u32 y) {
        return annexCost(st, x).rawValue() > annexCost(st, y).rawValue();
    });
    for (u32 sys : cands) {
        PeaceDemand d = makeAnnex(st, sys);
        if (d.cost.rawValue() > (c->warScore - c->spent).rawValue()) continue;
        (void)addDemand(st, winner, d, nullptr);
        if (c->spent.rawValue() >= c->warScore.rawValue() * 0.8) break;
    }
    Fixed left = c->warScore - c->spent;
    if (left.rawValue() >= 1) {
        PeaceDemand d = makeReparations(left * Fixed(1000));
        (void)addDemand(st, winner, d, nullptr);
    }
}

}  // namespace

std::string_view peaceDemandName(PeaceDemandKind k) {
    switch (k) {
        case PeaceDemandKind::AnnexSystem: return "割让星系";
        case PeaceDemandKind::Reparations: return "战争赔款";
        case PeaceDemandKind::TechTransfer: return "技术转移";
        case PeaceDemandKind::Manpower: return "人力征调";
        case PeaceDemandKind::DisarmFleet: return "解除武装";
        case PeaceDemandKind::Vassalize: return "附庸";
        case PeaceDemandKind::Count: break;
    }
    return "?";
}

Fixed annexCost(const GameState& st, u32 system) {
    const SystemNode* s = st.system(system);
    if (s == nullptr) return Fixed(0);
    // 代价必须与「一场战争能挣到多少分数」匹配：
    // 战争分数主要来自赢得战斗（每场 +1）与占领星系（每个 +3），
    // 一场中等规模的战争大约能挣 4~12 分。
    // 早期把普通星系定价到 9 分以上，导致胜方几乎什么都拿不到。
    Fixed cost = Fixed(2);
    for (u32 pid : s->planets) {
        const Planet* p = st.planet(pid);
        if (p == nullptr) continue;
        // 行星越多、人口越密、开发越高 ⇒ 越贵（但增幅温和）
        cost += Fixed::raw(500) + Fixed(p->pops) / Fixed(5000) + p->development / Fixed(5);
    }
    if (s->capital) cost = cost * Fixed(2);
    if (s->megastructure) cost = cost * Fixed::raw(1500);
    // 至少 1 分，避免免费
    return cost.rawValue() > Fixed(1).rawValue() ? cost : Fixed(1);
}

Fixed reparationsCost(Fixed credits) {
    // 每 1000 cr 折 1 点分数
    Fixed c = credits / Fixed(1000);
    return c.rawValue() > 0 ? c : Fixed(1);
}

Fixed techTransferCost() { return Fixed(2); }
Fixed manpowerCost(Fixed thousands) {
    Fixed c = thousands / Fixed(200);
    return c.rawValue() > 0 ? c : Fixed(1);
}
Fixed vassalizeCost() { return Fixed(50); }

const PeaceConference* findConference(const GameState& st, u32 a, u32 b) {
    for (const auto& c : st.peace) {
        if (!c.active) continue;
        if ((c.winner == a && c.loser == b) || (c.winner == b && c.loser == a)) return &c;
    }
    return nullptr;
}

Fixed peaceSpent(const GameState& st, u32 empire) {
    for (const auto& c : st.peace) {
        if (!c.active || c.winner != empire) continue;
        return c.spent;
    }
    return Fixed(0);
}

Fixed peaceRemaining(const GameState& st, u32 empire) {
    for (const auto& c : st.peace) {
        if (!c.active || c.winner != empire) continue;
        return c.warScore - c.spent;
    }
    return Fixed(0);
}

bool shouldConvenePeace(const GameState& st, u32 a, u32 b) {
    if (a == b) return false;
    if (!atWarWith(st, a, b)) return false;
    const Relation& rel = st.relation(a, b);
    // 任一方向的战争分数达到门槛
    if (rel.warScore >= kPeaceScoreThreshold) return true;
    const Relation& rev = st.relation(b, a);
    if (rev.warScore >= kPeaceScoreThreshold) return true;
    // 首都失守 ⇒ 无条件
    const Empire* ea = st.empire(a);
    const Empire* eb = st.empire(b);
    if (ea != nullptr && st.system(ea->capital) != nullptr && st.system(ea->capital)->owner == b) return true;
    if (eb != nullptr && st.system(eb->capital) != nullptr && st.system(eb->capital)->owner == a) return true;
    // 战争疲劳上限 ⇒ 无条件（保证任何战争都有终点）。
    // warStartTick 存的是「开战 tick + 1」（0 保留给「未开战」），此处减回。
    if (rel.warStartTick != 0 && st.tick >= rel.warStartTick - 1 + kMaxWarQuarters) return true;
    if (rev.warStartTick != 0 && st.tick >= rev.warStartTick - 1 + kMaxWarQuarters) return true;
    // 一方已被打到「无舰队且无星系」⇒ 立即清算，不必再拖
    if (ea != nullptr && ea->alive && ea->fleets.empty() && ea->systems.empty()) return true;
    if (eb != nullptr && eb->alive && eb->fleets.empty() && eb->systems.empty()) return true;
    return false;
}

bool convenePeace(GameState& st, u32 winner, u32 loser, std::string* err) {
    Empire* w = st.empire(winner);
    Empire* l = st.empire(loser);
    if (w == nullptr || l == nullptr) {
        if (err) *err = "非法主体";
        return false;
    }
    if (!atWarWith(st, winner, loser)) {
        if (err) *err = "双方并未处于战争状态";
        return false;
    }
    if (findConference(st, winner, loser) != nullptr) {
        if (err) *err = "与对方的和平会议已在召开中";
        return false;
    }
    PeaceConference c;
    c.active = true;
    c.winner = winner;
    c.loser = loser;
    c.startTick = static_cast<u32>(st.tick);
    // 战争分数：取该方向累计的分数，并折算为可用的会议分数
    const Relation& rel = st.relation(winner, loser);
    Fixed score = Fixed(static_cast<i64>(rel.warScore));
    // 首都失守 ⇒ 无条件接受，并给予大量分数
    bool capFallen = false;
    if (st.system(l->capital) != nullptr && st.system(l->capital)->owner == winner) capFallen = true;
    if (capFallen) {
        score = fxMax(score, Fixed(20)) * Fixed::raw(1500);
        c.unconditional = true;
    }
    c.warScore = fxMax(score, Fixed(1));
    st.peace.push_back(std::move(c));
    st.logEvent(LogPhase::Combat, kLogWar,
                "【和平会议】" + w->name + " 与 " + l->name + " 开始清算战争（可用分数 " +
                    fixedStr(st.peace.back().warScore, 0) + (capFallen ? "，首都已失守：无条件接受" : "") + "）",
                winner);
    return true;
}

PeaceDemand makeAnnex(const GameState& st, u32 system) {
    PeaceDemand d;
    d.kind = PeaceDemandKind::AnnexSystem;
    d.target = system;
    d.cost = annexCost(st, system);
    const SystemNode* s = st.system(system);
    d.text = "割让 " + std::string(s ? s->name : "?");
    return d;
}

PeaceDemand makeReparations(Fixed credits) {
    PeaceDemand d;
    d.kind = PeaceDemandKind::Reparations;
    d.amount = credits;
    d.cost = reparationsCost(credits);
    d.text = "赔款 " + fixedStr(credits, 0) + " cr";
    return d;
}

PeaceDemand makeTech(u32 techId) {
    PeaceDemand d;
    d.kind = PeaceDemandKind::TechTransfer;
    d.target = techId;
    d.cost = techTransferCost();
    d.text = "技术转移";
    return d;
}

PeaceDemand makeManpower(Fixed thousands) {
    PeaceDemand d;
    d.kind = PeaceDemandKind::Manpower;
    d.amount = thousands;
    d.cost = manpowerCost(thousands);
    d.text = "征调人力 " + fixedStr(thousands, 0) + "k";
    return d;
}

bool addDemand(GameState& st, u32 empire, const PeaceDemand& d, std::string* err) {
    for (auto& c : st.peace) {
        if (!c.active) continue;
        // 只有胜方可以提出要求（战败方在无条件会议中连这个资格都没有）
        if (c.winner != empire && !c.unconditional) {
            if (err) *err = "只有占优方可以提出要求";
            return false;
        }
        if (c.winner != empire && c.loser != empire) continue;
        if (c.winner != empire) {
            if (err) *err = "战败方无权提出要求";
            return false;
        }
        Fixed remaining = c.warScore - c.spent;
        if (d.cost.rawValue() > remaining.rawValue()) {
            if (err)
                *err = "战争分数不足：需要 " + fixedStr(d.cost, 0) + "，剩余 " + fixedStr(remaining, 0);
            return false;
        }
        // 割让目标必须真的属于战败方
        if (d.kind == PeaceDemandKind::AnnexSystem) {
            const SystemNode* s = st.system(d.target);
            if (s == nullptr) {
                if (err) *err = "非法星系";
                return false;
            }
            if (s->owner != c.loser) {
                if (err) *err = "该星系不属于战败方";
                return false;
            }
            // 不能重复索取
            for (const auto& x : c.demands)
                if (x.kind == PeaceDemandKind::AnnexSystem && x.target == d.target) {
                    if (err) *err = "该星系已被列入要求";
                    return false;
                }
        }
        c.demands.push_back(d);
        c.spent += d.cost;
        st.logEvent(LogPhase::Combat, kLogWar,
                    std::string(st.empire(empire) ? st.empire(empire)->name : "?") + " 在和平会议中要求：" +
                        d.text + "（消耗 " + fixedStr(d.cost, 0) + " 分）",
                    empire);
        return true;
    }
    if (err) *err = "当前没有进行中的和平会议";
    return false;
}

bool removeDemand(GameState& st, u32 empire, std::size_t index, std::string* err) {
    for (auto& c : st.peace) {
        if (!c.active || c.winner != empire) continue;
        if (index >= c.demands.size()) {
            if (err) *err = "要求编号越界";
            return false;
        }
        c.spent -= c.demands[index].cost;
        c.demands.erase(c.demands.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }
    if (err) *err = "当前没有你可主导的和平会议";
    return false;
}

bool concludePeace(GameState& st, u32 empire, std::string* err) {
    for (auto& c : st.peace) {
        if (!c.active || c.winner != empire) continue;
        Empire* w = st.empire(c.winner);
        Empire* l = st.empire(c.loser);
        if (w == nullptr || l == nullptr) {
            c.active = false;
            continue;
        }
        std::string applied;
        for (const auto& d : c.demands) {
            switch (d.kind) {
                case PeaceDemandKind::AnnexSystem: {
                    SystemNode* s = st.system(d.target);
                    if (s == nullptr || s->owner != c.loser) break;
                    s->owner = c.winner;
                    s->colonized = true;
                    l->systems.erase(std::remove(l->systems.begin(), l->systems.end(), d.target),
                                     l->systems.end());
                    if (std::find(w->systems.begin(), w->systems.end(), d.target) == w->systems.end())
                        w->systems.push_back(d.target);
                    // 行星归属随之转移
                    for (u32 pid : s->planets) {
                        Planet* p = st.planet(pid);
                        if (p != nullptr && p->owner == c.loser) p->owner = c.winner;
                    }
                    applied += s->name + "、";
                    break;
                }
                case PeaceDemandKind::Reparations: {
                    Fixed amt = fxMin(d.amount, fxMax(l->treasury, Fixed(0)));
                    l->treasury -= amt;
                    w->treasury += amt;
                    if (l->isPlayer) st.market.margin.cash = l->treasury;
                    if (w->isPlayer) st.market.margin.cash = w->treasury;
                    applied += "赔款 " + fixedStr(amt, 0) + "、";
                    break;
                }
                case PeaceDemandKind::TechTransfer: {
                    if (static_cast<int>(d.target) < kTechCount &&
                        !techCompleted(l->tech, static_cast<int>(d.target)) &&
                        !techCompleted(w->tech, static_cast<int>(d.target))) {
                        w->tech.completed.push_back(static_cast<u8>(d.target));
                        applied += "技术、";
                    }
                    break;
                }
                case PeaceDemandKind::Manpower: {
                    // 从战败方行星按比例抽调人口
                    Fixed want = d.amount;
                    for (auto& p : st.planets) {
                        if (p.owner != c.loser || want.rawValue() <= 0) continue;
                        i64 take = static_cast<i64>(want.rawValue() / FIX);
                        if (take > p.pops / 4) take = p.pops / 4;
                        if (take <= 0) continue;
                        p.pops -= take;
                        want -= Fixed(take);
                        // 加到胜方首都所在行星
                        for (auto& q : st.planets) {
                            if (q.owner == c.winner && q.capital) {
                                q.pops += take;
                                break;
                            }
                        }
                    }
                    applied += "人力、";
                    break;
                }
                case PeaceDemandKind::DisarmFleet: {
                    Fleet* f = st.fleet(d.target);
                    if (f != nullptr && f->owner == c.loser) {
                        f->strength = f->strength / Fixed(4);
                        f->org = Fixed(0);
                        applied += "解除武装、";
                    }
                    break;
                }
                case PeaceDemandKind::Vassalize: {
                    l->vassalOf = c.winner;
                    applied += "附庸、";
                    break;
                }
                case PeaceDemandKind::Count: break;
            }
        }
        // 结束战争
        declareWar(st, c.winner, c.loser, false);
        st.relation(c.winner, c.loser).opinion =
            fxClamp(st.relation(c.winner, c.loser).opinion - Fixed::pct(40), Fixed(-1), Fixed(1));
        st.relation(c.loser, c.winner).opinion =
            fxClamp(st.relation(c.loser, c.winner).opinion - Fixed::pct(40), Fixed(-1), Fixed(1));
        c.lastResult = "已执行 " + std::to_string(c.demands.size()) + " 项要求";
        c.active = false;
        st.logEvent(LogPhase::Combat, kLogWar,
                    "【和平条约】" + w->name + " 与 " + l->name + " 停战；" + w->name +
                        " 获得：" + (applied.empty() ? std::string("（无要求）") : applied),
                    c.winner);
        return true;
    }
    if (err) *err = "当前没有你可主导的和平会议";
    return false;
}

bool abandonPeace(GameState& st, u32 empire, std::string* err) {
    for (auto& c : st.peace) {
        if (!c.active || c.winner != empire) continue;
        c.active = false;
        c.lastResult = "已放弃会议（战争继续）";
        st.logEvent(LogPhase::Combat, kLogWar,
                    std::string(st.empire(empire) ? st.empire(empire)->name : "?") + " 放弃了和平会议",
                    empire);
        return true;
    }
    if (err) *err = "当前没有你可主导的和平会议";
    return false;
}

void peacePhase(GameState& st) {
    // 清理已结束的会议
    st.peace.erase(std::remove_if(st.peace.begin(), st.peace.end(),
                                  [](const PeaceConference& c) { return !c.active; }),
                   st.peace.end());

    // AI 之间的战争：自动召开并达成一份「合理」的条约
    for (std::size_t i = 0; i < st.relations.size(); ++i) {
        Relation& rel = st.relations[i];
        if (!rel.atWar) continue;
        u32 a = static_cast<u32>(i / kMaxEmpires);
        u32 b = static_cast<u32>(i % kMaxEmpires);
        if (a >= st.empires.size() || b >= st.empires.size()) continue;
        if (a == b || a > b) continue;
        const Empire* ea = st.empire(a);
        const Empire* eb = st.empire(b);
        if (ea == nullptr || eb == nullptr || !ea->alive || !eb->alive) continue;
        if (!shouldConvenePeace(st, a, b)) continue;
        // 判定谁是胜方
        u32 winner = a, loser = b;
        bool capA = st.system(ea->capital) != nullptr && st.system(ea->capital)->owner == b;
        bool capB = st.system(eb->capital) != nullptr && st.system(eb->capital)->owner == a;
        if (capA && !capB) {
            winner = b;
            loser = a;
        } else if (capB) {
            winner = a;
            loser = b;
        } else if (st.relation(b, a).warScore > st.relation(a, b).warScore) {
            winner = b;
            loser = a;
        }
        // 涉及玩家：等玩家在会议中决定，但**不能无限期等**。
        //
        // 旧实现只有 `convenePeace` 而没有期限，于是玩家不回应时会议永久 active。
        // 实测后果：玩家与某国的会议在 t=26 建立后一直挂着，`peacePhase` 里的
        // `findConference(st,a,b) == nullptr` 判定让**后续每一场战争都无法建立
        // 新会议** ⇒ 所有战争重新变成无限期，玩家被战争疲劳磨到领土归零。
        // 现在：会议超过 kAutoConcludeQuarters 季未被玩家推进，就按 AI 口径自动清算。
        if (ea->isPlayer || eb->isPlayer) {
            PeaceConference* existing = nullptr;
            for (auto& x : st.peace)
                if (x.active && ((x.winner == a && x.loser == b) || (x.winner == b && x.loser == a)))
                    existing = &x;
            if (existing == nullptr) {
                (void)convenePeace(st, winner, loser, nullptr);
                continue;
            }
            if (st.tick < existing->startTick + kAutoConcludeQuarters) continue;
            // 玩家长时间不回应 ⇒ 按占优方的意志强制清算（和平是战争的终点，不是选项）
            const u32 w = existing->winner;
            const u32 l = existing->loser;
            autoFillDemands(st, w, l);
            (void)concludePeace(st, w, nullptr);
            continue;
        }
        if (findConference(st, a, b) == nullptr) {
            if (!convenePeace(st, winner, loser, nullptr)) continue;
        }
        autoFillDemands(st, winner, loser);
        (void)concludePeace(st, winner, nullptr);
    }
}

std::string peaceText(const GameState& st, u32 empire) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return "非法主体\n";
    const PeaceConference* c = nullptr;
    for (const auto& x : st.peace)
        if (x.active && (x.winner == empire || x.loser == empire)) c = &x;
    if (c == nullptr) return "  （当前没有进行中的和平会议）\n";
    std::string out;
    out += "  交战方：" + std::string(st.empire(c->winner) ? st.empire(c->winner)->name : "?") + "（占优） vs " +
           std::string(st.empire(c->loser) ? st.empire(c->loser)->name : "?") + "\n";
    out += "  可用战争分数：" + fixedStr(c->warScore, 0) + "   已消耗：" + fixedStr(c->spent, 0) +
           "   剩余：" + fixedStr(c->warScore - c->spent, 0) + "\n";
    if (c->unconditional)
        out += "  " + style("战败方首都已失守：无条件接受任何要求", Style::Good) + "\n";
    out += "\n";
    if (c->demands.empty()) {
        out += "  （尚未提出任何要求）\n";
    } else {
        TextTable t;
        t.header({"#", "要求", "分数代价"});
        for (std::size_t i = 0; i < c->demands.size(); ++i)
            t.row({std::to_string(i), c->demands[i].text, fixedStr(c->demands[i].cost, 0)});
        out += t.render();
    }
    return out;
}

}  // namespace gf
