#include <cstdio>
#include <cstdlib>

#include "core/ResolutionEngine.h"

#include <algorithm>

#include "core/TickPipeline.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {
namespace {

Fixed applyNumericDelta(Empire& e, ResTarget t, Fixed v) {
    switch (t) {
        case ResTarget::Treasury: {
            Fixed d = v;
            e.treasury += d;
            return d;
        }
        case ResTarget::Influence:
            e.influence = fxMax(Fixed(0), e.influence + v);
            break;
        case ResTarget::Unity:
            e.unity = fxMax(Fixed(0), e.unity + v);
            break;
        case ResTarget::Military:
            e.military = fxMax(Fixed(0), e.military + v);
            break;
        case ResTarget::Economy:
            e.economy = fxMax(Fixed(0), e.economy + v);
            break;
        case ResTarget::Capacity:
            for (auto& c : e.capacity) c = fxMax(Fixed(0), c + v);
            break;
        case ResTarget::FleetPower:
            // 舰队战力为聚合量，直接作用于军力字段
            e.military = fxMax(Fixed(0), e.military + v);
            break;
        default:
            break;
    }
    return v;
}

void pushEffect(Empire& e, u16 defId, ResTarget t, Fixed v, int duration, const std::string& src) {
    ActiveEffect a;
    a.defId = defId;
    a.target = t;
    a.value = v;
    a.ticksLeft = duration > 0 ? duration : -1;
    a.positive = resEffectIsPositive(t, v);
    a.source = src;
    e.resolutions.active.push_back(std::move(a));
}

/// 施加一个效果：修正类挂持久修正；数值类立即结算，若 duration>0 再挂持续增量
void applyEffect(GameState& st, Empire& e, u16 defId, const ResEffect& ef, int duration,
                 const std::string& src) {
    if (ef.target == ResTarget::Count || ef.value.rawValue() == 0) return;
    ModKind mk = resTargetToMod(ef.target);
    if (mk != ModKind::Count) {
        pushEffect(e, defId, ef.target, ef.value, duration, src);
        return;
    }
    if (duration > 0) {
        pushEffect(e, defId, ef.target, ef.value, duration, src);
    } else {
        applyNumericDelta(e, ef.target, ef.value);
        if (e.isPlayer) st.market.margin.cash = e.treasury;
    }
}

}  // namespace

Fixed resolutionModifier(const Empire& e, ModKind kind) {
    Fixed acc = Fixed(0);
    for (const auto& a : e.resolutions.active) {
        if (resTargetToMod(a.target) != kind) continue;
        acc += a.value;
    }
    return acc;
}

bool hasResolution(const Empire& e, u16 defId) {
    for (const auto& a : e.resolutions.active) {
        if (a.defId == defId && a.ticksLeft != 0) return true;
    }
    for (const auto& c : e.resolutions.countdowns) {
        if (c.defId == defId && !c.completed && !c.failed) return true;
    }
    return false;
}

