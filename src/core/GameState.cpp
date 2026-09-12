#include "core/GameState.h"

#include "crypto/Sha256.h"
#include "save/Serde.h"
#include "util/Bits.h"
#include "util/Str.h"

namespace gf {

void GameState::initRelations() {
    relations.assign(static_cast<std::size_t>(kMaxEmpires) * kMaxEmpires, Relation{});
}

std::size_t GameState::aliveEmpires() const {
    std::size_t n = 0;
    for (const auto& e : empires)
        if (e.alive) ++n;
    return n;
}

void GameState::logEvent(LogPhase phase, std::string_view code, std::string text, u32 actor, Fixed value) {
    LogEntry e;
    e.tick = tick;
    e.phase = static_cast<u8>(phase);
    e.actor = actor;
    e.code.assign(code);
    e.text = std::move(text);
    e.value = value;
    log.push_back(std::move(e));
    ++logSeq;
}

void GameState::trimLog(std::size_t keep) {
    if (log.size() > keep) log.erase(log.begin(), log.end() - static_cast<std::ptrdiff_t>(keep));
}

u64 GameState::stateHash() const {
    std::vector<u8> bytes = serializeState(*this);
    Sha256Digest d = sha256(bytes.data(), bytes.size());
    return readLE64(d.data());
}

u64 GameState::fingerprint() const {
    u64 h = 1469598103934665603ull;
    auto mix = [&h](u64 v) {
        h ^= v;
        h *= 1099511628211ull;
    };
    mix(tick);
    mix(seed);
    mix(epochIndex);
    mix(victory.consecutiveQuarters);
    mix(victory.lastEvaluatedTick);
    mix(victory.achieved);
    mix(rng.fingerprint());
    mix(static_cast<u64>(empires.size()));
    mix(static_cast<u64>(planets.size()));
    mix(static_cast<u64>(map.systems.size()));
    for (const auto& e : empires) {
        mix(static_cast<u64>(e.treasury.rawValue()));
        mix(static_cast<u64>(e.stock[0].rawValue()));
        mix(static_cast<u64>(e.stock[static_cast<std::size_t>(Commodity::Alloys)].rawValue()));
        mix(static_cast<u64>(e.score.rawValue()));
    }
    for (const auto& sys : map.systems) {
        mix(static_cast<u64>(sys.owner));
        mix(static_cast<u64>(sys.blockade.rawValue()));
    }
    mix(static_cast<u64>(pending.size()));
    mix(static_cast<u64>(plot.act));
    return h;
}

void GameState::pushHistory() {
    HistoryPoint p;
    p.tick = tick;
    p.stateHash = stateHash();
    for (std::size_t i = 0; i < empires.size() && i < kMaxEmpires; ++i) {
        p.score[i] = empires[i].score;
        p.treasury[i] = empires[i].treasury;
    }
    for (int c = 0; c < kCommodityCount; ++c) p.index[static_cast<std::size_t>(c)] = market.spotIndex[static_cast<std::size_t>(c)];
    history.push_back(std::move(p));
    if (history.size() > 192) history.erase(history.begin(), history.begin() + 64);
}

std::string stateSummaryLine(const GameState& st) {
    const Empire& p = st.empires.empty() ? Empire{} : st.empires[0];
    std::string s = "tick=" + std::to_string(st.tick);
    s += " 纪元=" + std::to_string(st.epochIndex);
    s += " 幕=" + std::to_string(static_cast<int>(st.plot.act));
    s += " 国库=" + fixedStr(p.treasury, 1);
    s += " 国力=" + fixedStr(p.score, 2);
    s += " 待抉择=" + std::to_string(st.pending.size());
    return s;
}

}  // namespace gf
