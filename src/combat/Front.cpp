#include "combat/Front.h"

#include <algorithm>

#include "util/TextTable.h"
#include "combat/Resolver.h"
#include "domain/Treaty.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {
namespace {

/// 某帝国在某星系可投入的战力（不含组织度归零的舰队）
Fixed powerInSystem(const GameState& st, u32 owner, u32 system) {
    Fixed acc = Fixed(0);
    for (const auto& f : st.fleets) {
        if (f.owner != owner) continue;
        if (f.system != system) continue;
        if (f.org.rawValue() <= 0) continue;
        acc += fleetPower(st, f.id);
    }
    return acc;
}

/// 闲置（可被前线自动分配）的舰队
bool isIdleForFront(const Fleet& f) {
    if (f.org.rawValue() <= 0) return false;
    if (f.battle != 0xFFFFFFFFu) return false;
    return f.order == FleetOrder::Idle || f.order == FleetOrder::Patrol;
}

}  // namespace

Front computeFront(const GameState& st, u32 empire, u32 enemy) {
    Front fr;
    fr.enemy = enemy;
    const Empire* e = st.empire(empire);
    const Empire* en = st.empire(enemy);
    if (e == nullptr || en == nullptr) return fr;
    fr.enemyName = en->name;

    // ---- A) 接触扇区：敌方舰队与我方舰队同处一个星系 ----
    // 这类扇区不属于「接壤边界」，但它是实际交战的所在，
    // 若不计入前线，玩家在自家星系被入侵时会看不到任何战线。
    {
        std::vector<u32> contact;
        for (const auto& f : st.fleets) {
            if (f.owner != empire) continue;
            const SystemNode* s = st.system(f.system);
            if (s == nullptr) continue;
            // 该星系里有敌人吗？
            bool enemyHere = false;
            for (const auto& g : st.fleets) {
                if (g.owner != enemy) continue;
                if (g.system != f.system) continue;
                enemyHere = true;
                break;
            }
            // 或者星系本身归敌方所有
            if (s->owner == enemy) enemyHere = true;
            if (!enemyHere) continue;
            contact.push_back(f.system);
        }
        std::sort(contact.begin(), contact.end());
        contact.erase(std::unique(contact.begin(), contact.end()), contact.end());
        for (u32 sysId : contact) {
            FrontSector sec;
            sec.system = sysId;
            sec.fromSystem = sysId;
            sec.owner = enemy;
            sec.friendlyPower = powerInSystem(st, empire, sysId);
            // 相邻我方星系可支援
            const SystemNode* cs = st.system(sysId);
            if (cs != nullptr) {
                for (u32 back : cs->links) {
                    const SystemNode* bs = st.system(back);
                    if (bs == nullptr || bs->owner != empire) continue;
                    sec.friendlyPower += powerInSystem(st, empire, back) / Fixed(2);
                }
            }
            sec.enemyPower = powerInSystem(st, enemy, sysId);
            Fixed fort = systemDefense(st, sysId) - sec.enemyPower;
            if (fort.rawValue() > 0) sec.enemyPower += fort;
            sec.ratio = (sec.enemyPower.rawValue() > 0) ? sec.friendlyPower / sec.enemyPower : Fixed(3);
            sec.terrain = terrainNameOf(st, sysId);
            for (const auto& b : st.battles) {
                if (!b.resolved && b.system == sysId) sec.contested = true;
            }
            fr.sectors.push_back(std::move(sec));
        }
    }

    // ---- B) 接壤扇区：我方星系与敌方领土之间的边界 ----
    for (u32 mine : e->systems) {
        const SystemNode* ms = st.system(mine);
        if (ms == nullptr) continue;
        Fixed myPowerHere = powerInSystem(st, empire, mine);
        for (u32 link : ms->links) {
            const SystemNode* ls = st.system(link);
            if (ls == nullptr) continue;
            if (ls->owner != enemy) continue;

            FrontSector sec;
            sec.system = link;
            sec.fromSystem = mine;
            sec.owner = enemy;
            // 该扇区我方可用兵力 = 出发星系 + 相邻我方星系的兵力
            sec.friendlyPower = myPowerHere;
            for (u32 back : ls->links) {
                const SystemNode* bs = st.system(back);
                if (bs == nullptr) continue;
                if (bs->owner != empire) continue;
                if (back == mine) continue;
                sec.friendlyPower += powerInSystem(st, empire, back);
            }
            // 敌方守备 = 该星系舰队 + 要塞 + 地形
            sec.enemyPower = powerInSystem(st, enemy, link);
            Fixed fort = systemDefense(st, link) - sec.enemyPower;
            if (fort.rawValue() > 0) sec.enemyPower += fort;
            sec.ratio = (sec.enemyPower.rawValue() > 0)
                            ? sec.friendlyPower / sec.enemyPower
                            : Fixed(3);
            sec.terrain = terrainNameOf(st, link);
            for (const auto& b : st.battles) {
                if (b.resolved) continue;
                if (b.system != link) continue;
                sec.contested = true;
            }
            fr.sectors.push_back(std::move(sec));
        }
    }

    // 去重：同一个敌方星系可能从多个出发星系可达，保留战力最高的那条
    std::sort(fr.sectors.begin(), fr.sectors.end(), [](const FrontSector& a, const FrontSector& b) {
        if (a.system != b.system) return a.system < b.system;
        return a.friendlyPower.rawValue() > b.friendlyPower.rawValue();
    });
    fr.sectors.erase(std::unique(fr.sectors.begin(), fr.sectors.end(),
                                 [](const FrontSector& a, const FrontSector& b) { return a.system == b.system; }),
                     fr.sectors.end());

    for (const auto& s : fr.sectors) {
        fr.friendlyTotal += s.friendlyPower;
        fr.enemyTotal += s.enemyPower;
    }
    fr.overallRatio = (fr.enemyTotal.rawValue() > 0) ? fr.friendlyTotal / fr.enemyTotal : Fixed(3);

    // 推荐进攻扇区 = 战力比最高者（且我方确有兵力）
    Fixed best = Fixed(-1);
    for (const auto& s : fr.sectors) {
        if (s.friendlyPower.rawValue() <= 0) continue;
        if (s.ratio.rawValue() > best.rawValue()) {
            best = s.ratio;
            fr.recommendedSector = s.system;
        }
    }

    // 规划进攻轴：让 UI 与 AI 看到同一份方案。
    // 可用战力取该帝国全部舰队战力（含正在别处作战的，作为上界估计）。
    {
        Fixed avail = Fixed(0);
        for (u32 fid : e->fleets) {
            const Fleet* f = st.fleet(fid);
            if (f != nullptr && f->org.rawValue() > 0) avail += f->strength;
        }
        planFrontAxes(st, empire, fr, avail);
    }

    // 姿态建议
    if (fr.sectors.empty()) fr.posture = "无接壤（需先经航线接近）";
    else if (fr.overallRatio.rawValue() >= Fixed(2).rawValue()) fr.posture = "全面优势：可多路推进";
    else if (fr.overallRatio.rawValue() >= Fixed::raw(1200).rawValue()) fr.posture = "局部优势：建议单点突破";
    else if (fr.overallRatio.rawValue() >= Fixed::raw(800).rawValue()) fr.posture = "均势：宜固守待援";
    else fr.posture = "劣势：建议转入防御或求和";
    return fr;
}

