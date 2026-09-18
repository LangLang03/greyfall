#include "domain/SpyNetwork.h"

#include <algorithm>

#include "util/TextTable.h"
#include "core/GameState.h"
#include "domain/Empire.h"
#include "domain/Treaty.h"
#include "gen/EmpireGen.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {
namespace {

SpyNetwork* findNet(GameState& st, u32 empire, u32 target) {
    Empire* e = st.empire(empire);
    if (e == nullptr) return nullptr;
    for (auto& n : e->spy.networks) {
        if (n.target == target && !n.burned) return &n;
    }
    return nullptr;
}

const SpyNetwork* findNetC(const GameState& st, u32 empire, u32 target) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return nullptr;
    for (const auto& n : e->spy.networks) {
        if (n.target == target && !n.burned) return &n;
    }
    return nullptr;
}

/// 该帝国已派驻的特工总数
int deployedAgents(const Empire& e) {
    int n = 0;
    for (const auto& net : e.spy.networks) {
        if (net.burned) continue;
        n += net.agents;
    }
    return n;
}

}  // namespace

std::string_view spyMissionName(SpyMission m) {
    switch (m) {
        case SpyMission::Recon: return "侦察";
        case SpyMission::StealTech: return "窃取科技";
        case SpyMission::Sabotage: return "破坏";
        case SpyMission::FundUnrest: return "煽动";
        case SpyMission::SleeperCell: return "潜伏";
        case SpyMission::Ideological: return "意识形态渗透";
        case SpyMission::CounterIntel: return "反渗透";
        case SpyMission::FalseFlag: return "假情报";
        case SpyMission::Count: break;
    }
    return "?";
}

SpyMission spyMissionFromName(std::string_view s) {
    for (int i = 0; i < kSpyMissionCount; ++i)
        if (spyMissionName(static_cast<SpyMission>(i)) == s) return static_cast<SpyMission>(i);
    if (iequals(s, "recon")) return SpyMission::Recon;
    if (iequals(s, "steal") || iequals(s, "stealtech")) return SpyMission::StealTech;
    if (iequals(s, "sabotage")) return SpyMission::Sabotage;
    if (iequals(s, "unrest") || iequals(s, "fundunrest")) return SpyMission::FundUnrest;
    if (iequals(s, "sleeper")) return SpyMission::SleeperCell;
    return SpyMission::Count;
}

Fixed spyMissionMinInfiltration(SpyMission m) {
    switch (m) {
        case SpyMission::Recon: return Fixed::pct(10);
        case SpyMission::FundUnrest: return Fixed::pct(25);
        case SpyMission::StealTech: return Fixed::pct(40);
        case SpyMission::Sabotage: return Fixed::pct(55);
        case SpyMission::SleeperCell: return Fixed::pct(65);
        case SpyMission::Ideological: return Fixed::pct(70);
        case SpyMission::CounterIntel: return Fixed::pct(20);
        case SpyMission::FalseFlag: return Fixed::pct(60);
        case SpyMission::Count: break;
    }
    return Fixed(1);
}

Fixed spyMissionCost(SpyMission m) {
    switch (m) {
        case SpyMission::Recon: return Fixed::pct(5);
        case SpyMission::FundUnrest: return Fixed::pct(10);
        case SpyMission::StealTech: return Fixed::pct(14);
        case SpyMission::Sabotage: return Fixed::pct(18);
        case SpyMission::SleeperCell: return Fixed::pct(8);
        case SpyMission::Ideological: return Fixed::pct(16);
        case SpyMission::CounterIntel: return Fixed::pct(12);
        case SpyMission::FalseFlag: return Fixed::pct(12);
        case SpyMission::Count: break;
    }
    return Fixed(0);
}

i64 spyMissionUpkeep(SpyMission m) {
    switch (m) {
        case SpyMission::Recon: return 200;
        case SpyMission::FundUnrest: return 600;
        case SpyMission::StealTech: return 800;
        case SpyMission::Sabotage: return 1200;
        case SpyMission::SleeperCell: return 1500;
        case SpyMission::Ideological: return 1800;
        case SpyMission::CounterIntel: return 1000;
        case SpyMission::FalseFlag: return 1400;
        case SpyMission::Count: break;
    }
    return 0;
}

