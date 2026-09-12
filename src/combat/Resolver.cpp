#include "combat/Front.h"
#include "combat/Resolver.h"

#include <algorithm>

#include "core/TickPipeline.h"
#include "domain/Fleet.h"
#include "domain/Personnel.h"
#include "domain/Starbase.h"
#include "gen/EmpireGen.h"
#include "mkt/OrderBook.h"
#include "rng/Streams.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {
namespace {

/// 战斗宽度上限：同时能展开的战力量级
constexpr i64 kBaseWidth = 1200;

/// 计算某方在战斗中的有效展开战力（受宽度限制与指挥修正）
Fixed effectiveWidth(const GameState& st, const Battle& b, bool attacker, u32 owner) {
    const Empire* e = st.empire(owner);
    Fixed width = Fixed(kBaseWidth);
    if (e != nullptr) {
        // 军事力量修正影响展开效率
        width = width * (Fixed(1) + empireModifier(*e, ModKind::MilitaryPower) / Fixed(2));
    }
    u32 cid = attacker ? b.attackerCommander : b.defenderCommander;
    const Commander* c = st.commander(cid);
    if (c != nullptr) {
        Fixed bonus = attacker ? (c->attack + commanderTraitAttack(*c))
                               : (c->defense + commanderTraitDefense(*c));
        // 军衔统率加成：影响展开宽度
        bonus = bonus + rankCommandBonus(c->rank);
        width = width * (Fixed(1) + bonus / Fixed(2));
    }
    // 对方诡道指挥官压缩宽度
    u32 enemyCid = attacker ? b.defenderCommander : b.attackerCommander;
    const Commander* ec = st.commander(enemyCid);
    if (ec != nullptr) width = width * (Fixed(1) + commanderTraitWidth(*ec));
    // 注意：超出宽度的部分只是「无法参战」（见 battlePhase 的 fxMin(sidePower, width)），
    // 不能再额外削弱已展开部队的战力 —— 否则堆叠兵力反而比不堆叠更弱，
    // 与「兵力优势有意义」的直觉相悖。penalty 仅用于 UI 提示。
    return fxMax(width, Fixed(50));
}

/// 某方在一个星系内可用于作战的舰队列表
void collectFleets(const GameState& st, u32 owner, u32 system, std::vector<u32>& out) {
    out.clear();
    for (const auto& f : st.fleets) {
        if (f.owner != owner) continue;
        if (f.system != system) continue;
        if (f.org.rawValue() <= 0) continue;   // 组织度归零 = 已退出战斗
        out.push_back(f.id);
    }
}

/// 一方在本场战斗中的总战力
Fixed sidePower(const GameState& st, const std::vector<u32>& fleets) {
    Fixed acc = Fixed(0);
    for (u32 id : fleets) acc += fleetPower(st, id);
    return acc;
}

}  // namespace

Fixed fleetPower(const GameState& st, u32 fleetId) {
    const Fleet* f = st.fleet(fleetId);
    if (f == nullptr) return Fixed(0);
    Fixed moraleFactor = Fixed::pct(50) + f->morale / Fixed(2);
    Fixed supplyFactor = Fixed::pct(40) + f->supply * Fixed::pct(60);
    Fixed power = f->strength * moraleFactor * supplyFactor;
    // 老练度加成
    power = power * veterancyMultiplier(veterancyLevel(f->experience));
    // 指挥官加成
    const Commander* c = st.commander(f->commander);
    if (c != nullptr) {
        power = power * (Fixed(1) + (c->attack + c->defense) / Fixed(4));
    }
    // 编队加成：编入集团军的舰队获得协同与司令加成。
    // 这是「编队/集团军」的实际价值 —— 没有它，编队只是一个标签。
    power = power * formationPowerMultiplier(st, fleetId);

    // 舰船设计加成：模块与舰体的火力/防御必须**真正参与战斗**。
    // 早期只有 fleetMaxOrg 读设计，火力与防御完全没进战斗公式 ——
    // 结果是装配模块、换装舰体、改造舰队都不影响胜负，整套设计系统只是装饰。
    // 这里以「(火力+防御) 相对该舰体基线」的比值作为倍率：
    // 装满武器模块的驱逐舰显著强于裸舰，改造为更强设计也会立刻体现在战力上。
    if (const Empire* e = st.empire(f->owner); e != nullptr) {
        const FleetDesign* d = nullptr;
        for (const auto& design : e->designs)
            if (design.id == f->design) d = &design;
        if (d != nullptr) {
            const HullInfo& hi = hullInfo(d->hull);
            Fixed baseline = hi.baseFirepower + hi.baseDefense;
            Fixed actual = d->firepower + d->defense;
            if (baseline.rawValue() > 0 && actual.rawValue() > 0)
                power = power * (actual / baseline);
        }
    }
    return power;
}

Fixed fleetMaxOrg(const GameState& st, const Fleet& f) {
    Fixed base = Fixed(100);
    const FleetDesign* d = nullptr;
    if (const Empire* e = st.empire(f.owner); e != nullptr) {
        for (const auto& design : e->designs)
            if (design.id == f.design) d = &design;
    }
    if (d != nullptr) {
        // 船体越大组织度基线越高（指挥链更完整）
        base = base + Fixed(static_cast<i64>(d->hull)) * Fixed(15);
    }
    // 老练度提升组织度上限
    base = base * (Fixed(1) + Fixed(veterancyLevel(f.experience)) * Fixed::pct(5));
    return base;
}