void planFrontAxes(const GameState& st, u32 empire, Front& fr, Fixed availablePower) {
    fr.axes.clear();
    fr.axisCount = 0;
    fr.multiAxis = false;
    (void)st;
    (void)empire;
    if (fr.sectors.empty()) return;

    // 候选轴：按战力比从高到低排序；ratio ≥ 1.2 才算「具备独立突破能力」
    struct Cand {
        const FrontSector* s;
    };
    std::vector<const FrontSector*> cands;
    for (const auto& s : fr.sectors) cands.push_back(&s);
    std::sort(cands.begin(), cands.end(), [](const FrontSector* a, const FrontSector* b) {
        if (a->ratio.rawValue() != b->ratio.rawValue()) return a->ratio.rawValue() > b->ratio.rawValue();
        return a->system < b->system;
    });

    // 计划投入的战力：留出 20% 作为预备队
    Fixed budget = availablePower * Fixed::pct(80);
    // 单条轴需要的战力 = 敌方守备 × 1.2（确保能形成突破而非僵持）
    auto needOf = [](const FrontSector& s) { return s.enemyPower * Fixed::raw(1200); };

    Fixed spent = Fixed(0);
    for (const FrontSector* c : cands) {
        if (c->enemyPower.rawValue() <= 0) continue;   // 无设防目标不需要「突破」
        Fixed need = needOf(*c);
        // 已有兵力可抵扣需求
        Fixed already = c->friendlyPower;
        Fixed extra = need - already;
        if (extra.rawValue() < 0) extra = Fixed(0);
        if (spent.rawValue() + extra.rawValue() > budget.rawValue()) continue;
        FrontAxis ax;
        ax.system = c->system;
        ax.ratio = c->ratio;
        ax.required = need;
        ax.assigned = already;
        ax.viable = c->ratio.rawValue() >= Fixed::raw(1200).rawValue();
        fr.axes.push_back(ax);
        spent += extra;
        // 轴数上限必须与姿态文字一致：
        // 只有「全面优势」（ratio ≥ 2.0）才分兵多路，否则集中兵力单点突破。
        // 早期门槛放到 1.2，导致文字说「建议单点突破」而规划却选了 2 条轴。
        int cap = 1;
        if (fr.overallRatio.rawValue() >= Fixed(5).rawValue()) cap = 4;
        else if (fr.overallRatio.rawValue() >= Fixed(3).rawValue()) cap = 3;
        else if (fr.overallRatio.rawValue() >= Fixed(2).rawValue()) cap = 2;
        if (static_cast<int>(fr.axes.size()) >= cap) break;
    }
    fr.axisCount = static_cast<int>(fr.axes.size());
    // 只有「多且都可行」才算多路突破；否则退化为单点主攻
    int viable = 0;
    for (const auto& a : fr.axes)
        if (a.viable) ++viable;
    fr.multiAxis = fr.axisCount > 1 && viable >= 2;
    if (!fr.multiAxis && !fr.axes.empty()) {
        // 退化为单点：只保留战力比最高的一条
        FrontAxis best = fr.axes.front();
        fr.axes.clear();
        fr.axes.push_back(best);
        fr.axisCount = 1;
    }
}

