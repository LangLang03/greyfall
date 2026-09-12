#include "core/LogCodes.h"

#include "util/Str.h"

namespace gf {
namespace {
constexpr std::string_view kPhaseNames[] = {
    "setup", "ap", "pending", "market", "vol", "reader", "model", "ai",
    "federation", "domestic", "combat", "event", "clue", "plot", "economy", "tick",
    "save", "chronicle",
};
}  // namespace

std::string_view logPhaseName(LogPhase p) {
    std::size_t i = static_cast<std::size_t>(p);
    if (i >= sizeof(kPhaseNames) / sizeof(kPhaseNames[0])) return "?";
    return kPhaseNames[i];
}

LogPhase logPhaseFromName(std::string_view s) {
    for (std::size_t i = 0; i < sizeof(kPhaseNames) / sizeof(kPhaseNames[0]); ++i)
        if (kPhaseNames[i] == s) return static_cast<LogPhase>(i);
    return LogPhase::Count;
}

LogPhase phaseOfCode(std::string_view code) {
    std::size_t dot = code.find('.');
    std::string_view head = dot == std::string_view::npos ? code : code.substr(0, dot);
    if (head == "world") return LogPhase::Setup;
    if (head == "tick") return LogPhase::Tick;
    if (head == "market") return LogPhase::Market;
    if (head == "ai") return LogPhase::Ai;
    if (head == "diplo") return LogPhase::Model;
    if (head == "domestic") return LogPhase::Domestic;
    if (head == "clue") return LogPhase::Clue;
    if (head == "plot") return LogPhase::Plot;
    if (head == "save") return LogPhase::Save;
    if (head == "econ") return LogPhase::Economy;
    if (head == "combat") return LogPhase::Combat;
    if (head == "event") return LogPhase::Event;
    if (head == "fed") return LogPhase::Federation;
    return LogPhase::Count;
}

}  // namespace gf