std::string terrainNameOf(const GameState& st, u32 system) {
    const SystemNode* sys = st.system(system);
    if (sys == nullptr) return "未知空域";
    if (sys->megastructure) return "巨构防御圈";
    if (sys->anomaly > 0) return "异常空域";
    if (sys->hazard.rawValue() > Fixed::pct(12).rawValue()) return "高辐射星云";
    if (sys->pirates.rawValue() > Fixed::pct(15).rawValue()) return "小行星带";
    if (sys->capital) return "首都防御圈";
    return "开阔空域";
}

Fixed terrainDefenseBonus(const GameState& st, u32 system) {
    const SystemNode* sys = st.system(system);
    if (sys == nullptr) return Fixed(0);
    Fixed bonus = Fixed(0);
    if (sys->megastructure) bonus += Fixed::pct(25);
    if (sys->anomaly > 0) bonus += Fixed::pct(10);
    if (sys->hazard.rawValue() > Fixed::pct(12).rawValue()) bonus += Fixed::pct(12);
    if (sys->pirates.rawValue() > Fixed::pct(15).rawValue()) bonus += Fixed::pct(8);
    if (sys->capital) bonus += Fixed::pct(20);
    return bonus;
}

Fixed systemDefense(const GameState& st, u32 system) {
    const SystemNode* sys = st.system(system);
    if (sys == nullptr) return Fixed(0);
    Fixed def = Fixed(0);
    for (const auto& f : st.fleets) {
        if (f.system != system) continue;
        if (sys->owner != kNoEmpire && f.owner != sys->owner) continue;
        def += fleetPower(st, f.id);
    }
    for (u32 pid : sys->planets) {
        const Planet* p = st.planet(pid);
        if (p == nullptr) continue;
        def += p->garrison;
        for (u32 bid : p->buildings) {
            const BuildingInfo& bi = buildingInfo(static_cast<int>(bid & 0xFFu));
            if (bi.effect == BuildingEffect::Defense) def += bi.effectValue;
        }
    }
    def = def * (Fixed(1) + Fixed::pct(20) + terrainDefenseBonus(st, system));
    if (sys->megastructure) {
        def += Fixed(400);
        if (sys->megastructureId < kMegastructureCount) {
            const auto& mega = megastructureInfo(sys->megastructureId);
            if (mega.effect == BuildingEffect::Defense) def += mega.effectValue;
        }
    }
    // 恒星基地：必须加在**按星系**的防守计算里。
    // 早期误加在 combatOdds（帝国对帝国的粗估）上 —— 那里不知道哪个星系被攻击，
    // 取的是「防守方全国基地之和」，实测对战斗毫无影响。
    // 基地的意义在于守住**具体这个星系**，因此只能在这里计入。
    if (const Starbase* base = starbaseAt(st, system); base != nullptr && base->owner == sys->owner) {
        def += starbaseDefense(base->tier);
    }
    return def;
}

Fixed combatOdds(const GameState& st, u32 attacker, u32 defender) {
    Fixed att = Fixed(0);
    const Empire* a = st.empire(attacker);
    if (a != nullptr) {
        for (u32 fid : a->fleets) att += fleetPower(st, fid);
        att = att * (Fixed(1) + empireModifier(*a, ModKind::MilitaryPower));
    }
    Fixed def = Fixed(0);
    const Empire* d = st.empire(defender);
    if (d != nullptr) {
        for (u32 fid : d->fleets) def += fleetPower(st, fid);
        def = def * (Fixed(1) + empireModifier(*d, ModKind::MilitaryPower));
    }
    def = def * Fixed::raw(1250);
    if (att.rawValue() + def.rawValue() <= 0) return Fixed::pct(50);
    Fixed odds = Fixed::raw(mulDivSat(att.rawValue(), FIX, att.rawValue() + def.rawValue()));
    return fxClamp(odds, Fixed::pct(2), Fixed::pct(98));
}

Fixed simulateBattleOdds(const GameState& st, u32 system, u32 attacker, u32 defender) {
    // 用「有效展开战力 + 组织度总量」估算，不改变状态
    std::vector<u32> af, df;
    collectFleets(st, attacker, system, af);
    collectFleets(st, defender, system, df);
    Fixed ap = sidePower(st, af) + Fixed(static_cast<i64>(af.size())) * Fixed(20);
    Fixed dp = sidePower(st, df) + Fixed(static_cast<i64>(df.size())) * Fixed(20);
    // 恒星基地与行星工事应当**始终**参与防守，而不是只在没有舰队时兜底。
    // 早期是 `if (dp <= 0) dp = systemDefense(...)` —— 只要有驻军，
    // 基地与要塞就完全被忽略，实测把防守从 1,021 抬到 3,621 也不影响胜算。
    Fixed fort = Fixed(0);
    if (st.system(system) != nullptr && st.system(system)->owner == defender)
        fort = systemDefense(st, system);
    dp += fort;
    dp = dp * (Fixed(1) + terrainDefenseBonus(st, system));
    if (ap.rawValue() + dp.rawValue() <= 0) return Fixed::pct(50);
    return fxClamp(Fixed::raw(mulDivSat(ap.rawValue(), FIX, ap.rawValue() + dp.rawValue())), Fixed::pct(2),
                   Fixed::pct(98));
}