void resolutionPhase(GameState& st, TickReport& rep) {
    for (auto& e : st.empires) {
        if (!e.alive) continue;

        // ---- 1) 应用持续效果并递减计时 ----
        //
        // 注意：这里**不设国库下限保护**。曾经加过一层「不得低于 -2 季收入」
        // 的兜底，用来掩盖「离散资源损失被按 duration 重复施加」的缺陷；
        // 根因已在激活路径修正（见下方 Auto 决议处），兜底只会让
        // 一个刻意注入大额支出的结算顺序测试失真。
        // 现在行为是确定的：活跃决议按其声明的数值逐季施加，玩家可预期。
        for (auto& a : e.resolutions.active) {
            if (a.ticksLeft == 0) continue;
            if (resTargetToMod(a.target) == ModKind::Count) {
                applyNumericDelta(e, a.target, a.value);
                if (e.isPlayer) st.market.margin.cash = e.treasury;
            }
            if (a.ticksLeft > 0) --a.ticksLeft;
        }
        // 移除已过期（数值类过期后不再产生增量；修正类由聚合函数过滤）
        e.resolutions.active.erase(
            std::remove_if(e.resolutions.active.begin(), e.resolutions.active.end(),
                           [](const ActiveEffect& a) { return a.ticksLeft == 0; }),
            e.resolutions.active.end());

        auto alreadyTriggered = [&e](u16 id) {
            return std::find(e.resolutions.triggered.begin(), e.resolutions.triggered.end(), id) !=
                   e.resolutions.triggered.end();
        };

        // ---- 2) 自动触发 / 可阻止 / 倒计时启动 ----
        for (int i = 0; i < kResolutionCount; ++i) {
            const ResolutionDef& d = resolutionDef(i);
            if (d.kind == ResolutionKind::Active || d.kind == ResolutionKind::Tradeoff) continue;
            u16 id = static_cast<u16>(i);
            if (alreadyTriggered(id)) continue;
            // 条件不满足 ⇒ 不触发
            if (!resConditionMet(st, e, d.trigger)) continue;
            // 互斥与阻止：自动触发的决议同样受约束 ——
            // 否则「被秘密警察阻止的公民庆典」仍会自行出现。
            if (!resCanActivate(st, e.id, i, nullptr)) continue;
            // Preventable：阻止条件满足 ⇒ 不触发（并记录已阻止）
            if (d.kind == ResolutionKind::Preventable && resConditionMet(st, e, d.prevent)) {
                e.resolutions.triggered.push_back(id);
                if (e.isPlayer) {
                    st.logEvent(LogPhase::Event, "resolution.prevented",
                                "已阻止【" + std::string(d.nameZh) + "】：阻止条件满足", e.id);
                }
                continue;
            }
            e.resolutions.triggered.push_back(id);

            if (d.kind == ResolutionKind::Countdown) {
                CountdownState c;
                c.defId = id;
                c.ticksLeft = d.countdownTicks;
                c.target = d.onComplete.target;
                c.goal = d.onComplete.value;
                e.resolutions.countdowns.push_back(c);
                if (e.isPlayer) {
                    st.logEvent(LogPhase::Event, "resolution.countdown",
                                "【" + std::string(d.nameZh) + "】启动：" + std::to_string(d.countdownTicks) +
                                    " 季内达成目标可获增益，否则承受惩罚",
                                e.id);
                }
                rep.eventsFired += 1;
                continue;
            }

            // Auto / Preventable：立即生效。
            //
            // **离散资源的损失必须一次性结算，不能按 duration 重复施加。**
            // `duration` 表达的是「修正类效果持续多少季」，而 Treasury /
            // Influence / Unity / Military / Economy 是**存量型数值**。
            // 旧写法把 duration 一并传给数值类效果，于是「面包暴动」这条
            // 灾难决议（Treasury -8,000）会连扣 24 季 = 192,000 cr，
            // 对季度收入只有 210 cr 的帝国等同于直接宣判死刑。
            // 实测玩家在 t=35~57 每季固定流失 8,000 cr，来源就是它。
            // 现在：离散资源一次性扣完；修正类（ResStability / ResTrade /
            // ResBuild / ResMilitary / ResResearch / ResUnrest）保留持续期。
            std::string src(d.nameZh);
            applyEffect(st, e, id, d.onActivate, 0, src);
            if (d.duration > 0) applyEffect(st, e, id, d.onTick, d.duration, src);
            if (e.isPlayer) {
                bool good = resEffectIsPositive(d.onActivate.target, d.onActivate.value);
                st.logEvent(LogPhase::Event, "resolution.triggered",
                            std::string(good ? "【增益】" : "【减益】") + std::string(d.nameZh) + "：" +
                                resEffectText(d.onActivate) +
                                (d.duration > 0 ? ("（持续 " + std::to_string(d.duration) + " 季）") : ""),
                            e.id, d.onActivate.value);
            }
            rep.eventsFired += 1;
        }

        // ---- 3) 推进倒计时 ----
        for (auto& c : e.resolutions.countdowns) {
            if (c.completed || c.failed) continue;
            const ResolutionDef& d = resolutionDef(c.defId);
            // 目标达成判定：修正类看聚合值，数值类看当前值
            bool reached = false;
            ModKind mk = resTargetToMod(c.target);
            if (mk != ModKind::Count) {
                reached = resolutionModifier(e, mk).rawValue() >= c.goal.rawValue();
            } else {
                switch (c.target) {
                    case ResTarget::Military: reached = e.military.rawValue() >= c.goal.rawValue(); break;
                    case ResTarget::Treasury: reached = e.treasury.rawValue() >= c.goal.rawValue(); break;
                    case ResTarget::Influence: reached = e.influence.rawValue() >= c.goal.rawValue(); break;
                    case ResTarget::Unity: reached = e.unity.rawValue() >= c.goal.rawValue(); break;
                    case ResTarget::Economy: reached = e.economy.rawValue() >= c.goal.rawValue(); break;
                    default: break;
                }
            }
            // 稳定度 / 民怨这类基础属性直接看帝国字段
            if (c.target == ResTarget::ResStability) reached = e.stability.rawValue() >= c.goal.rawValue();
            if (c.target == ResTarget::ResUnrest) reached = e.domestic.unrest.rawValue() <= c.goal.rawValue();
            if (c.target == ResTarget::ResTrade) reached = resolutionModifier(e, ModKind::TradeMargin).rawValue() >= c.goal.rawValue();
            if (c.target == ResTarget::ResBuild) reached = resolutionModifier(e, ModKind::BuildRate).rawValue() >= c.goal.rawValue();
            if (c.target == ResTarget::ResMilitary) reached = resolutionModifier(e, ModKind::MilitaryPower).rawValue() >= c.goal.rawValue();
            if (c.target == ResTarget::ResResearch) reached = resolutionModifier(e, ModKind::ResearchRate).rawValue() >= c.goal.rawValue();

            if (reached) {
                c.completed = true;
                e.resolutions.completed.push_back(c.defId);
                std::string src(d.nameZh);
                applyEffect(st, e, c.defId, d.onComplete, 0, src);
                if (e.isPlayer) {
                    st.logEvent(LogPhase::Event, "resolution.completed",
                                "【决议达成】" + std::string(d.nameZh) + " → " + resEffectText(d.onComplete),
                                e.id, d.onComplete.value);
                }
                rep.eventsFired += 1;
                continue;
            }
            --c.ticksLeft;
            if (c.ticksLeft <= 0) {
                c.failed = true;
                e.resolutions.failed.push_back(c.defId);
                // 失败惩罚的**持续时间必须与惩罚量级分开考虑**。
                //
                // 旧写法把惩罚持续 `d.countdownTicks` 季，而惩罚本身往往是
                // 一次性口径的金额（例如「基建攻坚」失败 = 国库 -12,000）。
                // 两者相乘就变成 8 × 12,000 = 96,000 —— 一条启动成本只有
                // 6,000 cr 的决议，失败却要付出 16 倍的代价。
                // 实测玩家因此在 t=18 起每季固定流失 12,000 cr，
                // 25 季内从 12.4 万跌到破产，而 lastIncome 始终为正，
                // 玩家完全看不到钱去了哪里。
                //
                // 修正：国库/影响力这类**离散资源**的惩罚一次性结算；
                // 其余（修正类、稳定度等）保留有限持续期，避免永久退化。
                const bool lumpSum = (d.onFail.target == ResTarget::Treasury ||
                                      d.onFail.target == ResTarget::Influence);
                std::string src(d.nameZh);
                applyEffect(st, e, c.defId, d.onFail, lumpSum ? 0 : d.countdownTicks, src);
                if (e.isPlayer) {
                    st.logEvent(LogPhase::Event, "resolution.failed",
                                "【决议失败】" + std::string(d.nameZh) + " 超时 → " + resEffectText(d.onFail),
                                e.id, d.onFail.value);
                }
                rep.eventsFired += 1;
            }
        }
        e.resolutions.countdowns.erase(
            std::remove_if(e.resolutions.countdowns.begin(), e.resolutions.countdowns.end(),
                           [](const CountdownState& c) { return c.completed || c.failed; }),
            e.resolutions.countdowns.end());

        // 玩家国库与市场现金保持同步
        if (e.isPlayer) st.market.margin.cash = e.treasury;
    }
}