Fixed counterIntelOf(const GameState& st, u32 empire) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return Fixed(0);
    // 基础反间谍 + 帝国修正（政策/科技/建筑）
    return fxClamp(e->counterIntel + empireModifier(*e, ModKind::IntelDefense), Fixed(0), Fixed(1));
}

Fixed infiltrationPowerOf(const GameState& st, u32 empire) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return Fixed(0);
    // 基础能力 + 操纵技巧修正
    return fxClamp(Fixed::pct(30) + empireModifier(*e, ModKind::ManipulationSkill), Fixed::pct(5),
                   Fixed::pct(120));
}

bool spyEstablish(GameState& st, u32 empire, u32 target, std::string* err) {
    Empire* e = st.empire(empire);
    Empire* t = st.empire(target);
    if (e == nullptr || t == nullptr) {
        if (err) *err = "非法主体";
        return false;
    }
    if (empire == target) {
        if (err) *err = "不能对自己建立间谍网络";
        return false;
    }
    if (!t->alive) {
        if (err) *err = "目标已灭亡";
        return false;
    }
    if (findNet(st, empire, target) != nullptr) {
        if (err) *err = "在该国已有活动中的网络";
        return false;
    }
    if (e->spy.agentPool <= 0) {
        if (err)
            *err = "没有空闲特工（共 " + std::to_string(e->spy.totalAgents) + " 名，已派驻 " +
                   std::to_string(deployedAgents(*e)) + " 名）";
        return false;
    }
    const i64 cost = 5000;
    if (e->treasury.rawValue() < Fixed(cost).rawValue()) {
        if (err) *err = "国库不足：建立网络需要 " + groupDigits(cost);
        return false;
    }
    e->treasury -= Fixed(cost);
    if (e->isPlayer) st.market.margin.cash = e->treasury;

    SpyNetwork n;
    n.target = target;
    n.agents = 1;
    n.establishedTick = static_cast<u32>(st.tick);
    // 新网络从一个小据点开始
    n.infiltration = Fixed::pct(5);
    e->spy.networks.push_back(std::move(n));
    e->spy.agentPool -= 1;
    st.logEvent(LogPhase::Clue, "spy.establish",
                e->name + " 在 " + t->name + " 境内建立了情报网络", empire);
    return true;
}

bool spyAssignAgents(GameState& st, u32 empire, u32 target, int delta, std::string* err) {
    Empire* e = st.empire(empire);
    if (e == nullptr) {
        if (err) *err = "非法主体";
        return false;
    }
    SpyNetwork* n = findNet(st, empire, target);
    if (n == nullptr) {
        if (err) *err = "在该国没有活动中的网络";
        return false;
    }
    if (delta > 0) {
        if (e->spy.agentPool < delta) {
            if (err)
                *err = "空闲特工不足：需要 " + std::to_string(delta) + " 名，可用 " +
                       std::to_string(e->spy.agentPool) + " 名";
            return false;
        }
        n->agents += delta;
        e->spy.agentPool -= delta;
        // 增派特工会提高暴露风险
        n->exposure = fxClamp(n->exposure + Fixed::pct(4) * Fixed(delta), Fixed(0), Fixed(1));
    } else if (delta < 0) {
        int drop = -delta;
        if (n->agents < drop) drop = n->agents;
        n->agents -= drop;
        e->spy.agentPool += drop;
    }
    // 特工全部撤回则网络失去意义
    if (n->agents <= 0) {
        e->spy.agentPool += 0;
        return true;
    }
    return true;
}

