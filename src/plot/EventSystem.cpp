#include "plot/EventSystem.h"

#include <algorithm>
#include <array>
#include <string>

#include "core/GameState.h"
#include "core/TickPipeline.h"
#include "clue/ClueGraph.h"
#include "core/ResolutionEngine.h"
#include "ai/FactionAI.h"
#include "domain/ModifierUtil.h"
#include "gen/EventSchedule.h"
#include "mkt/MarketEngine.h"
#include "plot/BeatResolver.h"
#include "rng/Streams.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {
namespace {

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
}  // namespace

void resolveChoiceAuto(GameState& st, int optionIndex) {
    if (st.pending.empty()) return;
    // 自动结算的选项必须**理性**，不能无脑选 0。
    //
    // 事件文本普遍把「代价：国库 -」放在第 0 项（公开处理/强硬回应），
    // 无脑选 0 等于让基准测试（selftest / bots / 自动化试玩脚本）
    // 持续替玩家做最贵的选择 —— 实测同一种子下「总是选 0」会让玩家在
    // 21 季内被征服，而按代价择优则能长期存活。
    // 基准测试若表现的是"最差玩家"，就无法用来判断游戏难度是否合理。
    //
    // 现状：优先选「明确不消耗国库」的选项；若全部要花钱，退而选
    // 代价文本最轻的（拖延/观望类通常排最后）。
    if (optionIndex < 0) {
        const PendingChoice& c = st.pending.items.front();
        int best = 0;
        if (c.kind != ChoiceKind::Faction) {
            int bestRank = -1;
            for (std::size_t i = 0; i < c.options.size(); ++i) {
                const std::string& opt = c.options[i];
                int rank = 0;
                const bool costly = opt.find("国库 -") != std::string::npos;
                const bool cheap = opt.find("观感 -") != std::string::npos ||
                                   opt.find("后续风险") != std::string::npos;
                if (costly) rank = -2;
                else if (cheap) rank = 1;
                else rank = 0;
                if (rank > bestRank) {
                    bestRank = rank;
                    best = static_cast<int>(i);
                }
            }
        }
        optionIndex = best;
    }
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

}  // namespace gf