namespace {

/// AI 对决议的偏好分：由处境与伦理决定
i64 aiResolutionPreference(const GameState& st, const Empire& e, const ResolutionDef& d) {
    i64 score = 0;
    // 正面效果加分，负面效果减分（ResEffect 是单个目标+数值）。
    // 量纲必须区分：修正类目标是「比例」（Fixed::pct(8) 的 raw 仅 80），
    // 标量类目标是「数量」（国库 25000 的 raw 是 25,000,000）。
    // 早期统一除以 100，导致比例类效果的整数除法结果恒为 0，AI 永远不发起决议。
    auto weigh = [&](const ResEffect& ef) {
        if (ef.target == ResTarget::Count) return;
        bool positive = resEffectIsPositive(ef.target, ef.value);
        const bool isModifier = (resTargetToMod(ef.target) != ModKind::Count);
        i64 mag = isModifier ? (fxAbs(ef.value).rawValue() / 10)
                             : (fxAbs(ef.value).rawValue() / FIX / 100);
        score += positive ? mag : -mag;
    };
    weigh(d.onActivate);
    if (d.duration > 0) weigh(d.onTick);
    // 持续期越长越值得做：4 季的决议在一场 200 季的对局里几乎是噪声，
    // 而「牺牲换利」类的永久效果会持续整个后半局。
    if (d.kind == ResolutionKind::Tradeoff) score += 30;   // 永久生效
    else score += static_cast<i64>(d.duration) * 2;         // 按持续季数加权
    if (d.cost.sacrificeTarget != ResTarget::Count) {
        score -= static_cast<i64>(fxAbs(d.cost.sacrificeValue).rawValue() / 5);
    }
    // 处境调整
    if (e.domestic.unrest.rawValue() > Fixed::pct(40).rawValue()) {
        if (d.onActivate.target == ResTarget::ResUnrest ||
            d.onActivate.target == ResTarget::ResStability)
            score += 40;
    }
    if (e.treasury.rawValue() < Fixed(30000).rawValue()) {
        if (d.cost.credits > 20000) score -= 40;
        if (d.onActivate.target == ResTarget::Treasury) score += 50;
    }
    // 战争时期偏好军事决议
    bool atWar = false;
    for (const auto& r : st.relations) {
        std::size_t idx = static_cast<std::size_t>(&r - st.relations.data());
        if (idx / kMaxEmpires == e.id && r.atWar) atWar = true;
    }
    if (atWar && d.onActivate.target == ResTarget::ResMilitary) score += 35;
    return score;
}

}  // namespace