bool spyRunMission(GameState& st, u32 empire, u32 target, SpyMission m, std::string* err) {
    Empire* e = st.empire(empire);
    Empire* t = st.empire(target);
    if (e == nullptr || t == nullptr) {
        if (err) *err = "非法主体";
        return false;
    }
    if (m == SpyMission::Count) {
        if (err) *err = "未知任务";
        return false;
    }
    SpyNetwork* n = findNet(st, empire, target);
    if (n == nullptr) {
        if (err) *err = "在该国没有活动中的网络（先用 --establish 建立）";
        return false;
    }
    if (n->agents <= 0) {
        if (err) *err = "该网络没有派驻特工";
        return false;
    }
    const Fixed need = spyMissionMinInfiltration(m);
    if (n->infiltration.rawValue() < need.rawValue()) {
        if (err)
            *err = std::string(spyMissionName(m)) + " 需要渗透度 ≥ " +
                   fixedStrPlain(need * Fixed(100), 0) + "%，当前 " +
                   fixedStrPlain(n->infiltration * Fixed(100), 0) + "%";
        return false;
    }
    // 经费
    const i64 upkeep = spyMissionUpkeep(m);
    if (e->treasury.rawValue() < Fixed(upkeep).rawValue()) {
        if (err) *err = "国库不足：该任务需要 " + groupDigits(upkeep);
        return false;
    }

    // 成功率：渗透度越高、目标反间谍越弱，成功率越高
    Fixed ci = counterIntelOf(st, target);
    Fixed success = fxClamp(n->infiltration * Fixed::raw(1400) - ci * Fixed::pct(70), Fixed::pct(15),
                            Fixed::pct(95));

    e->treasury -= Fixed(upkeep);
    if (e->isPlayer) st.market.margin.cash = e->treasury;
    n->infiltration = fxMax(Fixed(0), n->infiltration - spyMissionCost(m));
    n->missionsRun += 1;
    n->lastMissionTick = static_cast<u32>(st.tick);
    e->spy.totalMissions += 1;
    // 执行任务本身会显著提高暴露
    n->exposure = fxClamp(n->exposure + Fixed::pct(10) + Fixed::pct(12) * (Fixed(1) - success), Fixed(0),
                          Fixed(1));

    const bool ok = st.rng.chance(RngStream::Plot, success);
    std::string detail;
    if (!ok) {
        detail = "任务失败";
        // 失败会额外增加暴露
        n->exposure = fxClamp(n->exposure + Fixed::pct(12), Fixed(0), Fixed(1));
    } else {
        switch (m) {
            case SpyMission::Recon: {
                Fixed v = t->military + t->economy;
                n->intelValue += v;
                detail = "获得情报：国力 " + fixedStr(t->score, 1) + "，军力 " + fixedStr(t->military, 0) +
                         "，经济 " + fixedStr(t->economy, 0);
                break;
            }
            case SpyMission::StealTech: {
                Fixed gain = Fixed(300) + Fixed(static_cast<i64>(t->tech.completed.size())) * Fixed(20);
                e->tech.progress[0] += gain;   // 汇入物理分支
                n->intelValue += gain;
                detail = "窃得研究资料：研究点 +" + fixedStr(gain, 0);
                break;
            }
            case SpyMission::Sabotage: {
                Fixed dmg = t->economy * Fixed::pct(6);
                t->economy = fxMax(Fixed(0), t->economy - dmg);
                t->stability = fxClamp(t->stability - Fixed::pct(4), Fixed(0), Fixed(1));
                n->intelValue += dmg;
                detail = "破坏得手：目标经济 -" + fixedStr(dmg, 0) + "，稳定度 -4%";
                break;
            }
            case SpyMission::FundUnrest: {
                t->domestic.unrest = fxClamp(t->domestic.unrest + Fixed::pct(9), Fixed(0), Fixed(1));
                detail = "煽动成功：目标民怨 +9%";
                break;
            }
            case SpyMission::SleeperCell: {
                n->infiltration = fxClamp(n->infiltration + Fixed::pct(18), Fixed(0), Fixed(1));
                detail = "潜伏就位：渗透度 +18%";
                break;
            }
            case SpyMission::Ideological: {
                // 意识形态渗透：向目标社会注入你的伦理，压力长期累积。
                // 它不会立刻见效，但持续施压会改变对方的派系格局乃至伦理。
                Fixed push = Fixed::pct(12) + n->infiltration * Fixed::pct(18);
                t->ideologyPressure[empire] =
                    fxClamp(t->ideologyPressure[empire] + push, Fixed(0), Fixed(1));
                detail = "宣传网络铺开：意识形态压力 +" +
                         fixedStrPlain(push * Fixed(100), 0) + "%（现 " +
                         fixedStrPlain(t->ideologyPressure[empire] * Fixed(100), 0) + "%）";
                break;
            }
            case SpyMission::CounterIntel: {
                // 反渗透是对**本国**执行的：清剿境内的外国网络。
                // 这里 target 即本国，故遍历所有指向该国的外国网络。
                std::string hits;
                int cleared = 0;
                for (auto& other : st.empires) {
                    if (other.id == empire) continue;
                    for (auto& fn2 : other.spy.networks) {
                        if (fn2.target != target || fn2.burned) continue;
                        fn2.infiltration = fxMax(Fixed(0), fn2.infiltration - Fixed::pct(25));
                        fn2.exposure = fxClamp(fn2.exposure + Fixed::pct(30), Fixed(0), Fixed(1));
                        ++cleared;
                        if (!hits.empty()) hits += "、";
                        hits += other.name;
                    }
                }
                if (cleared == 0) {
                    detail = "未发现境内有外国网络";
                } else {
                    detail = "清剿了 " + std::to_string(cleared) + " 个外国网络（" + hits +
                             "）：其渗透度 -25%、暴露 +30%";
                }
                e->counterIntel = fxClamp(e->counterIntel + Fixed::pct(6), Fixed(0), Fixed(1));
                break;
            }
            case SpyMission::FalseFlag: {
                // 假情报：让目标误以为是第三方在对付它，挑动两国关系
                u32 third = kNoEmpire;
                for (const auto& o : st.empires) {
                    if (o.id == empire || o.id == target || !o.alive) continue;
                    third = o.id;
                    break;
                }
                if (third == kNoEmpire) {
                    detail = "无可嫁祸的第三方";
                    break;
                }
                st.relation(target, third).opinion =
                    fxClamp(st.relation(target, third).opinion - Fixed::pct(18), Fixed(-1), Fixed(1));
                st.relation(third, target).opinion =
                    fxClamp(st.relation(third, target).opinion - Fixed::pct(10), Fixed(-1), Fixed(1));
                detail = "假情报奏效：" + st.empires[third].name + " 在 " + t->name +
                         " 眼中的观感 -18%";
                break;
            }
            case SpyMission::Count: break;
        }
    }
    st.logEvent(LogPhase::Clue, "spy.mission",
                e->name + " 对 " + t->name + " 执行【" + std::string(spyMissionName(m)) + "】" +
                    (ok ? "成功" : "失败") + "：" + detail,
                empire);
    if (!ok) {
        if (err) *err = "任务失败（暴露风险上升）";
        return false;
    }
    return true;
}