u32 beginBattle(GameState& st, u32 system, u32 attacker, u32 defender) {
    // 已有同一对交战方的战斗 ⇒ 复用
    for (auto& b : st.battles) {
        if (b.resolved) continue;
        if (b.system != system) continue;
        if (b.attacker == attacker && b.defender == defender) {
            collectFleets(st, attacker, system, b.attackerFleets);
            collectFleets(st, defender, system, b.defenderFleets);
            return b.id;
        }
    }
    Battle b;
    b.id = st.nextBattleId++;
    b.system = system;
    b.attacker = attacker;
    b.defender = defender;
    b.startTick = st.tick;
    b.widthCap = Fixed(kBaseWidth);
    b.terrainName = terrainNameOf(st, system);
    b.terrainDefense = terrainDefenseBonus(st, system);
    collectFleets(st, attacker, system, b.attackerFleets);
    collectFleets(st, defender, system, b.defenderFleets);
    // 自动指派在场最高军衔指挥官
    auto pickCommander = [&](u32 owner) -> u32 {
        u32 best = 0xFFFFFFFFu;
        Fixed bestSkill = Fixed(-1);
        for (const auto& c : st.commanders) {
            if (c.owner != owner) continue;
            Fixed skill = c.attack + c.defense + c.planning;
            if (skill.rawValue() > bestSkill.rawValue()) {
                bestSkill = skill;
                best = c.id;
            }
        }
        return best;
    };
    b.attackerCommander = pickCommander(attacker);
    b.defenderCommander = pickCommander(defender);
    st.battles.push_back(std::move(b));
    // 标记参战舰队
    Battle& nb = st.battles.back();
    for (u32 id : nb.attackerFleets) {
        Fleet* f = st.fleet(id);
        if (f != nullptr) {
            f->battle = nb.id;
            f->planning = Fixed(0);   // 进攻消耗准备度
        }
    }
    for (u32 id : nb.defenderFleets) {
        Fleet* f = st.fleet(id);
        if (f != nullptr) f->battle = nb.id;
    }
    return nb.id;
}

namespace {

/// 把伤害分摊到舰队：组织度优先，其次兵力
void applyDamage(GameState& st, const std::vector<u32>& fleets, Fixed damage, Fixed orgDamage) {
    if (fleets.empty()) return;
    Fixed totalStr = Fixed(0);
    for (u32 id : fleets) {
        const Fleet* f = st.fleet(id);
        if (f != nullptr) totalStr += f->strength;
    }
    if (totalStr.rawValue() <= 0) totalStr = Fixed(static_cast<i64>(fleets.size()));
    for (u32 id : fleets) {
        Fleet* f = st.fleet(id);
        if (f == nullptr) continue;
        Fixed share = f->strength / totalStr;
        // 兵力损失
        Fixed dmg = damage * share;
        f->strength = f->strength - dmg;
        if (f->strength.rawValue() < FIX / 2) f->strength = Fixed::raw(FIX / 2);
        // 组织度损失（这是战斗持续性的关键：组织度先崩，兵力后损）
        f->org = f->org - orgDamage * share * Fixed(2);
        if (f->org.rawValue() < 0) f->org = Fixed(0);
        f->morale = fxClamp(f->morale - Fixed::pct(3), Fixed::pct(10), Fixed::pct(100));
        // 战斗经验累积
        // 经验以「0..1000 点」存于 raw（veterancyLevel 直接比较 rawValue）
        f->experience = fxClamp(Fixed::raw(f->experience.rawValue() + 12), Fixed(0), Fixed::raw(1000));
    }
}

}  // namespace