void resolutionAiPhase(GameState& st) {
    for (auto& e : st.empires) {
        if (e.isPlayer || !e.alive) continue;
        // 错开节奏，且每季最多发起一项。
        // 频率必须与决议的持续时间匹配：多数决议只持续 4 季，
        // 每 6 季才决策一次意味着同时最多只有约 2/3 个槽位被占用 ——
        // 实测 AI 生效决议长期停在 2.1 项（6 个互斥组的上限）。
        // 改为每 3 季决策一次，使槽位能被持续填满。
        if ((st.tick + e.id) % 3 != 0) continue;
        int best = -1;
        i64 bestScore = 0;
        for (int i = 0; i < kResolutionCount; ++i) {
            const ResolutionDef& d = resolutionDef(i);
            if (d.kind != ResolutionKind::Active && d.kind != ResolutionKind::Tradeoff) continue;
            // 已触发 / 已完成 / 正在生效 ⇒ 跳过。
            //
            // 「已触发即跳过」这道卫兵**不可移除**：Active 决议若带数值型效果
            //（Treasury / Influence / Military …），会在生效期内**每 tick 永久累加**。
            // 一旦允许重复发起，效果就会复利叠加 ——
            // 实测移除该卫兵后，AI 合计国库在 240 季内从 170 万暴涨到 1887 万、
            // 平均科技冲到 48/96 项，经济彻底失控。
            // AI 的生效决议稳定在 2.1 项（上限 6）是这一有限决策集的正常表现。
            u16 id = static_cast<u16>(i);
            if (std::find(e.resolutions.triggered.begin(), e.resolutions.triggered.end(), id) !=
                e.resolutions.triggered.end())
                continue;
            if (std::find(e.resolutions.completed.begin(), e.resolutions.completed.end(), id) !=
                e.resolutions.completed.end())
                continue;
            if (hasResolution(e, id)) continue;
            // 前置条件与代价
            if (!resConditionMet(st, e, d.require)) continue;
            if (e.treasury.rawValue() < Fixed(d.cost.credits).rawValue()) continue;
            if (e.influence.rawValue() < Fixed(d.cost.influence).rawValue()) continue;
            if (e.apLeft < d.cost.ap) continue;
            // ---- 持续代价的支付能力闸门（必须有，否则 AI 会自杀）----
            //
            // Active 决议的数值型效果会**按季重复施加**。一条
            // `Treasury -12,000 / 季` 的决议，对一个季度收入只有 210 cr 的帝国
            // 就是每季 57 倍的收入缺口 —— 实测玩家（AI 代管决议）因此在 t=18 起
            // 每季固定流失 12,000 cr，25 季内从 12.4 万跌到破产，
            // 而 `lastIncome` 始终为正，玩家完全看不到钱去了哪里
            //（流失发生在 resolutionPhase，不在 economyPhase）。
            // 规则：单季持续代价不得超过「本季收入 + 国库存量的 2%」。
            {
                Fixed recurring = Fixed(0);
                if (d.onActivate.target == ResTarget::Treasury && d.onActivate.value.rawValue() < 0)
                    recurring += -d.onActivate.value;
                Fixed budget = fxMax(e.lastIncome, Fixed(0)) + e.treasury * Fixed::pct(2);
                if (recurring.rawValue() > budget.rawValue()) continue;
            }
            // 互斥与依赖链
            if (!resCanActivate(st, e.id, i, nullptr)) continue;
            i64 sc = aiResolutionPreference(st, e, d);
            // 已经做过的决议降权：过期后它可以被重新发起（未进入 completed），
            // 于是 AI 会反复挑同一个「评分最高」的项，其余互斥组永不铺开 ——
            // 实测 AI 生效决议长期停在 2.1 项（上限 6）。
            // 降权促使它尝试尚未做过的方向。
            {
                u16 rid = static_cast<u16>(i);
                if (std::find(e.resolutions.triggered.begin(), e.resolutions.triggered.end(), rid) !=
                    e.resolutions.triggered.end())
                    sc -= 40;
            }
            if (sc > bestScore) {
                bestScore = sc;
                best = i;
            }
        }
        // 阈值与决议定价匹配：定价下调后，门槛同步放宽，
        // 否则 AI 仍不会主动发起（实测生效决议长期只有自动触发的 3 项）。
        if (best < 0 || bestScore < 25) continue;
        (void)resolutionActivate(st, e.id, static_cast<u16>(best), nullptr);
    }
}


