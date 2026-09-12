#pragma once
// 腐败：帝国规模扩大后的结构性损耗
//
// 设计动机：
//   早期没有任何「规模代价」—— 疆域越大、行星越多，净收益就线性增长，
//   不存在行政失控的问题。政体之间也因此缺少一条重要的区分维度：
//   资本主义市场繁荣但腐败严重，社会主义工厂高效但福利开支沉重。
//
// 现行规则：
//   * 腐败度 0..1，随**帝国规模**（行星数 + 星系数）增长；
//   * 受政体腐败倾向（corruptionBias）与官僚类修正影响；
//   * 腐败直接**侵蚀收入**（按比例抽走）并轻微推高民怨；
//   * 可主动治理：花钱反腐可短期压低，但会自然回升。
#include <string>

#include "util/Fixed.h"

namespace gf {

struct GameState;
struct Empire;

/// 反贪行动的国库成本
inline constexpr i64 kAntiCorruptionCost = 25000;

/// 该帝国当前的腐败度 0..1
[[nodiscard]] Fixed corruptionOf(const GameState& st, u32 empire);
/// 腐败每季侵蚀的收入比例（等于腐败度本身）
[[nodiscard]] Fixed corruptionIncomeLoss(const GameState& st, u32 empire);
/// 腐败带来的民怨增量
[[nodiscard]] Fixed corruptionUnrest(const GameState& st, u32 empire);

/// 发起反腐运动：花国库压低腐败，但会自然回升
[[nodiscard]] bool antiCorruption(GameState& st, u32 empire, std::string* msg);

/// 每 tick：腐败随规模与政体演化
void corruptionPhase(GameState& st);

/// 腐败态势文本
[[nodiscard]] std::string corruptionReport(const GameState& st, u32 empire);

}  // namespace gf
