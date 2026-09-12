#pragma once
#include <functional>
// 订单簿：价格-时间优先的存取、聚合档位、查询
#include <vector>

#include "mkt/MarketState.h"

namespace gf {

/// 重建派生聚合档位（bids 降序 / asks 升序，每侧至多 kBookDepthMax 档）
void bookRebuildLevels(Book& b);
/// 中间价：有双边用中值，否则回退 last
[[nodiscard]] Fixed bookMid(const Book& b);
/// 相对价差：(ask-bid)/mid
[[nodiscard]] Fixed bookSpread(const Book& b);
[[nodiscard]] const PriceLevel* bookBestBid(const Book& b);
[[nodiscard]] const PriceLevel* bookBestAsk(const Book& b);
/// 插入订单（保持 orders 稳定序，价格-时间优先由 (buy,px,seq) 决定）
void bookInsert(Book& b, const Order& o);
/// 按 id 移除；返回是否移除
bool bookRemove(Book& b, u64 orderId);
/// 找到订单指针
[[nodiscard]] Order* bookFind(Book& b, u64 orderId);
/// 某侧前 levels 档的累计数量
[[nodiscard]] i64 bookDepthQty(const Book& b, bool buy, int levels);
/// 某侧前 levels 档的累计名义金额
[[nodiscard]] Fixed bookDepthNotional(const Book& b, bool buy, int levels);
/// 市价单在当前簿上的预估成交均价（不改变簿）；qty 为可用量
[[nodiscard]] Fixed bookEstimatePrice(const Book& b, bool buy, i64 qty, i64* fillable = nullptr);
/// 清算交叉：真实交易所不可能存在「买一 ≥ 卖一」的账本。
/// 做市商重报价或外部挂单可能造成交叉，此处反复撮合到不交叉为止。
/// 返回成交笔数；同主体（非做市商）的交叉会撤销较晚的一笔以避免自成交。
i64 bookResolveCrossed(Book& b, int maxRounds = 64,
    const std::function<void(const Order&, const Order&, Fixed, i64)>& settle = {});

/// 移除所有 Day 单（tick 结束时调用）
i64 bookExpireDayOrders(Book& b, u64 tick);
/// 活动订单数量
[[nodiscard]] i64 bookActiveQty(const Book& b, bool buy);
/// 清空
void bookClear(Book& b);

}  // namespace gf