bool resolutionActivate(GameState& st, u32 empireId, u16 defId, std::string* err) {
    Empire* e = st.empire(empireId);
    if (e == nullptr) {
        if (err) *err = "非法主体";
        return false;
    }
    if (defId >= kResolutionCount) {
        if (err) *err = "非法决议编号";
        return false;
    }
    const ResolutionDef& d = resolutionDef(defId);
    if (d.kind != ResolutionKind::Active && d.kind != ResolutionKind::Tradeoff) {
        if (err) *err = "【" + std::string(d.nameZh) + "】不是主动决议（类型：" +
                        std::string(resolutionKindName(d.kind)) + "）";
        return false;
    }
    if (hasResolution(*e, defId)) {
        if (err) *err = "【" + std::string(d.nameZh) + "】已在生效中";
        return false;
    }
    if (std::find(e->resolutions.completed.begin(), e->resolutions.completed.end(), defId) !=
        e->resolutions.completed.end()) {
        if (err) *err = "【" + std::string(d.nameZh) + "】已完成";
        return false;
    }
    if (!resConditionMet(st, *e, d.require)) {
        if (err) *err = "前置条件未满足：" + resConditionText(d.require);
        return false;
    }
    // 互斥组与依赖链：同组冲突 / 缺少前置 / 被其他决议阻止
    {
        std::string why;
        if (!resCanActivate(st, e->id, defId, &why)) {
            if (err) *err = why;
            return false;
        }
    }
    // 代价校验
    if (e->treasury.rawValue() < Fixed(d.cost.credits).rawValue()) {
        if (err) *err = "国库不足：需要 " + groupDigits(d.cost.credits);
        return false;
    }
    if (e->influence.rawValue() < Fixed(d.cost.influence).rawValue()) {
        if (err) *err = "影响力不足：需要 " + groupDigits(d.cost.influence);
        return false;
    }
    if (e->unity.rawValue() < Fixed(d.cost.unity).rawValue()) {
        if (err) *err = "凝聚力不足：需要 " + groupDigits(d.cost.unity);
        return false;
    }
    if (d.cost.commodity >= 0 && d.cost.qty > 0) {
        std::size_t ci = static_cast<std::size_t>(d.cost.commodity);
        if (e->stock[ci].rawValue() < Fixed(d.cost.qty).rawValue()) {
            if (err) *err = std::string("资源不足：需要 ") + std::string(commodityName(d.cost.commodity)) + " " +
                            groupDigits(d.cost.qty);
            return false;
        }
    }
    if (e->apLeft < d.cost.ap) {
        if (err) *err = "行动点不足：需要 " + std::to_string(d.cost.ap);
        return false;
    }

    // 支付代价
    e->treasury -= Fixed(d.cost.credits);
    e->influence -= Fixed(d.cost.influence);
    e->unity -= Fixed(d.cost.unity);
    if (d.cost.commodity >= 0 && d.cost.qty > 0)
        e->stock[static_cast<std::size_t>(d.cost.commodity)] -= Fixed(d.cost.qty);
    e->apLeft -= d.cost.ap;

    std::string src(d.nameZh);
    applyEffect(st, *e, defId, d.onActivate, d.duration, src);
    if (d.duration > 0) applyEffect(st, *e, defId, d.onTick, d.duration, src);

    // 牺牲换利：永久代价
    if (d.kind == ResolutionKind::Tradeoff && d.cost.sacrificeTarget != ResTarget::Count) {
        pushEffect(*e, defId, d.cost.sacrificeTarget, d.cost.sacrificeValue, -1, src + "·代价");
    }

    e->resolutions.triggered.push_back(defId);
    if (e->isPlayer) st.market.margin.cash = e->treasury;
    st.logEvent(LogPhase::Event, "resolution.activate",
                "发起决议【" + std::string(d.nameZh) + "】→ " + resEffectText(d.onActivate) +
                    (d.kind == ResolutionKind::Tradeoff ? "（永久牺牲：" +
                                                              std::string(resTargetName(d.cost.sacrificeTarget)) +
                                                              " " +
                                                              fixedStrSigned(d.cost.sacrificeValue * Fixed(100), 1) +
                                                              "%）"
                                                        : ""),
                empireId, d.onActivate.value);
    return true;
}

