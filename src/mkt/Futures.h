#pragma once
// 期货：期限结构 / 基差 / 便利收益 / 交割
#include "core/GameState.h"
#include "mkt/MarketState.h"

namespace gf {

struct TickReport;

/// 理论价格：F = S·(1 + carry - convenience)
[[nodiscard]] Fixed futuresFairPrice(Fixed spot, Fixed carry, Fixed convenience);

/// 由现货库存水平推导便利收益（低库存 → 正便利收益 → backwardation）
[[nodiscard]] Fixed convenienceYield(const GameState& st, u8 res);

/// 更新某交易所的全部期限曲线
void futuresUpdateCurve(GameState& st, int exch);

/// 更新全部交易所的全部曲线
void futuresUpdateAll(GameState& st);

/// 判定期限结构：contango / backwardation / flat
[[nodiscard]] const char* futuresStructureName(const GameState& st, u8 res);

/// 交割：到期合约结算、违约判定
void futuresSettleExpiry(GameState& st, TickReport& rep);

/// 开仓。owner 决定资金来源与持仓归属：
///   owner == kPlayerId ⇒ 记入玩家保证金与持仓明细
///   owner != kPlayerId ⇒ 从该帝国国库扣款，只调整持仓量与持仓量统计
/// 之前无论谁开仓都从玩家保证金扣款，导致 AI 交易会掏空玩家账户。
[[nodiscard]] bool futuresOpen(GameState& st, u32 owner, u8 res, u8 term, i64 qty, bool isShort, int leverage,
                               std::string* err);
/// 兼容旧签名（默认玩家）
[[nodiscard]] bool futuresOpen(GameState& st, u8 res, u8 term, i64 qty, bool isShort, int leverage,
                               std::string* err);

/// 平仓
[[nodiscard]] bool futuresClose(GameState& st, u8 res, u8 term, i64 qty, std::string* err);

}  // namespace gf
