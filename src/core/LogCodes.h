#pragma once
// 日志编码：所有可复盘事件都有稳定 code，便于 --phase 过滤与测试断言
#include <string_view>

#include "util/Fixed.h"

namespace gf {

enum class LogPhase : u8 {
    Setup = 0,
    Ap,
    Pending,
    Market,
    Vol,
    Reader,
    Model,
    Ai,
    Federation,
    Domestic,
    Combat,
    Event,
    Clue,
    Plot,
    Economy,
    Tick,
    Save,
    Chronicle,
    Count,
};

struct LogCode {
    std::string_view code;
    LogPhase phase;
};

// 关键日志码（常量，避免散落字符串）
inline constexpr std::string_view kLogNewGame = "world.new";
inline constexpr std::string_view kLogTick = "tick.advance";
inline constexpr std::string_view kLogOrderAccepted = "market.order.accepted";
inline constexpr std::string_view kLogOrderRejected = "market.order.rejected";
inline constexpr std::string_view kLogFill = "market.fill";
inline constexpr std::string_view kLogNoFill = "market.nofill";
inline constexpr std::string_view kLogMarginCall = "market.margin.call";
inline constexpr std::string_view kLogCascade = "market.margin.cascade";
inline constexpr std::string_view kLogDefault = "market.default";
inline constexpr std::string_view kLogManip = "market.manip.detected";
inline constexpr std::string_view kLogInsider = "market.insider";
inline constexpr std::string_view kLogShock = "market.shock";
inline constexpr std::string_view kLogBetrayal = "ai.betrayal.ev";
inline constexpr std::string_view kLogBalance = "ai.balance.coalition";
inline constexpr std::string_view kLogIntent = "ai.intent.predicted";
inline constexpr std::string_view kLogModel = "ai.model.updated";
inline constexpr std::string_view kLogFraud = "ai.fraud.flagged";
inline constexpr std::string_view kLogEnvoy = "diplo.envoy";
inline constexpr std::string_view kLogTreaty = "diplo.treaty";
inline constexpr std::string_view kLogWar = "diplo.war";
inline constexpr std::string_view kLogSpy = "diplo.spy";
inline constexpr std::string_view kLogDomestic = "domestic.demand";
inline constexpr std::string_view kLogCoup = "domestic.coup";
inline constexpr std::string_view kLogClue = "clue.discovered";
inline constexpr std::string_view kLogDeduce = "clue.deduced";
inline constexpr std::string_view kLogAct = "plot.act";
inline constexpr std::string_view kLogEnding = "plot.ending";
inline constexpr std::string_view kLogSave = "save.write";
inline constexpr std::string_view kLogRollback = "save.rollback";

[[nodiscard]] std::string_view logPhaseName(LogPhase p);
[[nodiscard]] LogPhase logPhaseFromName(std::string_view s);
/// 由 code 首段推断阶段（"market.fill" → Market）
[[nodiscard]] LogPhase phaseOfCode(std::string_view code);

}  // namespace gf