std::string resolutionListText(const GameState& st, u32 empireId, bool onlyAvailable) {
    const Empire* e = st.empire(empireId);
    if (e == nullptr) return "非法主体";
    std::string out;
    for (int k = 0; k < static_cast<int>(ResolutionKind::Count); ++k) {
        auto kind = static_cast<ResolutionKind>(k);
        bool header = false;
        for (int i = 0; i < kResolutionCount; ++i) {
            const ResolutionDef& d = resolutionDef(i);
            if (d.kind != kind) continue;
            bool active = hasResolution(*e, static_cast<u16>(i));
            bool done = std::find(e->resolutions.completed.begin(), e->resolutions.completed.end(),
                                  static_cast<u16>(i)) != e->resolutions.completed.end();
            bool triggered = std::find(e->resolutions.triggered.begin(), e->resolutions.triggered.end(),
                                       static_cast<u16>(i)) != e->resolutions.triggered.end();
            if (onlyAvailable) {
                if (kind != ResolutionKind::Active && kind != ResolutionKind::Tradeoff) continue;
                if (active || done) continue;
                if (!resConditionMet(st, *e, d.require)) continue;
                if (!resCanActivate(st, e->id, i, nullptr)) continue;
            }
            if (!header) {
                out += "\n" + style(std::string("── ") + std::string(resolutionKindName(kind)) + " ──", Style::Sub) +
                       "\n";
                header = true;
            }
            std::string state;
            if (active) state = style("[生效中]", Style::Good);
            else if (done) state = style("[已完成]", Style::Good);
            else if (triggered) state = style("[已触发]", Style::Warn);
            else state = "        ";
            out += "  " + padRight(std::string(d.idName), 22) + padRight(std::string(d.nameZh), 12) + state + "  " +
                   wrapJoin(d.desc, 70) + "\n";
        }
    }
    return out;
}