int battlePhase(GameState& st, TickReport& rep) {
    int finished = 0;
    for (auto& b : st.battles) {
        if (b.resolved) continue;
        ++b.ticks;
        SystemNode* sys = st.system(b.system);

        // ---- 刷新参战名单（增援 / 撤退会自动反映）----
        collectFleets(st, b.attacker, b.system, b.attackerFleets);
        collectFleets(st, b.defender, b.system, b.defenderFleets);
        // 防守方若有主权，把要塞算作额外防御（不占宽度）
        Fixed defBase = sidePower(st, b.defenderFleets);
        if (sys != nullptr && sys->owner == b.defender) {
            Fixed fort = systemDefense(st, b.system) - defBase;
            if (fort.rawValue() < 0) fort = Fixed(0);
            defBase += fort;
        }

        // ---- 组织度耗尽判定 ----
        Fixed attOrg = Fixed(0), defOrg = Fixed(0);
        for (u32 id : b.attackerFleets) {
            const Fleet* f = st.fleet(id);
            if (f != nullptr) attOrg += f->org;
        }
        for (u32 id : b.defenderFleets) {
            const Fleet* f = st.fleet(id);
            if (f != nullptr) defOrg += f->org;
        }

        auto finish = [&](bool attackerWon, const std::string& why) {
            b.resolved = true;
            b.attackerWon = attackerWon;
            b.outcome = why;
            ++finished;
            // 指挥官战绩
            Commander* ac = st.commander(b.attackerCommander);
            Commander* dc = st.commander(b.defenderCommander);
            if (ac != nullptr) {
                (attackerWon ? ac->battlesWon : ac->battlesLost) += 1;
                ac->experience = fxClamp(Fixed::raw(ac->experience.rawValue() + 20), Fixed(0), Fixed::raw(1000));
                // 战功：胜方显著更多，规模越大、持续越久战功越高
                Fixed merit = Fixed(40) + Fixed(b.ticks) * Fixed(6);
                if (attackerWon) merit = merit * Fixed::raw(1500);
                (void)commanderAwardMerit(st, ac->id, merit);
            }
            if (dc != nullptr) {
                (attackerWon ? dc->battlesLost : dc->battlesWon) += 1;
                dc->experience = fxClamp(Fixed::raw(dc->experience.rawValue() + 15), Fixed(0), Fixed::raw(1000));
                Fixed merit = Fixed(30) + Fixed(b.ticks) * Fixed(5);
                if (!attackerWon) merit = merit * Fixed::raw(1500);
                (void)commanderAwardMerit(st, dc->id, merit);
            }
            // 战果与胜场直接结算
            {
                // 战争分数必须是**有方向的**：relation(X,Y).warScore 表示 X 对 Y 的战果。
                // 早期把攻方的分数抄给守方（relInv.warScore = rel2.warScore），
                // 抹掉了方向信息 —— 和平会议因此无法判断谁占优，
                // 实测会出现「胜方反被割地」的荒谬结果。
                Relation& rel2 = st.relation(b.attacker, b.defender);
                Relation& relInv = st.relation(b.defender, b.attacker);
                if (attackerWon) {
                    rel2.warScore += 1;
                    if (relInv.warScore > 0) --relInv.warScore;   // 守方战果倒退
                    if (Empire* ea = st.empire(b.attacker); ea != nullptr) ea->warsWon += 1;
                } else {
                    if (rel2.warScore > 0) --rel2.warScore;
                    relInv.warScore += 1;
                }
            }
            // 解除舰队战斗标记
            for (u32 id : b.attackerFleets) {
                Fleet* f = st.fleet(id);
                if (f != nullptr && f->battle == b.id) f->battle = 0xFFFFFFFFu;
            }
            for (u32 id : b.defenderFleets) {
                Fleet* f = st.fleet(id);
                if (f != nullptr && f->battle == b.id) f->battle = 0xFFFFFFFFu;
            }
            // 直接在此记录战报 —— 之前用「tick 比对」在事后扫描，
            // 条件脆弱且容易漏记（战斗跨 tick 时算不准）。
            {
                Battle snapshot = b;
                snapshot.resolved = true;
                snapshot.attackerWon = attackerWon;
                snapshot.outcome = why;
                st.logEvent(LogPhase::Combat, kLogWar, battleReportText(snapshot, st), b.attacker,
                            attackerWon ? Fixed(1) : Fixed(-1));
            }
            // 进攻方胜利 ⇒ 夺取星系（推进度满）
            if (attackerWon && sys != nullptr && sys->owner == b.defender) {
                // 夺取星系：战争分数 +3（和平会议的主要筹码来源），同样保持有方向
                st.relation(b.attacker, b.defender).warScore += 3;
                sys->owner = b.attacker;
                sys->colonized = true;
                Empire* a = st.empire(b.attacker);
                Empire* d = st.empire(b.defender);
                // 已覆灭的帝国不得再夺取星系（否则会「死而复生」地持有领土）
                if (a != nullptr && a->alive) a->systems.push_back(b.system);
                if (d != nullptr) {
                    d->systems.erase(std::remove(d->systems.begin(), d->systems.end(), b.system),
                                     d->systems.end());
                    for (u32 pid : sys->planets) {
                        Planet* p = st.planet(pid);
                        if (p != nullptr && p->owner == b.defender) {
                            p->owner = b.attacker;
                            p->unrest += Fixed::pct(25);
                        }
                    }
                }
            }
        };

        // 先算好展开宽度，保证即便战斗立刻结束，战报里的宽度也是有意义的
        {
            b.attackerWidth = effectiveWidth(st, b, true, b.attacker);
            b.defenderWidth = effectiveWidth(st, b, false, b.defender);
        }

        // 某一方已无兵力 ⇒ 直接结束
        if (b.attackerFleets.empty() && attOrg.rawValue() <= 0) {
            finish(false, "进攻方已无可用舰队，撤退");
            continue;
        }
        if (b.defenderFleets.empty() && defBase.rawValue() <= 0) {
            finish(true, "防守方已无可用舰队与要塞，进攻方接管");
            continue;
        }
        if (attOrg.rawValue() <= 0 && !b.attackerFleets.empty()) {
            finish(false, "进攻方组织度耗尽，被迫撤退");
            continue;
        }
        if (defOrg.rawValue() <= 0 && !b.defenderFleets.empty() && defBase.rawValue() <= 0) {
            finish(true, "防守方组织度耗尽，战线崩溃");
            continue;
        }

        // ---- 战斗宽度：超出部分无法参战 ----
        Fixed attWidth = b.attackerWidth;
        Fixed defWidth = b.defenderWidth;
        Fixed attEff = fxMin(sidePower(st, b.attackerFleets), attWidth);
        Fixed defEff = fxMin(defBase, defWidth);
        // 超出宽度的部分产生惩罚（指挥混乱）
        Fixed attOverflow = sidePower(st, b.attackerFleets) - attWidth;
        Fixed defOverflow = defBase - defWidth;
        b.attackerPenalty = attOverflow.rawValue() > 0
                                ? fxClamp(attOverflow / (attWidth + Fixed(1)) * Fixed::pct(30), Fixed(0),
                                          Fixed::pct(50))
                                : Fixed(0);
        b.defenderPenalty = defOverflow.rawValue() > 0
                                ? fxClamp(defOverflow / (defWidth + Fixed(1)) * Fixed::pct(30), Fixed(0),
                                          Fixed::pct(50))
                                : Fixed(0);

        if (attEff.rawValue() <= 0) {
            finish(false, "进攻方无法展开任何战力");
            continue;
        }

        // ---- 每 tick 伤害结算 ----
        // 地形与要塞给防守方减伤
        Fixed defMul = Fixed(1) + b.terrainDefense;
        // 注意：Fixed::pct 是百分之一，Fixed::bp 是万分之一。
        // 这里用 pct —— 每 tick 打掉对方几个百分点的兵力。
        Fixed attDmg = attEff * Fixed::pct(4) / defMul;    // 进攻方造成的兵力伤害
        Fixed defDmg = defEff * Fixed::pct(3) * defMul;    // 防守方造成的兵力伤害
        // 指挥官与准备度修正
        if (const Commander* ac = st.commander(b.attackerCommander); ac != nullptr) {
            attDmg = attDmg * (Fixed(1) + (ac->attack + commanderTraitAttack(*ac)) / Fixed(2) +
                               ac->planning / Fixed(3));
        }
        if (const Commander* dc = st.commander(b.defenderCommander); dc != nullptr) {
            defDmg = defDmg * (Fixed(1) + (dc->defense + commanderTraitDefense(*dc)) / Fixed(2));
        }
        // 组织度伤害（远高于兵力伤害 —— 战斗通常以组织度崩溃告终）
        // 组织度伤害需与组织度池（~115-145）同量级，否则战斗永不结束
        Fixed attOrgDmg = Fixed(25) + attEff * Fixed::pct(2);
        Fixed defOrgDmg = Fixed(22) + defEff * Fixed::pct(2);
        // 补给不足会加速组织度流失
        for (u32 id : b.defenderFleets) {
            const Fleet* f = st.fleet(id);
            if (f != nullptr && f->supply.rawValue() < Fixed::pct(40).rawValue()) defOrgDmg += Fixed(8);
        }
        for (u32 id : b.attackerFleets) {
            const Fleet* f = st.fleet(id);
            if (f != nullptr && f->supply.rawValue() < Fixed::pct(40).rawValue()) attOrgDmg += Fixed(8);
        }

        applyDamage(st, b.attackerFleets, defDmg, defOrgDmg);
        applyDamage(st, b.defenderFleets, attDmg, attOrgDmg);

        // 指挥官在交战期间持续积累经验（不只是战斗结束时结算），
        // 规模越大、持续越久的战斗学到越多。
        {
            Fixed scale = fxClamp(Fixed::raw(b.ticks) / Fixed(10), Fixed(0), Fixed(1));
            Fixed gain = Fixed::raw(3) + Fixed::raw(b.ticks) * Fixed::raw(1);
            if (Commander* ac = st.commander(b.attackerCommander); ac != nullptr) {
                ac->experience = fxClamp(Fixed::raw(ac->experience.rawValue() + gain.rawValue()), Fixed(0),
                                         Fixed::raw(1000));
                ac->planning = fxClamp(ac->planning + scale * Fixed::pct(2), Fixed(0), Fixed(1));
            }
            if (Commander* dc = st.commander(b.defenderCommander); dc != nullptr) {
                dc->experience = fxClamp(Fixed::raw(dc->experience.rawValue() + gain.rawValue()), Fixed(0),
                                         Fixed::raw(1000));
                dc->planning = fxClamp(dc->planning + scale * Fixed::pct(2), Fixed(0), Fixed(1));
            }
        }

        b.attackerLoss += defDmg;
        b.defenderLoss += attDmg;
        b.attackerOrgLoss += defOrgDmg;
        b.defenderOrgLoss += attOrgDmg;

        // ---- 推进度：优势方随时间积累 ----
        Fixed ratio = Fixed::raw(mulDivSat(attEff.rawValue(), FIX, attEff.rawValue() + defEff.rawValue() + 1));
        Fixed advance = (ratio - Fixed::pct(50)) * Fixed::pct(120);
        b.progress = fxClamp(b.progress + advance, Fixed(0), Fixed(1));
        if (b.progress.rawValue() >= FIX) {
            finish(true, "进攻方推进度已满，夺取星系");
            continue;
        }
        // 长期僵持：若战斗超过 16 季且无实质进展，进攻方因补给不支撤退
        if (b.ticks >= 16 && b.progress.rawValue() < Fixed::pct(20).rawValue()) {
            finish(false, "长期僵持，进攻方补给不支撤退");
            continue;
        }
    }

    // 清理已结束的战斗（保留最近 32 条用于战报）
    if (st.battles.size() > 32) {
        st.battles.erase(std::remove_if(st.battles.begin(), st.battles.end() - 32,
                                        [](const Battle& b) { return b.resolved; }),
                         st.battles.end() - 32);
    }
    (void)rep;
    return finished;
}