std::vector<Front> allFronts(const GameState& st, u32 empire) {
    std::vector<Front> out;
    for (const auto& other : st.empires) {
        if (other.id == empire || !other.alive) continue;
        if (!atWarWith(st, empire, other.id)) continue;
        Front f = computeFront(st, empire, other.id);
        out.push_back(std::move(f));
    }
    return out;
}

void assignFronts(GameState& st) {
    // ---- 0) 防御集结：本土星系被攻击时，闲置舰队必须回援 ----
    // 缺少这一步时 AI 会坐视领土沦陷：实测玩家把舰队停在 AI 星系上
    // 连打 40 季，AI 的 4 支舰队全程既不在场也不赶来，
    // 星系被单方面磨掉 —— 这是「开局零伤亡打全图」的直接原因。
    for (const auto& emp : st.empires) {
        if (!emp.alive) continue;
        // 找出「有敌舰队驻留」或「刚被夺走」的本土邻近星系
        std::vector<u32> threatened;
        for (u32 sid : emp.systems) {
            const SystemNode* s = st.system(sid);
            if (s == nullptr) continue;
            bool danger = false;
            for (const auto& f : st.fleets) {
                if (f.owner == emp.id) continue;
                if (f.strength.rawValue() <= 0) continue;
                if (!atWarWith(st, emp.id, f.owner)) continue;
                // 敌舰队就在本星系，或停在相邻星系（下一季即可抵达）
                if (f.system == sid) danger = true;
                else {
                    const SystemNode* fs = st.system(f.system);
                    if (fs != nullptr)
                        for (u32 nx : fs->links)
                            if (nx == sid) danger = true;
                }
                if (danger) break;
            }
            if (danger) threatened.push_back(sid);
        }
        if (threatened.empty()) continue;
        // 把舰队派往最近的受威胁星系。
        // **必须覆盖进攻命令**：AI 的 Invade 行动在战斗阶段之前就把舰队设为
        // Engage 并派往敌方腹地，若这里只挑「闲置」舰队，回防永远轮不到它们
        //（实测 AI 的 4 支舰队全程在打别人，本土被磨掉也不回援）。
        // 但保留正在交战的舰队 —— 把它们从战场拉走会造成战线崩解。
        for (u32 fid : emp.fleets) {
            Fleet* f = st.fleet(fid);
            if (f == nullptr) continue;
            if (f->battle != 0xFFFFFFFFu) continue;          // 正在交战：不动
            if (f->system == kNoSystem) continue;
            u32 best = kNoSystem;
            int bestHops = 1 << 30;
            for (u32 sid : threatened) {
                if (f->system == sid) {
                    best = sid;
                    bestHops = 0;
                    break;
                }
                int h = st.map.hops(f->system, sid);
                if (h >= 0 && h < bestHops) {
                    bestHops = h;
                    best = sid;
                }
            }
            if (best == kNoSystem) continue;
            if (f->system == best) {
                // 已在战场：转为交战姿态，不再被后续的进攻分配调走
                f->order = FleetOrder::Engage;
                f->targetSystem = kNoSystem;
                continue;
            }
            f->targetSystem = best;
            f->order = FleetOrder::Move;
        }
    }

    for (const auto& emp : st.empires) {
        if (!emp.alive) continue;
        std::vector<Front> fronts = allFronts(st, emp.id);
        if (fronts.empty()) continue;

        // 收集可分配的闲置舰队
        std::vector<u32> idle;
        for (u32 fid : emp.fleets) {
            const Fleet* f = st.fleet(fid);
            if (f == nullptr) continue;
            if (!isIdleForFront(*f)) continue;
            idle.push_back(fid);
        }
        if (idle.empty()) continue;

        // 先为每条前线规划进攻轴：优势足够时多路突破，否则集中单点。
        // 早期没有这一步，「全面优势：可多路推进」只是提示文字，
        // 实际分配永远把兵力堆到一个扇区，优势无法转化为推进速度。
        Fixed totalIdlePower = Fixed(0);
        for (u32 fid : idle) {
            const Fleet* f = st.fleet(fid);
            if (f != nullptr) totalIdlePower += f->strength;
        }
        // 所有前线的既有战力也算入可用预算
        Fixed existingPower = Fixed(0);
        for (const auto& fr2 : fronts)
            for (const auto& s2 : fr2.sectors) existingPower += s2.friendlyPower;
        for (auto& fr2 : fronts) planFrontAxes(st, emp.id, fr2, totalIdlePower + existingPower);

        // 按轴线分配：多路突破时各轴依次补兵，单点主攻时只补主攻轴
        struct Slot {
            u32 system;
            Fixed need;      // 需要的战力（敌方守备 × 1.2 − 已有）
            bool axis;
        };
        std::vector<Slot> slots;
        for (const auto& fr : fronts) {
            for (const auto& ax : fr.axes) {
                const FrontSector* sec = nullptr;
                for (const auto& s : fr.sectors)
                    if (s.system == ax.system) sec = &s;
                if (sec == nullptr) continue;
                Fixed need = ax.required - sec->friendlyPower;
                if (need.rawValue() < 0) need = Fixed(0);
                slots.push_back({ax.system, need, true});
            }
            // 未被选为轴线的扇区只做最低守备（不主动增兵）
            for (const auto& s : fr.sectors) {
                bool isAxis = false;
                for (const auto& ax : fr.axes)
                    if (ax.system == s.system) isAxis = true;
                if (isAxis) continue;
                Fixed want = s.enemyPower * Fixed::pct(80);
                Fixed need = want - s.friendlyPower;
                if (need.rawValue() < 0) need = Fixed(0);
                if (need.rawValue() > 0) slots.push_back({s.system, need, false});
            }
        }
        // 轴线优先（true 排前），同类型按需求降序
        std::sort(slots.begin(), slots.end(), [](const Slot& a, const Slot& b) {
            if (a.axis != b.axis) return a.axis;
            if (a.need.rawValue() != b.need.rawValue()) return a.need.rawValue() > b.need.rawValue();
            return a.system < b.system;
        });
        if (slots.empty()) continue;
        // 需求高者优先（更大的突破机会），同需求按星系 id 保证确定性
        std::sort(slots.begin(), slots.end(), [](const Slot& a, const Slot& b) {
            if (a.need.rawValue() != b.need.rawValue()) return a.need.rawValue() > b.need.rawValue();
            return a.system < b.system;
        });

        // 姿态判断：整体劣势时只投入一部分舰队（保留战略预备队），
        // 避免把全部家当送进一场必败的进攻。
        Fixed overallRatio = Fixed(0);
        {
            Fixed mine = Fixed(0), theirs = Fixed(0);
            for (const auto& fr2 : fronts) {
                mine += fr2.friendlyTotal;
                theirs += fr2.enemyTotal;
            }
            overallRatio = (theirs.rawValue() > 0) ? mine / theirs : Fixed(3);
        }
        std::size_t commitLimit = idle.size();
        if (overallRatio.rawValue() < Fixed::raw(800).rawValue()) {
            // 明显劣势：只投入 1/3 用于防守要道
            commitLimit = std::max<std::size_t>(1, idle.size() / 3);
        } else if (overallRatio.rawValue() < Fixed::raw(1200).rawValue()) {
            // 均势：投入 2/3
            commitLimit = std::max<std::size_t>(1, idle.size() * 2 / 3);
        }
        std::size_t committed = 0;

        // 逐个舰队派往最需要的扇区
        for (u32 fid : idle) {
            Fleet* f = st.fleet(fid);
            if (f == nullptr) continue;
            if (slots.empty()) break;
            if (committed >= commitLimit) break;
            ++committed;
            // 当前已有足够兵力的扇区不再堆兵（避免全部挤在一个星系）
            u32 target = slots.front().system;
            Fixed myPower = f->strength;
            slots.front().need = slots.front().need - myPower;
            if (slots.front().need.rawValue() <= 0) {
                slots.erase(slots.begin());
            } else {
                std::sort(slots.begin(), slots.end(), [](const Slot& a, const Slot& b) {
                    if (a.need.rawValue() != b.need.rawValue()) return a.need.rawValue() > b.need.rawValue();
                    return a.system < b.system;
                });
            }
            if (f->system == target) {
                f->order = FleetOrder::Engage;
            } else {
                f->targetSystem = target;
                f->order = FleetOrder::Move;
            }
        }
    }
}