bool spyDisband(GameState& st, u32 empire, u32 target, std::string* err) {
    Empire* e = st.empire(empire);
    if (e == nullptr) {
        if (err) *err = "非法主体";
        return false;
    }
    auto& nets = e->spy.networks;
    for (auto it = nets.begin(); it != nets.end(); ++it) {
        if (it->target != target || it->burned) continue;
        e->spy.agentPool += it->agents;
        nets.erase(it);
        st.logEvent(LogPhase::Clue, "spy.disband",
                    e->name + " 撤回了在 " + std::string(st.empire(target) ? st.empire(target)->name : "?") +
                        " 的情报网络",
                    empire);
        return true;
    }
    if (err) *err = "在该国没有活动中的网络";
    return false;
}

void spyPhase(GameState& st) {
    for (auto& e : st.empires) {
        if (!e.alive) continue;
        const Fixed power = infiltrationPowerOf(st, e.id);
        for (auto& n : e.spy.networks) {
            if (n.burned) continue;
            const Empire* t = st.empire(n.target);
            if (t == nullptr || !t->alive) {
                n.burned = true;
                continue;
            }
            const Fixed ci = counterIntelOf(st, n.target);
            // 破获判定放在最前：暴露度在上一次结算中已经触顶 ⇒ 本季被捣毁。
            // 早期把判定放在降温之后，暴露 100% 的网络会被同季降温「救回来」，
            // 导致破获几乎不会发生。
            if (n.exposure.rawValue() >= Fixed(1).rawValue()) {
                n.burned = true;
                // 破获后**保留一段时间**再清出列表：否则防御方根本看不到
                // 境内曾有外国网络，反渗透既无从决策、也无从验证效果
                //（实测网络在 20 季内被破获并立即消失）。这里记下时点，
                // 由下方的清理逻辑在 12 季后移除。
                if (n.burnedTick == 0) n.burnedTick = st.tick;
                n.timesBurned += 1;
                e.spy.totalBurned += 1;
                e.spy.agentPool += n.agents;
                e.creditRating = fxClamp(e.creditRating - Fixed::pct(8), Fixed(0), Fixed(1));
                st.relation(e.id, n.target).opinion =
                    fxClamp(st.relation(e.id, n.target).opinion - Fixed::pct(25), Fixed(-1), Fixed(1));
                st.relation(n.target, e.id).opinion =
                    fxClamp(st.relation(n.target, e.id).opinion - Fixed::pct(25), Fixed(-1), Fixed(1));
                st.logEvent(LogPhase::Clue, "spy.burned",
                            "【间谍网被破获】" + e.name + " 在 " +
                                std::string(st.empire(n.target) ? st.empire(n.target)->name : "?") +
                                " 的网络被捣毁，声望与关系受损",
                            e.id);
                continue;
            }
            // 渗透增长：特工数 × 能力 × (1 − 目标反间谍)
            Fixed growth = power * Fixed(n.agents) * Fixed::pct(5) * (Fixed(1) - ci * Fixed::pct(80));
            if (growth.rawValue() < 0) growth = Fixed(0);
            n.infiltration = fxClamp(n.infiltration + growth, Fixed(0), Fixed(1));
            // 暴露：渗透越深、特工越多，越容易被发现；但**不执行任务时会自然降温**。
            //
            // 早期实现只有单向累积，导致暴露度必然达到 100% ——
            // 网络在来得及发挥作用之前就被破获，玩家没有任何管理手段。
            // 正确模型：安静的网络的暴露会收敛到一个可控水平，
            // 而每次任务都会带来暴露尖峰，这才是真正的取舍。
            // 暴露随「渗透深度 × 特工数量 × 对方反间谍」上升：
            // 派的人越多越容易被注意到，这是增派特工的真实代价。
            Fixed agentFactor = Fixed(1) + Fixed::pct(15) * Fixed(n.agents - 1);
            Fixed expo = n.infiltration * Fixed::bp(150) * (Fixed::pct(40) + ci * Fixed::raw(1500)) *
                         agentFactor;
            // 距上次任务越久，降温越充分
            const bool quiet = (st.tick > static_cast<u64>(n.lastMissionTick) + 3) || n.missionsRun == 0;
            // 降温速率决定「活跃网络」的寿命：太慢则暴露永不累积（网络无敌），
            // 太快则网络来不及发挥作用就被破获。经实测取 0.35%/季。
            Fixed cool = quiet ? Fixed::pct(1) : Fixed(0);
            n.exposure = fxClamp(n.exposure + expo - cool, Fixed(0), Fixed(1));

        }
        // 清理被破获的网络：保留 12 季供防御方查看，之后移除。
        // 立即移除会让「反渗透」这条线完全没有反馈 —— 玩家看不到
        // 境内曾有外国网络，也无从判断自己的反间谍投入是否有效。
        constexpr u64 kBurnedRetention = 12;
        e.spy.networks.erase(
            std::remove_if(e.spy.networks.begin(), e.spy.networks.end(),
                           [&](const SpyNetwork& n) {
                               if (!n.burned) return false;
                               if (n.burnedTick == 0) return true;   // 无时点记录：立即清理
                               return st.tick >= n.burnedTick + kBurnedRetention;
                           }),
            e.spy.networks.end());
    }
}

