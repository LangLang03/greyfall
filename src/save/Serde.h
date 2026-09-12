#pragma once
// GameState ⇄ 字节流（确定性：同状态两次序列化字节完全相同）
#include <vector>

#include "util/Fixed.h"

namespace gf {

struct GameState;

/// 序列化（含字符串池），字节唯一
[[nodiscard]] std::vector<u8> serializeState(const GameState& st);
/// 反序列化；失败抛 GameError(Integrity)
void deserializeState(const std::vector<u8>& bytes, GameState& st);
/// 反序列化（不抛异常版本）
[[nodiscard]] bool tryDeserializeState(const std::vector<u8>& bytes, GameState& st);

/// 重建订单簿的派生聚合档位（读档后必须调用）
void rebuildDerived(GameState& st);

/// 序列化日志条目（轻量，用于导出可读存档片段）
[[nodiscard]] std::string stateDigestHex(const GameState& st);

}  // namespace gf
