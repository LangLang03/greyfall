#pragma once
// 市场引擎：撮合编排 / 做市 / 冲击 / 阶段推进
#include "core/GameState.h"
#include "mkt/MarketState.h"
#include "mkt/Matching.h"

namespace gf {

struct TickReport;

struct OrderRequest {
    u32 owner = kPlayerId;
    u8 res = 0;
    u8 exch = kExchCX;
    bool buy = true;
    i64 qty = 0;
    Fixed px{};                                  // 市价单忽略
    OrderKind kind = OrderKind::Limit;
    Tif tif = Tif::Gtc;
    /// 冰量单的露出量（0 = 全量可见）
    i64 shown = 0;
    /// 同控制人的其他账户（操纵检测用）
    u32 controller = 0xFFFFFFFFu;
};

struct OrderAck {
    bool accepted = false;
    u64 orderId = 0;
    i64 filled = 0;
    i64 remaining = 0;
    Fixed avgPx{};
    Fixed impact{};
    Fixed fee{};
    Fixed notional{};
    std::string reason;
};

/// 提交订单（撮合 + 入簿 + 冲击 + 统计）
[[nodiscard]] OrderAck marketSubmitOrder(GameState& st, const OrderRequest& req);

/// 撤单（返回剩余量）
[[nodiscard]] i64 marketCancelOrder(GameState& st, u8 exch, u8 res, u64 orderId, bool* found);

/// 手续费
[[nodiscard]] Fixed marketFee(int exch, Fixed notional);

/// 阶段 2：新息到达 → 8 次 auction call 撮合 → 做市重报价
void phaseMarket(GameState& st, TickReport& rep);
void marketAuctionCalls(GameState& st, TickReport& rep, int calls);
void marketMakerRequote(GameState& st);
void marketUpdateSpotIndex(GameState& st);

/// 阶段 3 的一部分：波动率更新
void marketUpdateVolatility(GameState& st);
/// 阶段 3 的一部分：保证金盯市与级联
void marketSettleMargins(GameState& st, TickReport& rep);

/// 基本面漂移：产能/消耗通过知情交易者的限价单"发现"进价格
void marketFundamentalFlow(GameState& st);

/// 清理簿内陈旧订单，保持档位与内存有界
void marketPruneBooks(GameState& st);

/// 播放一个市场冲击事件
void marketApplyShock(GameState& st, u8 res, Fixed magnitude, std::string_view cause, int exch = kExchCX);

}  // namespace gf