void spyAiPhase(GameState& st) {
    if (st.tick % 3 != 0) return;
    for (auto& e : st.empires) {
        if (!e.alive || e.isPlayer) continue;
        // 已有网络则维持并伺机行动
        for (auto& n : e.spy.networks) {
            const Empire* t = st.empire(n.target);
            if (t == nullptr || !t->alive) continue;
            // 暴露过高时撤回部分特工以降低风险
            if (n.exposure.rawValue() > Fixed::pct(70).rawValue() && n.agents > 1) {
                (void)spyAssignAgents(st, e.id, n.target, -1, nullptr);
                continue;
            }
            // 渗透足够且关系不佳时，优先破坏或煽动
            if (n.infiltration.rawValue() >= spyMissionMinInfiltration(SpyMission::Sabotage).rawValue() &&
                st.relation(e.id, n.target).opinion.rawValue() < 0) {
                (void)spyRunMission(st, e.id, n.target, SpyMission::Sabotage, nullptr);
                continue;
            }
            if (n.infiltration.rawValue() >= spyMissionMinInfiltration(SpyMission::StealTech).rawValue()) {
                (void)spyRunMission(st, e.id, n.target, SpyMission::StealTech, nullptr);
                continue;
            }
            if (n.infiltration.rawValue() >= spyMissionMinInfiltration(SpyMission::Recon).rawValue()) {
                (void)spyRunMission(st, e.id, n.target, SpyMission::Recon, nullptr);
            }
        }
        // 没有网络时尝试建立（选择威胁最大的对手）
        if (e.spy.agentPool > 0 && e.spy.networks.empty()) {
            u32 best = kNoEmpire;
            Fixed bestScore = Fixed(0);
            for (const auto& o : st.empires) {
                if (o.id == e.id || !o.alive) continue;
                Fixed score = o.military + o.economy / Fixed(2);
                if (score.rawValue() > bestScore.rawValue()) {
                    bestScore = score;
                    best = o.id;
                }
            }
            if (best != kNoEmpire) (void)spyEstablish(st, e.id, best, nullptr);
        }
    }
}

