#include "gen/EventSchedule.h"

#include <algorithm>

#include "clue/ClueDef.h"
#include "rng/Streams.h"

namespace gf {

void scheduleEvents(GameState& st) {
    RngBus& rng = st.rng;
    st.crises.clear();
    // 危机：按难度在前期到中期排布 3~5 个
    int count = 3 + std::min(3, st.difficulty / 2);
    for (int i = 0; i < count; ++i) {
        CrisisState c;
        c.id = static_cast<u32>(i);
        c.defId = static_cast<u8>(rng.pick(RngStream::Crisis, 30));
        c.name = std::string(eventInfo(c.defId).title);
        c.active = false;
        c.startTick = static_cast<u64>(12 + i * 14 + rng.range(RngStream::Crisis, 0, 8));
        c.severity = Fixed::raw(static_cast<i64>(rng.range(RngStream::Crisis, 800, 1800)));
        c.target = static_cast<u32>(rng.pick(RngStream::Crisis, st.empires.size()));
        c.progress = Fixed(0);
        st.crises.push_back(std::move(c));
    }
}

void generateAnomalies(GameState& st) {
    RngBus& rng = st.rng;
    // 约 30% 的星系有异常点
    for (auto& sys : st.map.systems) {
        if (rng.chance(RngStream::World, Fixed::pct(30))) {
            sys.anomaly = static_cast<u32>(1 + rng.pick(RngStream::World, kAnomalyCount - 1));
        } else {
            sys.anomaly = 0;
        }
    }
}

void initClueGraph(GameState& st) {
    st.clues.assign(kClueCount, ClueNode{});
    st.clueEdges.clear();
    for (int i = 0; i < kClueCount; ++i) {
        st.clues[static_cast<std::size_t>(i)].def = static_cast<u16>(i);
        st.clues[static_cast<std::size_t>(i)].act = clueDef(i).act;
    }
    // 玩家开局已知：第 1 幕的少量线索（作为推理起点）
    RngBus& rng = st.rng;
    int starters = 6;
    for (int i = 0; i < starters; ++i) {
        int idx = static_cast<int>(rng.pick(RngStream::Clue, static_cast<std::size_t>(kClueCount / kActCount)));
        ClueNode& n = st.clues[static_cast<std::size_t>(idx)];
        if (n.known) continue;
        n.known = true;
        n.credibility = Fixed::pct(70);
        n.prov.channel = ProvChannel::DirectObservation;
        n.prov.credibility = Fixed::pct(80);
        n.prov.tick = 0;
        n.discoveredTick = 0;
        st.plot.knownClues.push_back(static_cast<u16>(idx));
    }
    // 建立初始超边（来自内容的静态关联）
    for (int i = 0; i < kClueCount; ++i) {
        for (u16 j : clueDef(i).linked) {
            if (j >= static_cast<u16>(kClueCount) || j <= static_cast<u16>(i)) continue;
            ClueEdge e;
            e.a = static_cast<u16>(i);
            e.b = j;
            e.kind = clueDef(i).linkKind;
            e.weight = Fixed::pct(55);
            e.createdTick = 0;
            st.clueEdges.push_back(e);
        }
    }
}

int pickEvent(GameState& st, EventPhase phase) {
    RngBus& rng = st.rng;
    int lo = 0, hi = 0;
    switch (phase) {
        case EventPhase::Crisis: lo = 0; hi = 30; break;
        case EventPhase::MarketShock: lo = 30; hi = 55; break;
        case EventPhase::Diplomatic: lo = 55; hi = 75; break;
        case EventPhase::Anomaly: lo = 75; hi = 100; break;
        case EventPhase::Story: lo = 100; hi = kEventCount; break;
        default: return -1;
    }
    // 加权抽样（跳过 minTick 未到的）
    i64 total = 0;
    for (int i = lo; i < hi; ++i) {
        const EventInfo& e = eventInfo(i);
        if (static_cast<i64>(st.tick) < e.minTick) continue;
        total += e.weight;
    }
    if (total <= 0) return -1;
    i64 pick = static_cast<i64>(rng.range(RngStream::Plot, 1, total));
    for (int i = lo; i < hi; ++i) {
        const EventInfo& e = eventInfo(i);
        if (static_cast<i64>(st.tick) < e.minTick) continue;
        pick -= e.weight;
        if (pick <= 0) return i;
    }
    return lo;
}

}  // namespace gf