std::string resolutionDetailText(const GameState& st, u32 empireId, u16 defId) {
    const Empire* e = st.empire(empireId);
    if (e == nullptr || defId >= kResolutionCount) return "非法决议";
    const ResolutionDef& d = resolutionDef(defId);
    std::string out;
    out += style("【" + std::string(d.nameZh) + "】", Style::Heading) + "  " +
           std::string(resolutionKindName(d.kind)) + "\n";
    out += wrapJoin(d.desc, 86, "  ") + "\n\n";
    if (d.kind == ResolutionKind::Active || d.kind == ResolutionKind::Tradeoff) {
        out += "  前置条件：" + resConditionText(d.require) + "\n";
        out += "  代价：";
        std::vector<std::string> parts;
        if (d.cost.credits) parts.push_back(groupDigits(d.cost.credits) + " cr");
        if (d.cost.influence) parts.push_back(groupDigits(d.cost.influence) + " 影响力");
        if (d.cost.unity) parts.push_back(groupDigits(d.cost.unity) + " 凝聚力");
        if (d.cost.commodity >= 0 && d.cost.qty)
            parts.push_back(std::string(commodityName(d.cost.commodity)) + " " + groupDigits(d.cost.qty));
        parts.push_back(std::to_string(d.cost.ap) + " AP");
        out += join(parts, " + ") + "\n";
        out += "  效果：" + resEffectText(d.onActivate) + "\n";
        if (d.duration > 0) out += "  持续：" + resEffectText(d.onTick) + "，共 " + std::to_string(d.duration) + " 季\n";
        if (d.kind == ResolutionKind::Tradeoff && d.cost.sacrificeTarget != ResTarget::Count) {
            out += style("  永久牺牲：" + std::string(resTargetName(d.cost.sacrificeTarget)) + " " +
                             fixedStrSigned(d.cost.sacrificeValue * Fixed(100), 1) + "%",
                         Style::Bad) +
                   "\n";
        }
        // 统一用 resCanActivate 判定：它同时覆盖前置条件、互斥组与阻止关系。
        // 早期这里只查 resConditionMet，会与下方的互斥/依赖判定互相矛盾
        //（实测同时输出「可发起：是」与「不可发起：需要先完成…」）。
        std::string why;
        bool ok = resConditionMet(st, *e, d.require) && resCanActivate(st, empireId, defId, &why);
        out += std::string("\n  当前可发起：") + (ok ? style("是", Style::Good) : style("否", Style::Bad));
        if (!ok) {
            if (!why.empty()) out += "（" + why + "）";
            else out += "（" + resConditionText(d.require) + " 未满足）";
        }
        out += "\n";
    } else {
        out += "  触发条件：" + resConditionText(d.trigger) + "\n";
        if (d.kind == ResolutionKind::Preventable)
            out += "  阻止条件：" + resConditionText(d.prevent) + style("（满足即可避免）", Style::Good) + "\n";
        if (d.kind == ResolutionKind::Countdown) {
            out += "  期限：" + std::to_string(d.countdownTicks) + " 季\n";
            out += "  达成目标：" + std::string(resTargetName(d.onComplete.target)) + " ≥ " +
                   fixedStrPlain(d.onComplete.value * Fixed(100), 0) + "%\n";
            out += style("  成功奖励：" + resEffectText(d.onComplete), Style::Good) + "\n";
            out += style("  失败惩罚：" + resEffectText(d.onFail), Style::Bad) + "\n";
        } else {
            bool good = resEffectIsPositive(d.onActivate.target, d.onActivate.value);
            out += std::string("  效果：") + (good ? style(resEffectText(d.onActivate), Style::Good)
                                                   : style(resEffectText(d.onActivate), Style::Bad)) +
                   "\n";
        }
        bool trig = std::find(e->resolutions.triggered.begin(), e->resolutions.triggered.end(), defId) !=
                    e->resolutions.triggered.end();
        out += std::string("\n  状态：") + (trig ? "已触发/已阻止" : "尚未触发") + "\n";
    }

    // ---- 互斥与依赖链 ----
    if (d.exclusionGroup != 0) {
        out += "\n  互斥组：" + std::string(resExclusionGroupName(d.exclusionGroup)) +
               "（同组只能有一项生效）\n";
        for (int i = 0; i < kResolutionCount; ++i) {
            if (i == defId) continue;
            const ResolutionDef& o = resolutionDef(i);
            if (o.exclusionGroup != d.exclusionGroup) continue;
            out += "    · " + std::string(o.nameZh) + "\n";
        }
    }
    if (d.requireCount > 0) {
        out += "\n  前置决议（必须先完成）：\n";
        for (u8 i = 0; i < d.requireCount && i < d.requiresRes.size(); ++i) {
            u16 req = d.requiresRes[i];
            bool done = false;
            if (e != nullptr) {
                done = std::find(e->resolutions.triggered.begin(), e->resolutions.triggered.end(), req) !=
                           e->resolutions.triggered.end() ||
                       std::find(e->resolutions.completed.begin(), e->resolutions.completed.end(), req) !=
                           e->resolutions.completed.end();
            }
            out += "    " + std::string(done ? style("✓", Style::Good) : style("✗", Style::Bad)) + " " +
                   std::string(resolutionDef(req).nameZh) + "\n";
        }
    }
    if (d.blockCount > 0) {
        out += "\n  本决议生效期间阻止：\n";
        for (u8 i = 0; i < d.blockCount && i < d.blocksRes.size(); ++i)
            out += "    · " + std::string(resolutionDef(d.blocksRes[i]).nameZh) + "\n";
    }
    return out;
}

}  // namespace gf