std::string spyNetworkText(const GameState& st, u32 empire, u32 target) {
    const SpyNetwork* n = findNetC(st, empire, target);
    const Empire* t = st.empire(target);
    std::string out;
    if (n == nullptr) {
        out = "  " + std::string(t ? t->name : "?") + "：无活动中的网络\n";
        return out;
    }
    out = "  " + padRight(t ? t->name : "?", 14) + " 渗透度 " + padLeft(fixedStrPlain(n->infiltration * Fixed(100), 0) + "%", 5) +
          "  暴露 " + padLeft(fixedStrPlain(n->exposure * Fixed(100), 0) + "%", 5) + "  特工 " +
          std::to_string(n->agents) + "  任务 " + std::to_string(n->missionsRun) + " 次\n";
    return out;
}

std::string spyAgencyText(const GameState& st, u32 empireId) {
    const Empire* e = st.empire(empireId);
    if (e == nullptr) return "非法主体\n";
    const SpyAgency& a = e->spy;
    std::string out;
    out += "  特工总数 " + std::to_string(a.totalAgents) + "   空闲 " + std::to_string(a.agentPool) +
           "   已派驻 " + std::to_string(deployedAgents(*e)) + "\n";
    out += "  渗透能力 " + fixedStrPlain(infiltrationPowerOf(st, empireId) * Fixed(100), 0) +
           "%   累计任务 " + std::to_string(a.totalMissions) + "   被破获 " + std::to_string(a.totalBurned) +
           "\n\n";
    if (a.networks.empty()) {
        out += "  （没有活动中的网络）\n";
    } else {
        out += style("活动中的网络", Style::Sub) + "\n";
        for (const auto& n : a.networks) out += spyNetworkText(st, empireId, n.target);
    }
    return out;
}


// ---------------------------------------------------------------------------
// 意识形态渗透的长期效果
// ---------------------------------------------------------------------------

Fixed ideologyPressureOf(const GameState& st, u32 target, u32 source) {
    const Empire* t = st.empire(target);
    if (t == nullptr || source >= kMaxEmpires) return Fixed(0);
    return t->ideologyPressure[source];
}

void ideologyPhase(GameState& st) {
    for (auto& t : st.empires) {
        if (!t.alive) continue;
        for (u32 src = 0; src < kMaxEmpires; ++src) {
            Fixed& p = t.ideologyPressure[src];
            if (p.rawValue() <= 0) continue;
            const Empire* s = st.empire(src);
            if (s == nullptr || !s->alive) {
                p = Fixed(0);
                continue;
            }
            // 压力每季自然衰减 3%：渗透是持续投入，停止就消退
            p = fxClamp(p - Fixed::pct(3), Fixed(0), Fixed(1));

            // 压力 ≥ 40%：开始影响派系格局
            if (p.rawValue() >= Fixed::pct(40).rawValue()) {
                Fixed intensity = (p - Fixed::pct(40)) / Fixed::pct(60);   // 0..1
                for (auto& f : t.domestic.factions) {
                    // 与渗透方伦理相近的派系受益，相远的受损。
                    // 这里用「派系类型 × 渗透方政体」做粗略对齐判断：
                    // 军部/贵族亲近威权，劳工/民粹亲近平等，商会亲近自由。
                    bool aligned = false;
                    switch (s->government) {
                        case 2: case 3: case 5: case 8: case 9: case 11:   // 威权
                            aligned = (f.kind == FactionKind::Military ||
                                       f.kind == FactionKind::Nobility ||
                                       f.kind == FactionKind::Fundamentalist);
                            break;
                        default:   // 民主/共和
                            aligned = (f.kind == FactionKind::Labor ||
                                       f.kind == FactionKind::Populist ||
                                       f.kind == FactionKind::Merchant);
                            break;
                    }
                    Fixed delta = intensity * Fixed::pct(2);
                    f.satisfaction = fxClamp(f.satisfaction + (aligned ? delta : -delta), Fixed(0), Fixed(1));
                    f.influence = fxClamp(f.influence + (aligned ? delta : -delta) / Fixed(2), Fixed(0), Fixed(1));
                }
                // 民怨随之上升（社会撕裂）
                t.domestic.unrest = fxClamp(t.domestic.unrest + intensity * Fixed::pct(1), Fixed(0), Fixed(1));
            }

            // 压力 ≥ 75%：伦理被改写 —— 目标采纳渗透方的一项伦理
            if (p.rawValue() >= Fixed::pct(75).rawValue()) {
                bool has = false;
                for (u8 e : t.ethics)
                    if (e == s->ethics.front()) has = true;
                if (!has) {
                    // ethics 是定长 3 的数组：替换最后一项（视为「最弱的一环」）
                    t.ethics.back() = s->ethics.front();
                    st.logEvent(LogPhase::Domestic, "spy.ideology",
                                t.name + " 的官方伦理在长期渗透下转向（采纳了 " + s->name +
                                    " 的主张）",
                                src);
                    p = Fixed::pct(30);   // 改写后压力回落，需要重新积累
                }
            }
        }
    }
}

