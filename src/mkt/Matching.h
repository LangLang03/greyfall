#pragma once
// 撮合内核：价格-时间优先 + 平方根冲击 + 无自成交
#include <vector>

#include "mkt/MarketState.h"

namespace gf {

/// 进入撮合的订单（不改变调用者的订单对象）
struct MkOrder {
    u64 id = 0;
    u32 owner = 0;
    Fixed px{};
    i64 qty = 0;
    bool buy = true;
    OrderKind kind = OrderKind::Limit;
    /// 冰量单的露出量（0 表示全量可见）
    i64 shown = 0;
};

struct MkFill {
    u64 passiveId = 0;
    u32 passiveOwner = 0;
    /// 被动方的「控制人」：做市商为 0xFFFF0000+交易所号，用于回填做市商库存
    u32 passiveController = 0xFFFFFFFFu;
    i64 qty = 0;
    Fixed px{};
};

struct MkResult {
    i64 filled = 0;
    Fixed avgPx{};
    std::vector<MkFill> consumed;
    Fixed impactTemp{};
    Fixed impactPerm{};
    i64 levelsConsumed = 0;
    /// 未成交剩余
    i64 remaining = 0;
};

/// 撮合一笔订单到簿上；impactReserve 供内部冲击计算使用（可用 Fixed(0)）
MkResult matchOrder(Book& b, const MkOrder& in, Fixed impactReserve);

/// 平方根冲击定律：Δp_temp = Y·σ·sqrt(Q/V20)，Δp_perm = κ·Δp_temp
[[nodiscard]] Fixed impactTemporary(Fixed sigma, i64 qty, Fixed v20, Fixed liquidityScale = Fixed(1));
[[nodiscard]] Fixed impactPermanent(Fixed tempImpact);

/// 冲击随时间半衰回归（λ=0.5 每 tick 衰减一半）
[[nodiscard]] Fixed impactDecay(Fixed temp);

/// 从簿上实际成交量更新 last / vwap / volume
void bookRecordTrade(Book& b, Fixed px, i64 qty);

}  // namespace gf