std::string battleReportText(const Battle& b, const GameState& st) {
    const Empire* a = st.empire(b.attacker);
    const Empire* d = st.empire(b.defender);
    const SystemNode* sys = st.system(b.system);
    std::string out;
    out += "战斗报告：" + std::string(sys ? sys->name : "?") + "（" + b.terrainName + "）\n";
    out += "  进攻方 " + std::string(a ? a->name : "?") + "  损失 " + fixedStr(b.attackerLoss, 0) +
           "  组织度损失 " + fixedStr(b.attackerOrgLoss, 0) + "\n";
    out += "  防守方 " + std::string(d ? d->name : "?") + "  损失 " + fixedStr(b.defenderLoss, 0) +
           "  组织度损失 " + fixedStr(b.defenderOrgLoss, 0) + "\n";
    out += "  历时 " + std::to_string(b.ticks) + " 季  结果：" +
           std::string(b.attackerWon ? "进攻方夺取星系" : "进攻方失败") + " —— " + b.outcome;
    return out;
}

std::string battlesText(const GameState& st, u32 filterOwner) {
    std::string out;
    int shown = 0;
    for (const auto& b : st.battles) {
        if (filterOwner != 0xFFFFFFFFu && b.attacker != filterOwner && b.defender != filterOwner) continue;
        const Empire* a = st.empire(b.attacker);
        const Empire* d = st.empire(b.defender);
        const SystemNode* sys = st.system(b.system);
        out += "\n  " + style("战斗 #" + std::to_string(b.id), Style::Heading) + "  " +
               std::string(sys ? sys->name : "?") + "（" + b.terrainName + "）" +
               (b.resolved ? style("  [已结束]", Style::Dim) : style("  [进行中]", Style::Warn)) + "\n";
        out += "    " + padRight(std::string(a ? a->name : "?"), 16) + "→ " +
               padRight(std::string(d ? d->name : "?"), 16) + "  持续 " + std::to_string(b.ticks) + " 季\n";
        out += "    进攻方：舰队 " + std::to_string(b.attackerFleets.size()) + " 支  展开宽度 " +
               fixedStr(b.attackerWidth, 0) + "  损失 " + fixedStr(b.attackerLoss, 0) + "  组织度损失 " +
               fixedStr(b.attackerOrgLoss, 0) + "\n";
        out += "    防守方：舰队 " + std::to_string(b.defenderFleets.size()) + " 支  展开宽度 " +
               fixedStr(b.defenderWidth, 0) + "  损失 " + fixedStr(b.defenderLoss, 0) + "  组织度损失 " +
               fixedStr(b.defenderOrgLoss, 0) + "\n";
        out += "    推进度 " + bar(b.progress, 24) + " " + fixedStrPlain(b.progress * Fixed(100), 1) + "%";
        if (b.attackerPenalty.rawValue() > 0)
            out += "   进攻宽度惩罚 " + fixedStrPlain(b.attackerPenalty * Fixed(100), 0) + "%";
        if (b.defenderPenalty.rawValue() > 0)
            out += "   防守宽度惩罚 " + fixedStrPlain(b.defenderPenalty * Fixed(100), 0) + "%";
        out += "\n";
        if (b.resolved) out += "    结果：" + b.outcome + "\n";
        ++shown;
    }
    if (shown == 0) out = "  （当前没有战斗）\n";
    return out;
}