bool counterInfiltrate(GameState& st, u32 empire, std::string* msg) {
    Empire* e = st.empire(empire);
    if (e == nullptr) {
        if (msg) *msg = "非法主体";
        return false;
    }
    // 直接调用任务执行路径需要先有本国网络，这里做成独立入口：
    // 花影响力与国库扫荡境内所有外国网络。
    const Fixed cost = Fixed(15000);
    if (e->treasury.rawValue() < cost.rawValue()) {
        if (msg) *msg = "国库不足：反渗透行动需要 " + fixedStr(cost, 0) + " cr";
        return false;
    }
    e->treasury -= cost;
    if (e->isPlayer) st.market.margin.cash = e->treasury;
    int cleared = 0;
    std::string names;
    for (auto& other : st.empires) {
        if (other.id == empire || !other.alive) continue;
        for (auto& n : other.spy.networks) {
            if (n.target != empire || n.burned) continue;
            n.infiltration = fxMax(Fixed(0), n.infiltration - Fixed::pct(30));
            n.exposure = fxClamp(n.exposure + Fixed::pct(35), Fixed(0), Fixed(1));
            ++cleared;
            if (!names.empty()) names += "、";
            names += other.name;
        }
    }
    e->counterIntel = fxClamp(e->counterIntel + Fixed::pct(8), Fixed(0), Fixed(1));
    if (msg) {
        if (cleared == 0)
            *msg = "反渗透行动完成：境内未发现外国网络（反间谍 +8%）";
        else
            *msg = "反渗透行动完成：清剿 " + std::to_string(cleared) + " 个外国网络（" + names +
                   "）——渗透度 -30%、暴露 +35%（反间谍 +8%）";
    }
    st.logEvent(LogPhase::Plot, "spy.counter", e->name + " 实施反渗透行动", empire);
    return true;
}

std::string counterIntelReport(const GameState& st, u32 empire) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return "非法主体\n";
    std::string out;
    TextTable t;
    t.header({"渗透方", "渗透度", "暴露", "任务数", "状态"});
    int n = 0;
    for (const auto& other : st.empires) {
        if (other.id == empire || !other.alive) continue;
        for (const auto& net : other.spy.networks) {
            if (net.target != empire) continue;
            ++n;
            t.row({other.name, fixedStrPlain(net.infiltration * Fixed(100), 0) + "%",
                   fixedStrPlain(net.exposure * Fixed(100), 0) + "%",
                   std::to_string(net.missionsRun),
                   net.burned ? "已破获" : (net.exposure.rawValue() > Fixed::pct(70).rawValue() ? "即将暴露" : "活动中的")});
        }
    }
    if (n == 0) return "  （境内没有发现外国间谍网络）\n";
    out += t.render();
    out += "  用 `greyfall spy --counter` 实施反渗透（15,000 cr）。\n";
    return out;
}

}  // namespace gf
