#include "domain/Corruption.h"
#include "util/Fmt.h"
#include "plot/BeatResolver.h"

#include <algorithm>
#include <array>

#include "ai/AiCore.h"
#include "ai/FactionAI.h"
#include "clue/ClueGraph.h"
#include "core/ResolutionEngine.h"
#include "core/TickPipeline.h"
#include "domain/ModifierUtil.h"
#include "domain/Policy.h"
#include "gen/EmpireGen.h"
#include "gen/EventSchedule.h"
#include "mkt/BlackMarket.h"
#include "mkt/Debt.h"
#include "mkt/Futures.h"
#include "mkt/MarketEngine.h"
#include "mkt/Margin.h"
#include "mkt/OrderBook.h"
#include "rng/Streams.h"
#include "util/Str.h"

namespace gf {
namespace {

std::string eventOptionText(int eventId, int optionIndex) {
    const EventInfo& e = eventInfo(eventId);
    static const char* kRows[3][3] = {
        {"公开处理（提升合法性，但暴露立场）", "秘密处理（保留余地，但风险自担）", "转嫁给第三方（低成本，损信誉）"},
        {"强硬回应（军事/制裁）", "克制回应（外交/让利）", "拖延观望（延后代价上升）"},
        {"全额投入（消耗资源，快速生效）", "最小投入（省钱，效果迟缓）", "拒绝（保留资源，激怒相关方）"},
    };
    int row = (static_cast<int>(e.phase) + eventId) % 3;
    int col = optionIndex % 3;
    return kRows[row][col];
}

void fillOptions(PendingChoice& c) {
    const EventInfo& e = eventInfo(c.eventId);
    int n = e.choiceCount > 0 ? e.choiceCount : 2;
    for (int i = 0; i < n; ++i) {
        c.options.push_back(eventOptionText(c.eventId, i));
        c.hints.push_back(i == 0 ? "代价：国库 -" : (i == 1 ? "代价：观感 -" : "代价：后续风险 +"));
    }
}

void applyEventEffect(GameState& st, const EventInfo& e, u32 scopeTarget, int optionIndex, TickReport* rep) {
    // 事件强度随选项变化
    Fixed mult = optionIndex == 0 ? Fixed(1) : (optionIndex == 1 ? Fixed::pct(70) : Fixed::pct(40));
    Fixed severity = e.severity * mult;
    switch (e.phase) {
        case EventPhase::Crisis: {
            Empire* t = st.empire(scopeTarget);
            if (t != nullptr) {
                t->stability -= Fixed::pct(5) * mult;
                t->treasury -= severity * Fixed(20);
                if (t->isPlayer) st.market.margin.cash = t->treasury;
                t->domestic.unrest += Fixed::pct(4) * mult;
            }
            if (rep != nullptr) rep->eventsFired += 1;
            break;
        }
        case EventPhase::MarketShock: {
            int res = e.requireCommodity >= 0 ? e.requireCommodity : 0;
            Fixed magnitude = Fixed::pct(3) * mult;
            if (optionIndex == 1) magnitude = Fixed::raw(-magnitude.rawValue());
            marketApplyShock(st, static_cast<u8>(res), magnitude, e.title);
            if (rep != nullptr) rep->eventsFired += 1;
            break;
        }
        case EventPhase::Diplomatic: {
            Empire* t = st.empire(scopeTarget);
            if (t != nullptr) {
                t->influence += severity * Fixed(5) * mult;
                for (auto& other : st.empires)
                    if (other.id != t->id) other.addOpinion(t->id, Fixed::pct(2) * mult);
            }
            if (rep != nullptr) rep->eventsFired += 1;
            break;
        }
        case EventPhase::Anomaly: {
            SystemNode* sys = st.system(scopeTarget);
            if (sys != nullptr && sys->anomaly > 0) {
                const AnomalyInfo& ai = anomalyInfo(static_cast<int>(sys->anomaly));
                if (optionIndex == 0 && ai.rewardClue >= 0) {
                    Provenance p;
                    p.channel = ProvChannel::Analysis;
                    p.credibility = Fixed::pct(70);
                    p.tick = st.tick;
                    clueDiscover(st, static_cast<u16>(ai.rewardClue), p, rep);
                }
            }
            break;
        }
        case EventPhase::Story: {
            st.plot.storyFlags |= (1u << (e.id % 32));
            if (rep != nullptr) rep->eventsFired += 1;
            break;
        }
        default:
            break;
    }
}

}  // namespace

std::string pendingText(const GameState& st, const PendingChoice& c) {
    const EventInfo& e = eventInfo(c.eventId);
    std::string out;
    out += style("【" + (c.kind == ChoiceKind::Faction ? std::string("国内派系诉求") : std::string(e.title)) + "】", Style::Heading);
    out += "  （" + std::string(eventPhaseName(e.phase)) + " / 已拖延 " + std::to_string(c.deferCount) + " 季）\n";
    if (c.kind == ChoiceKind::Faction) {
        const Empire* empire = st.empire(c.scopeTarget);
        if (empire != nullptr) for (const auto& f : empire->domestic.factions)
            if (static_cast<u8>(f.kind) == c.subject) out += "  " + f.name + "：" + f.lastDemand + "\n";
    } else out += wrapJoin(e.body, 86, "  ") + "\n";
    if (c.deferredUntil > st.tick) out += "  已延后至 tick " + std::to_string(c.deferredUntil) + "\n";
    for (std::size_t i = 0; i < c.options.size(); ++i) {
        out += "  [" + std::to_string(i) + "] " + c.options[i];
        if (i < c.hints.size()) out += "   " + style("(" + c.hints[i] + ")", Style::Dim);
        out += "\n";
    }
    (void)st;
    return out;
}

void eventsPhase(GameState& st, TickReport& rep) {
    // 1) 危机时间表：到点激活
    for (auto& c : st.crises) {
        if (c.active) {
            c.progress += Fixed::pct(10);
            // 危机持续影响
            Empire* t = st.empire(c.target);
            if (t != nullptr) {
                t->stability -= Fixed::pct(1) * c.severity / Fixed(10);
                t->treasury -= c.severity * Fixed::raw(2);
                if (t->isPlayer) st.market.margin.cash = t->treasury;
            }
            if (c.progress.rawValue() >= FIX) {
                c.active = false;
                st.logEvent(LogPhase::Event, "event.crisis.end", "危机【" + c.name + "】结束", c.target);
            }
            continue;
        }
        if (st.tick < c.startTick) continue;
        c.active = true;
        const EventInfo& e = eventInfo(c.defId);
        st.logEvent(LogPhase::Event, "event.crisis.begin",
                    "危机爆发【" + c.name + "】" + (e.hasChoices ? "（需要抉择）" : ""), c.target, c.severity);
        if (e.hasChoices) {
            PendingChoice pc;
            pc.eventId = c.defId;
            pc.scopeTarget = c.target;
            pc.createdTick = st.tick;
            fillOptions(pc);
            st.pending.push(pc);
            rep.pendingRaised = true;
        } else {
            applyEventEffect(st, e, c.target, 0, &rep);
        }
    }

    // 2) 常规事件抽取（每 tick 1~2 个）
    int draws = 1 + (st.rng.chance(RngStream::Plot, Fixed::pct(35)) ? 1 : 0);
    for (int i = 0; i < draws; ++i) {
        EventPhase phase;
        Fixed roll = st.rng.unit(RngStream::Plot);
        if (roll.rawValue() < Fixed::pct(35).rawValue()) phase = EventPhase::MarketShock;
        else if (roll.rawValue() < Fixed::pct(60).rawValue()) phase = EventPhase::Diplomatic;
        else if (roll.rawValue() < Fixed::pct(80).rawValue()) phase = EventPhase::Anomaly;
        else phase = EventPhase::Story;
        int id = pickEvent(st, phase);
        if (id < 0) continue;
        const EventInfo& e = eventInfo(id);
        u32 scope = 0;
        if (e.scope == EventScope::Empire) {
            scope = static_cast<u32>(st.rng.pick(RngStream::Plot, st.empires.size()));
        } else if (e.scope == EventScope::System) {
            scope = static_cast<u32>(st.rng.pick(RngStream::Plot, st.map.systems.size()));
        } else if (e.scope == EventScope::Planet) {
            scope = static_cast<u32>(st.rng.pick(RngStream::Plot, st.planets.size()));
        }
        st.logEvent(LogPhase::Event, "event.fire",
                    "事件【" + std::string(e.title) + "】" + (e.hasChoices ? "（需要抉择）" : ""), scope,
                    e.severity);
        if (e.hasChoices && scope == kPlayerId) {
            PendingChoice pc;
            pc.eventId = e.id;
            pc.scopeTarget = scope;
            pc.createdTick = st.tick;
            fillOptions(pc);
            st.pending.push(pc);
            rep.pendingRaised = true;
        } else {
            applyEventEffect(st, e, scope, 0, &rep);
        }
    }
    (void)eventOptionText;
}

void resolveChoiceAuto(GameState& st, int optionIndex) {
    if (st.pending.empty()) return;
    if (resolveChoice(st, 0, optionIndex, nullptr)) return;
    // 无力满足诉求时选择不花钱的拖延，避免无头模拟卡在同一事件。
    const int fallback = st.pending.items.front().kind == ChoiceKind::Faction ? 2 : 0;
    (void)resolveChoice(st, 0, fallback, nullptr);
}

bool resolveChoice(GameState& st, std::size_t index, int optionIndex, std::string* err) {
    if (index >= st.pending.size()) {
        if (err) *err = "抉择序号越界";
        return false;
    }
    PendingChoice c = st.pending.items[index];
    const EventInfo& e = eventInfo(c.eventId);
    int maxOptions = c.options.empty() ? 2 : static_cast<int>(c.options.size());
    if (optionIndex < 0 || optionIndex >= maxOptions) {
        if (err) *err = "选项序号越界（0.." + std::to_string(maxOptions - 1) + "）";
        return false;
    }
    if (c.kind == ChoiceKind::Faction) {
        const auto kind = static_cast<FactionKind>(c.subject);
        if (optionIndex == 0 && !satisfyFaction(st, c.scopeTarget, kind, err)) return false;
        if (optionIndex == 1 && !suppressFaction(st, c.scopeTarget, kind, err)) return false;
        if (optionIndex == 2) {
            Empire* empire = st.empire(c.scopeTarget);
            if (empire != nullptr) for (auto& f : empire->domestic.factions)
                if (f.kind == kind) f.demandPressure += Fixed::pct(10);
        }
    } else applyEventEffect(st, e, c.scopeTarget, optionIndex, nullptr);
    st.pending.removeAt(index);
    st.logEvent(LogPhase::Pending, "pending.resolve",
                "结算抉择【" + (c.kind == ChoiceKind::Faction ? std::string("派系诉求") : std::string(e.title)) + "】选择 [" + std::to_string(optionIndex) + "] " +
                    (c.options.empty() ? "" : c.options[static_cast<std::size_t>(optionIndex)]),
                kPlayerId);
    clampInvariants(st);
    return true;
}

bool deferChoice(GameState& st, std::size_t index, int ticks, std::string* err) {
    if (index >= st.pending.size()) {
        if (err) *err = "抉择序号越界";
        return false;
    }
    if (ticks <= 0 || ticks > 400) {
        if (err) *err = "--ticks 必须在 1..400";
        return false;
    }
    PendingChoice& c = st.pending.items[index];
    c.deferCount += ticks;
    c.deferredUntil = std::max(c.deferredUntil, st.tick) + static_cast<u64>(ticks);
    // 延后代价：立即支付一部分
    st.empires[kPlayerId].domestic.unrest += Fixed::pct(2) * Fixed(ticks);
    for (auto& e : st.empires)
        if (e.id != kPlayerId && e.alive) e.addOpinion(kPlayerId, Fixed::pct(-1) * Fixed(ticks));
    st.logEvent(LogPhase::Pending, "pending.defer",
                "延后抉择 " + std::to_string(ticks) + " 季（民怨 +" + std::to_string(ticks * 2) + "%）", kPlayerId);
    clampInvariants(st);
    return true;
}

int evaluateEnding(const GameState& st) {
    std::array<Fixed, 8> vec = st.plot.endingVector;
    const Empire& p = st.empires[kPlayerId];
    // 由当前局面补充各维度
    vec[0] += Fixed(static_cast<i64>(p.systems.size())) / Fixed(3);
    vec[1] += p.influence / Fixed(300);
    vec[2] += p.treasury / Fixed(40000);
    vec[3] += Fixed(static_cast<i64>(p.fleets.size()));
    vec[4] += Fixed(static_cast<i64>(st.plot.committedConclusions.size())) / Fixed(2);
    vec[5] += p.unity / Fixed(300);
    vec[6] += (Fixed(1) - p.domestic.unrest) * Fixed(3);
    vec[7] += Fixed(static_cast<i64>(p.tech.completed.size())) / Fixed(8);
    // 失误惩罚
    vec[6] -= Fixed(static_cast<i64>(st.plot.falseConclusions.size())) * Fixed::pct(50);
    if (st.chronicleBurned) vec[6] -= Fixed(2);
    int id = pickEnding(vec);
    return id;
}

void plotPhase(GameState& st) {
    // 1) 结论自动解锁（只记录，不自动提交）
    for (int i = 0; i < kConclusionCount; ++i) {
        const ConclusionDef& def = conclusionDef(i);
        if (def.act != st.plot.act) continue;
        u16 id = static_cast<u16>(i);
        if (std::find(st.plot.conclusionsReached.begin(), st.plot.conclusionsReached.end(), id) !=
            st.plot.conclusionsReached.end())
            continue;
        if (!conclusionUnlockable(st, id, nullptr)) continue;
        st.plot.conclusionsReached.push_back(id);
        st.logEvent(LogPhase::Plot, kLogDeduce,
                    "结论解锁【" + std::string(def.nameZh) + "】（可用 deduce --commit 提交）", kPlayerId);
    }

    // 2) 幕次推进
    const ActInfo& act = actInfo(static_cast<int>(st.plot.act));
    int committed = 0;
    for (u16 c : st.plot.committedConclusions)
        if (conclusionDef(static_cast<int>(c)).act == st.plot.act) ++committed;
    if (committed >= act.requiredConclusions && st.plot.act < kActCount) {
        ++st.plot.act;
        st.act = st.plot.act;
        st.logEvent(LogPhase::Plot, kLogAct,
                    "幕次推进 → " + std::string(actTitle(static_cast<int>(st.plot.act))), kPlayerId);
        // 幕推进引发市场冲击
        marketApplyShock(st, static_cast<u8>(Commodity::DataCrystals), act.marketImpact, "剧情推进");
        st.plot.endingVector[4] += Fixed::pct(30) * act.marketImpact;
    }

    // 3) 隐藏第 8 幕
    if (!st.plot.hiddenActUnlocked && hiddenActAvailable(st.plot)) {
        st.plot.hiddenActUnlocked = true;
        st.logEvent(LogPhase::Plot, kLogAct, "【隐藏幕解锁】第八幕的门开启了 —— 观测者悖论", kPlayerId);
    }

    // 4) 剧情结局由实际提交的结论触发；征服胜利在季末独立判定。
    int committedAll = static_cast<int>(st.plot.committedConclusions.size());
    if (!st.ended && !st.victory.achieved && committedAll >= 40) {
        st.endingId = static_cast<u8>(evaluateEnding(st));
        st.ended = true;
        const EndingInfo& en = endingInfo(st.endingId);
        st.logEvent(LogPhase::Plot, kLogEnding,
                    "【结局】" + std::string(en.nameZh) + "：" + std::string(en.text), kPlayerId);
    }
    if (st.endless && st.ended) {
        st.ended = false;   // 无尽模式继续
    }
}

/// 每季推进各帝国的研究立项。
/// 研究是**需要时间的项目**：每季按「基础速率 + 资金投入」推进，
/// 且推进量受「成本 / 最短工期」截断 —— 钱多也不能跳过工期。
static void researchTick(GameState& st) {
    for (auto& e : st.empires) {
        if (!e.alive || e.tech.project == TechState::kNoTech) continue;
        Fixed funding = Fixed(0);
        if (e.tech.fundingPerTick.rawValue() > 0) {
            // 玩家侧的投入来自国库；扣不起就按可支付额度缩减（不会透支）
            Fixed want = e.tech.fundingPerTick;
            Fixed have = fxMax(e.treasury, Fixed(0));
            funding = fxMin(want, have);
            e.treasury -= funding;
            if (e.isPlayer) st.market.margin.cash = e.treasury;
            // 国库见底时自动把投入降档，避免玩家每季都被迫手动调整
            if (funding.rawValue() < want.rawValue()) e.tech.fundingPerTick = funding;
        }
        Fixed sciBonus = Fixed(0);
        for (int b = 0; b < kTechBranchCount; ++b) {
            if (e.tech.current[static_cast<std::size_t>(b)] == e.tech.project) {
                sciBonus = scientistBonus(st, e.id, b);
                break;
            }
        }
        std::vector<u8> done = techTickProject(e.tech, funding, e.tech.completed, sciBonus);
        for (u8 d : done) {
            const TechInfo& ti = techInfo(static_cast<int>(d));
            st.logEvent(LogPhase::Economy, "econ.tech",
                        e.name + " 完成研究【" + std::string(ti.nameZh) + "】→ " + techEffectText(ti),
                        e.id);
            // 不塞进 pending 队列：那会阻塞 advance（退出码 5），
            // 每完成一项科技都强制玩家确认一次过于打扰。
            // 完成记录写入日志，玩家用 `greyfall logs` 或 `research status` 查看。
        }
    }
}

void economyPhase(GameState& st) {
    nationalEdictPhase(st);
    refreshEmpireBonuses(st);
    for (auto& e : st.empires) {
        if (!e.alive) continue;
        Fixed income = Fixed(0);
        Fixed upkeep = developmentUpkeep(st, e.id);
        std::array<Fixed, kCommodityCount> demand;
        for (int c = 0; c < kCommodityCount; ++c) demand[c] = resourceDemand(st, e, c);
        const auto openingStock = e.stock;
        // 本季各商品的**新增产量**（劳役倍率只作用于它，避免复利）
        std::array<Fixed, kCommodityCount> producedByCommodity{};
        Fixed producedThisTick = Fixed(0);

        // 行星产出
        for (u32 sys : e.systems) {
            const SystemNode* s = st.system(sys);
            if (s == nullptr) continue;
            for (u32 pid : s->planets) {
                Planet* p = st.planet(pid);
                if (p == nullptr || p->owner != e.id) continue;
                // 发展度与安抚提升产出
                Fixed stabilityFactor = Fixed::pct(50) + p->stability / Fixed(2);
                std::array<Fixed, kCommodityCount> naturalProduction{};
                for (int c = 0; c < kCommodityCount; ++c)
                    naturalProduction[c] = planetNaturalProduction(*p, static_cast<u8>(c), openingStock[c], demand[c]);
                // ---- 行星发展度成长 ----
                // 开发度原先只在世界生成时设定、永不增长，使经济体缺乏成长循环，
                // 也让「贸易税 = 发展度 × 30」这一收入项冻结不变。
                // 现在：建筑提供基础动力，稳定度加速，民怨抑制，人口提供规模效应。
                {
                    Fixed base = Fixed::bp(25) + Fixed(static_cast<i64>(p->buildings.size())) * Fixed::bp(8);
                    Fixed popFactor = fxSqrt(Fixed(p->pops) / Fixed(10));
                    Fixed dev = base * (Fixed(1) + popFactor) * stabilityFactor *
                                (Fixed(1) - fxClamp(p->unrest, Fixed(0), Fixed(1)));
                    p->development = fxClamp(p->development + dev, Fixed(0), Fixed(10));   // 上限 10（结构体约定范围）
                }
                for (int c = 0; c < kCommodityCount; ++c) {
                    Fixed prod = naturalProduction[static_cast<std::size_t>(c)];
                    if (prod.rawValue() > 0) {
                        e.stock[static_cast<std::size_t>(c)] += prod;
                        producedByCommodity[static_cast<std::size_t>(c)] += prod;
                        producedThisTick += prod;
                        if (c == static_cast<int>(Commodity::Credits)) income += prod;
                    }
                }
                // 建筑加成
                for (u32 bid : p->buildings) {
                    const BuildingInfo& bi = buildingInfo(static_cast<int>(bid & 0xFFu));
                    switch (bi.effect) {
                        case BuildingEffect::ProdCredits:
                            income += bi.effectValue;
                            break;
                        case BuildingEffect::ProdEnergy:
                            e.stock[static_cast<std::size_t>(Commodity::Energy)] += bi.effectValue;
                            break;
                        case BuildingEffect::ProdMinerals:
                            e.stock[static_cast<std::size_t>(Commodity::Minerals)] += bi.effectValue;
                            break;
                        case BuildingEffect::ProdFood:
                            e.stock[static_cast<std::size_t>(Commodity::Food)] += bi.effectValue;
                            break;
                        case BuildingEffect::ProdMedicines:
                            e.stock[static_cast<std::size_t>(Commodity::Medicines)] += bi.effectValue;
                            break;
                        case BuildingEffect::ProdAlloys:
                            e.stock[static_cast<std::size_t>(Commodity::Alloys)] += bi.effectValue;
                            break;
                        case BuildingEffect::ProdComponents:
                            e.stock[static_cast<std::size_t>(Commodity::Components)] += bi.effectValue;
                            break;
                        case BuildingEffect::ProdUnity:
                            e.unity += bi.effectValue / Fixed(10);
                            break;
                        case BuildingEffect::ProdInfluence:
                            e.influence += bi.effectValue / Fixed(10);
                            break;
                        case BuildingEffect::Stability:
                            e.stability = fxClamp(e.stability + bi.effectValue / Fixed(10), Fixed(0), Fixed(1));
                            break;
                        case BuildingEffect::ProdResearch:
                        case BuildingEffect::Trading:
                        case BuildingEffect::Storage:
                        case BuildingEffect::ClueDiscovery:
                            // 持续加成统一由 refreshEmpireBonuses 重建。
                            break;
                        default:
                            break;
                    }
                    upkeep += Fixed(bi.upkeep);
                }
                // 人头税：受稳定度与民怨调节。
                // 除数决定税基量级 —— 之前用 /1000 导致每行星每季只有约 3 cr，
                // 比舰队维护低两个数量级，所有帝国必然破产。
                Fixed taxRate = Fixed(2) * (Fixed::pct(100) - p->unrest * Fixed::pct(40)) *
                                (Fixed::pct(50) + p->stability / Fixed(2));
                income += Fixed(p->pops) * taxRate / Fixed(20);
                // 贸易税：发展度带来的流通收益
                income += p->development * Fixed(30);
                // 人口维护费
                upkeep += Fixed(p->pops) / Fixed(200);
                // 人口增长
                Fixed growth = Fixed(p->pops) * Fixed::bp(20) * (Fixed(1) + empireModifier(e, ModKind::Growth));
                growth = growth * (Fixed(1) - p->unrest);
                p->pops += growth.rawValue() / FIX;
                if (p->pops < 0) p->pops = 0;
            }
        }

        for (int c = 0; c < kCommodityCount; ++c) {
            const Fixed production = megaProduction(st, e, c);
            e.stock[c] += production;
            producedByCommodity[c] += production;
            producedThisTick += production;
        }
        // ---- 库存消耗与维护 ----
        // 关键：demand 必须真的被消耗，否则库存只增不减 ⇒
        // GDP 无限膨胀、维护费吃掉全部收入、国力指数爆炸。
        int shortages = 0;
        for (int c = 0; c < kCommodityCount; ++c) {
            const CommodityInfo& ci = commodityInfo(c);
            Fixed& stock = e.stock[static_cast<std::size_t>(c)];
            Fixed need = demand[static_cast<std::size_t>(c)];
            if (need.rawValue() > 0) {
                if (stock.rawValue() >= need.rawValue()) {
                    stock -= need;
                } else {
                    if (stock.rawValue() > 0) stock = Fixed(0);
                    ++shortages;
                }
            }
            // 库存维护（按剩余库存计费）
            upkeep += stock * ci.storage / Fixed(1000);
            // 战略物资囤积过剩会招致额外维护
            Fixed storageLimit = Fixed(50000) + e.storageBonus;
            if (ci.cat == EcoCategory::Strategic && stock.rawValue() > storageLimit.rawValue()) {
                upkeep += (stock - storageLimit) / Fixed(500);
            }
        }
        if (shortages > 0) {
            // 短缺对民怨与稳定度的影响都是**结构性**的，
            // 已统一在 FactionAI 的结构性民怨与稳定度目标中计算，此处不再按季扣减。
            if (e.isPlayer && st.tick % 8 == 0) {
                st.logEvent(LogPhase::Economy, "econ.shortage",
                            "本季有 " + std::to_string(shortages) + " 种物资短缺，稳定度与民心受损",
                            e.id, Fixed(shortages));
            }
        }

        // 政策维护费
        upkeep += Fixed(policyUpkeep(st, e.id));
        // 派系满意度的均衡收敛统一在 domesticPhase / FactionAI 中处理，
        // 此处不再重复施加（两处收敛会互相抵消）。
        // 劳役制度：以民怨为代价换取经济产出（蓄奴制 ×1.30）。
        //
        // 为什么作用在**收入**而不是实物产量：实物生产有库存节流 ——
        // 库存远超需求时产量被压到 0（实测开局库存 2400、需求仅 16，
        // 产量从第 1 季起就完全为 0），倍率乘以 0 仍是 0。
        // 收入是纯流量、不受节流影响，奴役的经济意义在这里才看得见。
        {
            Fixed laborMult = laborOutputMultiplier(e.labor);
            if (laborMult.rawValue() != Fixed(1).rawValue()) {
                Fixed gain = (income - upkeep) * (laborMult - Fixed(1));
                if (gain.rawValue() > 0) income += gain;
            }
        }
        // 民生开销：社会主义的福利支出（upkeepBias 为正则加重）。
        // 这是它「工厂效率高」的对价 —— 没有这一项，社会主义就是纯赚。
        {
            Fixed ub = governmentInfo(e.government).upkeepBias;
            if (ub.rawValue() > 0) upkeep += upkeep * ub;
            else if (ub.rawValue() < 0) upkeep += upkeep * ub;   // 负值即减负
        }
        // 腐败：直接按比例抽走净收入。
        // 这是「帝国规模」的真实代价 —— 没有它，疆域扩张永远是纯收益。
        {
            Fixed loss = corruptionIncomeLoss(st, e.id);
            if (loss.rawValue() > 0) {
                Fixed net = income - upkeep;
                if (net.rawValue() > 0) {
                    Fixed skim = net * loss;
                    income -= skim;
                }
            }
        }
        // 记录净收入：AI 的各类支出以它为上限（见 MarketEngine / FactionAI）
        e.lastIncome = income - upkeep;
        e.treasury += income - upkeep;
        // 财政赤字对民怨与稳定度的影响同样是结构性的（见 FactionAI），
        // 此处不再按季扣减。
        if (e.id == kPlayerId) {
            st.market.margin.cash += income - upkeep;
            e.treasury = st.market.margin.cash;
        }

        // 种族张力导致的民怨是**结构性**的，已统一在 FactionAI 的民怨目标中计算，
        // 此处不再按季累加（累加会让民怨在长局中必然饱和）。

        // 研究推进已改由 researchTick() 以「立项 + 逐季推进」的方式处理
        //（见本文件顶部的 researchTick）。这里**不能**再调用 techAdvance：
        // 那是一条绕过最短工期的即时路径，会让「研究需要时间」形同虚设
        //（两条路径并存时，科技会在立项当季就被旧路径结算掉）。

        // GDP 与国力评分。
        // 库存的价值贡献按「开方」计入 —— 否则单纯囤积就能让国力无限增长，
        // 与「经济实力来自产能与流通」的直觉相悖。
        Fixed gdp = Fixed(0);
        for (int c = 0; c < kCommodityCount; ++c) {
            Fixed qty = e.stock[static_cast<std::size_t>(c)];
            Fixed px = st.market.spotIndex[static_cast<std::size_t>(c)];
            if (px.rawValue() <= 0) px = commodityInfo(c).basePrice;
            // 单位换算到「千单位」量级后再开方，保持量纲稳定
            Fixed kilounits = qty / Fixed(1000);
            Fixed contrib = fxSqrt(kilounits) * px;
            gdp += contrib;
        }
        e.gdp = gdp;
        e.economy = fxLerp(e.economy, gdp / Fixed(20), Fixed::pct(10));
        if (e.economy.rawValue() < 0) e.economy = Fixed(0);
        e.score = e.powerIndex();
    }

    researchTick(st);
    developmentPhase(st);
    (void)marginMarkToMarket(st);
}

std::string epochReport(const GameState& st) {
    std::string out;
    out += "═══ 纪元报告 ═══\n";
    out += "纪元 #" + std::to_string(st.epochIndex) + "：" + st.epochName + "\n";
    out += "词缀：" + st.modifierName + "\n";
    out += "回合：" + std::to_string(st.tick) + " 季    当前幕：第 " + std::to_string(static_cast<int>(st.plot.act)) +
           " 幕 / " + std::to_string(kActCount) + "\n";
    out += "难度：" + std::to_string(st.difficulty) + "    AI 前瞻：" + std::to_string(st.aiForesight) + " 季\n";
    out += "读档次数：" + std::to_string(st.rollbackCount) + "    chronicle 链头：" +
           std::to_string(st.chronicleHead) + "\n\n";

    out += "结局向量（8 维）：\n";
    static const char* kDims[] = {"霸权", "联邦", "资本", "种族", "知识", "信仰", "毁灭", "超脱"};
    for (int i = 0; i < 8; ++i) {
        Fixed v = st.plot.endingVector[static_cast<std::size_t>(i)];
        out += "  " + padRight(kDims[i], 6) + " " + bar(v / Fixed(8), 24) + " " + fixedStrPlain(v, 2) + "\n";
    }
    if (st.ended) {
        const EndingInfo& en = endingInfo(st.endingId);
        out += "\n" + style("结局：" + std::string(en.nameZh), Style::Heading) + "\n";
        out += wrapJoin(en.text, 86, "  ") + "\n";
    } else {
        out += "\n（结局尚未达成：提交至少 40 个结论，或推进到 tick 200）\n";
    }
    out += "\n已提交结论：" + std::to_string(st.plot.committedConclusions.size()) + "    误判：" +
           std::to_string(st.plot.falseConclusions.size()) + "    已发现线索：" +
           std::to_string(st.plot.knownClues.size()) + "\n";
    if (st.plot.hiddenActUnlocked) out += style("隐藏第八幕已解锁\n", Style::Accent);
    return out;
}

}  // namespace gf