std::string frontsText(const GameState& st, u32 empireId) {
    const Empire* e = st.empire(empireId);
    if (e == nullptr) return "非法主体\n";
    std::vector<Front> fronts = allFronts(st, empireId);
    if (fronts.empty()) {
        return "  （当前没有与其他阵营的战线 —— 你并未处于战争状态）\n";
    }
    std::string out;
    for (const auto& fr : fronts) {
        out += "\n" + style("═══ 对 " + fr.enemyName + " 的前线 ═══", Style::Heading) + "\n";
        out += "  我方总战力 " + fixedStr(fr.friendlyTotal, 0) + "   敌方总守备 " +
               fixedStr(fr.enemyTotal, 0) + "   战力比 " + fixedStrPlain(fr.overallRatio, 2) + "\n";
        out += "  姿态：" + fr.posture + "\n";
        if (fr.sectors.empty()) continue;
        TextTable t;
        t.header({"目标星系", "出发自", "地形", "我方战力", "敌方守备", "比", "状态"},
                 {Align::Left, Align::Left, Align::Left, Align::Right, Align::Right, Align::Right, Align::Left});
        for (const auto& s : fr.sectors) {
            const SystemNode* ts = st.system(s.system);
            const SystemNode* fs = st.system(s.fromSystem);
            std::string mark;
            if (s.system == fr.recommendedSector) mark = style("★ 建议主攻", Style::Good);
            if (s.contested) mark = style("⚔ 交战中", Style::Warn);
            t.row({(ts ? ts->name : "?") + " #" + std::to_string(s.system), (fs ? fs->name : "?"),
                   s.terrain, fixedStr(s.friendlyPower, 0), fixedStr(s.enemyPower, 0),
                   fixedStrPlain(s.ratio, 2), mark});
        }
        out += t.render();
        // 多路突破方案
        if (!fr.axes.empty()) {
            if (fr.multiAxis) {
                out += "  " + style("多路突破：" + std::to_string(fr.axisCount) + " 条进攻轴同时推进",
                                    Style::Good) + "\n";
            } else {
                out += "  " + style("单点主攻（优势不足以分兵）", Style::Warn) + "\n";
            }
            for (const auto& ax : fr.axes) {
                const SystemNode* ts = st.system(ax.system);
                out += "    · " + padRight(ts ? ts->name : "?", 12) + " 需战力 " +
                       padLeft(fixedStr(ax.required, 0), 7) + "  现有 " + padLeft(fixedStr(ax.assigned, 0), 7) +
                       (ax.viable ? style("  可独立突破", Style::Good) : "") + "\n";
            }
        }
        out += "\n";
    }
    out += "\n" + style("说明", Style::Sub) + "\n";
    out += "  · 每个「扇区」是一个可进攻的敌方星系；相邻的我方星系兵力可协同。\n";
    out += "  · 每季系统会把**闲置**舰队（命令为待命/巡逻）自动派往最需要兵力的扇区。\n";
    out += "  · 想让某支舰队固定在某处，把它设为 blockade/escort/retreat 即可不参与自动分配。\n";
    out += "  · 手动指定：greyfall fleet --id <n> --order move --to <星系>\n";
    return out;
}