void fleetRecoveryPhase(GameState& st) {
    for (auto& f : st.fleets) {
        // 组织度恢复：非交战状态恢复更快
        Fixed maxOrg = fleetMaxOrg(st, f);
        f.maxOrg = maxOrg;
        if (f.org.rawValue() <= 0) {
            // 组织度归零的舰队自动退出战斗并撤往相邻己方星系
            if (f.battle != 0xFFFFFFFFu) {
                f.battle = 0xFFFFFFFFu;
                if (f.order != FleetOrder::Move) {
                    f.order = FleetOrder::Retreat;
                }
            }
            f.org = f.org + maxOrg * Fixed::pct(25);
        } else if (f.battle != 0xFFFFFFFFu) {
            // 交战中几乎不恢复（只有鼓舞特质能带来小幅回复）
            const Commander* c = st.commander(f.commander);
            Fixed insp = (c != nullptr && c->trait == CommanderTrait::Inspiring) ? Fixed::bp(40) : Fixed(0);
            f.org = fxMin(maxOrg, f.org + maxOrg * insp);
        } else {
            const Commander* c = st.commander(f.commander);
            Fixed logi = (c != nullptr) ? commanderTraitLogistics(*c) : Fixed(0);
            f.org = fxMin(maxOrg, f.org + maxOrg * (Fixed::pct(20) + logi));
            // 战斗准备度累积
            f.planning = fxMin(Fixed(1), f.planning + Fixed::pct(12));
            if (f.order == FleetOrder::Retreat) f.order = FleetOrder::Idle;
        }
        // 补给恢复 / 消耗
        const Commander* c = st.commander(f.commander);
        Fixed logi = (c != nullptr) ? commanderTraitLogistics(*c) : Fixed(0);
        if (f.battle != 0xFFFFFFFFu) {
            f.supply = fxClamp(f.supply - Fixed::pct(8) * (Fixed(1) - logi), Fixed(0), Fixed::pct(100));
        } else {
            f.supply = fxClamp(f.supply + Fixed::pct(10) * (Fixed(1) + logi), Fixed(0), Fixed::pct(100));
            // 士气缓慢恢复
            f.morale = fxClamp(f.morale + Fixed::pct(8), Fixed::pct(10), Fixed::pct(100));
        }
        f.veteran = veterancyLevel(f.experience) >= 2;
    }
}

