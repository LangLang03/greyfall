#pragma once
// OmniscientReader —— 泛视网络：AI 直接读取玩家的全部状态
//
// 剧情解释：大沉默后遗留的泛视网络把玩家舰桥遥测变成公开广播。
// 世界"看得见你"，看不见的是你的意图与你的谎言之所以为你的谎言。
// 因此玩家的欺诈轴从「隐藏数据」转为「布置数据」——伪造字段带 Provenance。
#include <string>
#include <vector>

#include "core/GameState.h"
#include "domain/Observable.h"
#include "domain/Provenance.h"
#include "util/Fixed.h"

namespace gf {

/// 读取某主体的全部状态
[[nodiscard]] Observable omniscientSnapshot(const GameState& st, u32 subject);

/// 读取某主体时应用其反情报/屏障（降低覆盖率、提高噪声）
[[nodiscard]] Fixed subjectCounterIntel(const GameState& st, u32 subject);

/// intel --what-they-know 的透明度报告
[[nodiscard]] std::string whatTheyKnowReport(const GameState& st, u32 observer, u32 subject);

/// 玩家能看到"AI 眼中的自己"的置信度摘要
[[nodiscard]] Fixed observationConfidence(const GameState& st, u32 observer, u32 subject);

}  // namespace gf