std::string frontDetailText(const GameState& st, u32 empireId, u32 enemyId) {
    const Empire* e = st.empire(empireId);
    const Empire* en = st.empire(enemyId);
    if (e == nullptr || en == nullptr) return "非法主体\n";
    if (!atWarWith(st, empireId, enemyId)) {
        return "你与 " + en->name + " 并未处于战争状态。\n";
    }
    Front fr = computeFront(st, empireId, enemyId);
    std::string out;
    out += style("═══ 对 " + en->name + " 的战线分析 ═══", Style::Heading) + "\n";
    out += "  接壤扇区：" + std::to_string(fr.sectors.size()) + " 个\n";
    out += "  我方总战力 " + fixedStr(fr.friendlyTotal, 0) + "   敌方总守备 " + fixedStr(fr.enemyTotal, 0) +
           "\n";
    out += "  战力比 " + fixedStrPlain(fr.overallRatio, 2) + "   姿态：" + fr.posture + "\n";
    if (fr.recommendedSector != kNoSystem) {
        const SystemNode* rs = st.system(fr.recommendedSector);
        out += "  建议主攻：" + std::string(rs ? rs->name : "?") + " #" +
               std::to_string(fr.recommendedSector) + "（战力比最高）\n";
    }
    // 敌方后方的薄弱点（非接壤但守备低）
    out += "\n  敌方纵深薄弱星系（守备最低的 5 个）：\n";
    std::vector<std::pair<Fixed, u32>> deep;
    for (u32 sys : en->systems) {
        // 纵深守备 = 该星系舰队 + 要塞
        deep.emplace_back(systemDefense(st, sys), sys);
    }
    std::sort(deep.begin(), deep.end(), [](const auto& a, const auto& b) {
        if (a.first.rawValue() != b.first.rawValue()) return a.first.rawValue() < b.first.rawValue();
        return a.second < b.second;
    });
    for (std::size_t i = 0; i < deep.size() && i < 5; ++i) {
        const SystemNode* s = st.system(deep[i].second);
        out += "    · " + std::string(s ? s->name : "?") + " #" + std::to_string(deep[i].second) + "  守备 " +
               fixedStr(systemDefense(st, deep[i].second), 0) + "  " +
               terrainNameOf(st, deep[i].second) + "\n";
    }
    return out;
}

}  // namespace gf