void fleetMovementPhase(GameState& st) {
    for (auto& f : st.fleets) {
        // 交战中或组织度归零的舰队不移动
        if (f.battle != 0xFFFFFFFFu) continue;
        if (f.org.rawValue() <= 0) continue;
        if (f.order == FleetOrder::Idle) continue;
        if (f.targetSystem == kNoSystem) continue;
        if (f.system == f.targetSystem) {
            if (f.order == FleetOrder::Move) f.order = FleetOrder::Patrol;
            continue;
        }
        const SystemNode* sys = st.system(f.system);
        if (sys == nullptr) continue;
        int bestHops = 1 << 30;
        u32 bestNext = f.system;
        for (u32 nx : sys->links) {
            int h = st.map.hops(nx, f.targetSystem);
            if (h >= 0 && h < bestHops) {
                bestHops = h;
                bestNext = nx;
            }
        }
        if (bestNext != f.system) {
            f.system = bestNext;
            f.supply = fxClamp(f.supply - Fixed::pct(3), Fixed(0), Fixed::pct(100));
        }
    }
}

void combatPhase(GameState& st, TickReport& rep) {
    // 阶段顺序至关重要：
    //   1) 先移动（决定谁和谁接触）
    //   2) 再开辟/推进战斗（结算伤害与组织度损失）
    //   3) 最后才恢复
    // 若把恢复放在战斗之前，恢复量会淹没组织度损失，
    // 导致「战斗中组织度反而上升」——战斗永远不会结束。

    // 1) 移动
    fleetMovementPhase(st);

    // 1.2) 前线部署：把闲置舰队按战线兵力缺口自动分配。
    // HOI4 的「前线」语义 —— 玩家给一个战略方向，系统负责把部队铺上去。
    assignFronts(st);

    // 1.5) 遭遇战：交战双方的舰队同处一个星系时必须开战。
    // 这是「舰队机动 → 接触 → 交战」的核心路径，缺了它战斗永远不会发生。
    {
        std::vector<std::pair<std::pair<u32, u32>, u32>> encounters;   // (att,def), system
        for (std::size_t i = 0; i < st.fleets.size(); ++i) {
            for (std::size_t j = i + 1; j < st.fleets.size(); ++j) {
                const Fleet& fa = st.fleets[i];
                const Fleet& fb = st.fleets[j];
                if (fa.owner == fb.owner) continue;
                if (fa.system != fb.system) continue;
                if (fa.org.rawValue() <= 0 || fb.org.rawValue() <= 0) continue;
                if (!atWarWith(st, fa.owner, fb.owner)) continue;
                // 星系所有者是防守方，另一方是入侵者（进攻方）。
                // 若星系无主，则按舰队序号定序，保证确定性。
                u32 sysOwner = st.system(fa.system) != nullptr ? st.system(fa.system)->owner : kNoEmpire;
                u32 att = fa.owner, def = fb.owner;
                if (sysOwner == fb.owner) {
                    att = fa.owner;
                    def = fb.owner;
                } else if (sysOwner == fa.owner) {
                    att = fb.owner;
                    def = fa.owner;
                }
                encounters.push_back({{att, def}, fa.system});
            }
        }
        std::sort(encounters.begin(), encounters.end());
        encounters.erase(std::unique(encounters.begin(), encounters.end()), encounters.end());
        for (const auto& e : encounters) {
            (void)beginBattle(st, e.second, e.first.first, e.first.second);
        }
    }

    // 2) 对每一对交战国，在「攻方舰队已抵达的敌方星系」开辟战斗。
    //
    // 关键：只有攻方真的有舰队在场时才开辟战斗。
    // 之前会在接壤星系无条件开辟，导致攻方零舰队也会产生一场
    // 「历时 1 季、损失 0、进攻方失败」的幽灵战斗。
    // 注意：即便守方没有舰队，只要星系归守方所有，要塞仍会防守 ——
    // 这条路径覆盖「攻方推进到无舰队防守的敌方星系」。
    for (std::size_t idx = 0; idx < st.relations.size(); ++idx) {
        Relation& rel = st.relations[idx];
        if (!rel.atWar) continue;
        u32 att = static_cast<u32>(idx / kMaxEmpires);
        u32 def = static_cast<u32>(idx % kMaxEmpires);
        if (att >= st.empires.size() || def >= st.empires.size()) continue;
        if (att == def) continue;
        // 按领土归属检查两个方向；beginBattle 负责同一星系的去重。
        if (!st.empires[att].alive || !st.empires[def].alive) continue;

        // 收集攻方舰队所在的、属于守方的星系
        std::vector<u32> incursions;
        for (const auto& f : st.fleets) {
            if (f.owner != att) continue;
            if (f.org.rawValue() <= 0) continue;
            const SystemNode* s = st.system(f.system);
            if (s == nullptr) continue;
            if (s->owner != def) continue;
            incursions.push_back(f.system);
        }
        std::sort(incursions.begin(), incursions.end());
        incursions.erase(std::unique(incursions.begin(), incursions.end()), incursions.end());
        for (u32 sysId : incursions) (void)beginBattle(st, sysId, att, def);

        // 另外：攻方舰队进入「无主但接壤守方」的星系不构成入侵，
        // 只有真正进入敌方领土才算。
    }

    // 3) 推进所有战斗（伤害与组织度损失在此结算）
    int before = 0;
    for (const auto& b : st.battles)
        if (!b.resolved) ++before;
    int finished = battlePhase(st, rep);

    // 3.5) 战斗结束后才做恢复：仍在交战中的舰队恢复极慢
    fleetRecoveryPhase(st);

    // 4) 战争结算（战报已在战斗结束时记录）
    for (std::size_t idx = 0; idx < st.relations.size(); ++idx) {
        Relation& rel = st.relations[idx];
        if (!rel.atWar) continue;
        u32 att = static_cast<u32>(idx / kMaxEmpires);
        u32 def = static_cast<u32>(idx % kMaxEmpires);
        if (att >= st.empires.size() || def >= st.empires.size()) continue;
        // 战果在 finish() 内已直接结算，此处只需检查求和条件
        // 战争不再「自动结束」—— 达到阈值时召开**和平会议**，
        // 由占优方用战争分数兑换领土与赔款（见 peacePhase）。
        // 早期版本只把 atWar 置回 false，导致打了半天没有任何领土变更。
    }
    // 覆灭判定：失去全部星系与行星的帝国即被消灭。
    // 缺了这一步，被彻底征服的国家会「零领土但有舰队」地永远存活 ——
    // 既没有领土可攻、也不会灭亡，胜利条件因此**在数学上不可达**
    //（实测玩家占下 17 个星系后，胜利判定仍显示「仍有 7 个对手未被击败」）。
    for (auto& e : st.empires) {
        if (e.isPlayer || !e.alive) continue;
        if (!e.systems.empty()) continue;
        bool hasPlanet = false;
        for (const auto& p : st.planets)
            if (p.owner == e.id) hasPlanet = true;
        if (hasPlanet) continue;
        e.alive = false;
        e.systems.clear();
        e.fleets.clear();
        st.logEvent(LogPhase::Combat, kLogWar, e.name + " 已被彻底征服，退出历史舞台", e.id);
    }

    (void)before;
    (void)finished;
}

// ---------------------------------------------------------------------------
// 指挥官
// ---------------------------------------------------------------------------
u32 recruitCommander(GameState& st, u32 empire, std::string name, CommanderTrait trait) {
    Commander c;
    c.id = static_cast<u32>(st.commanders.size());
    c.name = std::move(name);
    c.owner = empire;
    c.trait = trait;
    // 技能由帝国军事传统与随机决定
    SplitMix64 rng(st.seed ^ (static_cast<u64>(c.id) * 0x9E3779B97F4A7C15ull) ^ st.tick);
    const Empire* e = st.empire(empire);
    Fixed base = Fixed::pct(40);
    if (e != nullptr) base = base + empireModifier(*e, ModKind::MilitaryPower);
    c.attack = fxClamp(base + Fixed::raw(static_cast<i64>(rng.nextU64() % 400)), Fixed::pct(10), Fixed::pct(95));
    c.defense = fxClamp(base + Fixed::raw(static_cast<i64>(rng.nextU64() % 400)), Fixed::pct(10), Fixed::pct(95));
    c.logistics = fxClamp(base + Fixed::raw(static_cast<i64>(rng.nextU64() % 400)), Fixed::pct(10), Fixed::pct(95));
    c.planning = fxClamp(base + Fixed::raw(static_cast<i64>(rng.nextU64() % 400)), Fixed::pct(10), Fixed::pct(95));
    st.commanders.push_back(std::move(c));
    return st.commanders.back().id;
}

bool assignCommander(GameState& st, u32 commanderId, u32 fleetId, std::string* err) {
    Commander* c = st.commander(commanderId);
    if (c == nullptr) {
        if (err) *err = "找不到该指挥官";
        return false;
    }
    Fleet* f = st.fleet(fleetId);
    if (f == nullptr) {
        if (err) *err = "找不到该舰队";
        return false;
    }
    if (c->owner != f->owner) {
        if (err) *err = "指挥官与舰队不属于同一阵营";
        return false;
    }
    // 一个指挥官只能带一支舰队
    for (auto& other : st.fleets) {
        if (other.id != fleetId && other.commander == commanderId) other.commander = 0xFFFFFFFFu;
    }
    c->fleet = fleetId;
    f->commander = commanderId;
    return true;
}

std::string commandersText(const GameState& st, u32 empireId) {
    std::string out;
    int shown = 0;
    for (const auto& c : st.commanders) {
        if (empireId != 0xFFFFFFFFu && c.owner != empireId) continue;
        const Fleet* f = (c.fleet != 0xFFFFFFFFu) ? st.fleet(c.fleet) : nullptr;
        out += "  " + padRight(c.name, 12) + padRight(std::string(commanderTraitName(c.trait)), 8) +
               " 攻 " + padLeft(fixedStrPlain(c.attack, 2), 5) + " 防 " + padLeft(fixedStrPlain(c.defense, 2), 5) +
               " 勤 " + padLeft(fixedStrPlain(c.logistics, 2), 5) + " 谋 " +
               padLeft(fixedStrPlain(c.planning, 2), 5) + "   战绩 " + std::to_string(c.battlesWon) + "胜" +
               std::to_string(c.battlesLost) + "负" + "   经验 " +
               padLeft(fixedStrPlain(c.experience, 0), 4) + "  " +
               (f != nullptr ? ("→ " + f->name) : std::string("（未分配）")) + "\n";
        ++shown;
    }
    if (shown == 0) out = "  （没有指挥官）\n";
    return out;
}

}  // namespace gf
