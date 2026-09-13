#include "mkt/MarketEngine.h"

#include <algorithm>

#include "core/TickPipeline.h"
#include "domain/ModifierUtil.h"
#include "mkt/Arbitrage.h"
#include "mkt/BlackMarket.h"
#include "mkt/Debt.h"
#include "mkt/FX.h"
#include "mkt/Futures.h"
#include "mkt/Insider.h"
#include "mkt/ManipulationDetect.h"
#include "mkt/Margin.h"
#include "mkt/OrderBook.h"
#include "mkt/Settlement.h"
#include "mkt/VolModel.h"
#include "rng/Streams.h"

namespace gf {
namespace {

constexpr int kAuctionCalls = 8;
constexpr std::size_t kMaxOrdersPerBook = 96;   // 与方案 §6.2 的 96 档上限一致

u32 mmController(int exch) { return 0xFFFF0000u + static_cast<u32>(exch); }

void settleSpotFill(GameState& st, u32 id, u8 res, u8 exch, bool buy, i64 qty, Fixed px) {
    Empire* e = st.empire(id);
    if (e == nullptr) return;
    const Fixed notional = px * Fixed(qty);
    const Fixed fee = marketFee(exch, notional);
    e->stock[res] += buy ? Fixed(qty) : -Fixed(qty);
    Fixed& cash = id == kPlayerId ? st.market.margin.cash : e->treasury;
    cash += (buy ? -notional : notional) - fee;
    e->treasury = cash;
    if (id != kPlayerId) return;
    auto& positions = st.market.positions;
    auto it = std::find_if(positions.begin(), positions.end(), [res](const Position& p) { return p.res == res; });
    if (it == positions.end()) {
        Position p;
        p.res = res;
        positions.push_back(p);
        it = positions.end() - 1;
    }
    // 出售生产所得库存不创建空头仓位。
    if (it->qty < 0) { it->qty = 0; it->avgCost = Fixed(0); }
    if (buy) {
        it->avgCost = (it->avgCost * Fixed(it->qty) + notional) / Fixed(it->qty + qty);
        it->qty += qty;
    } else {
        const i64 closed = std::min(qty, it->qty);
        it->realized += (px - it->avgCost) * Fixed(closed);
        it->qty -= closed;
        if (it->qty == 0) it->avgCost = Fixed(0);
    }
}

// 挂单不冻结资源；撮合前共享当前预算，裁减失去资金或库存支持的数量。
void validateRestingOrders(GameState& st, Book& b, u8 res, u8 exch) {
    std::array<Fixed, kMaxEmpires> cash{}, stock{};
    for (const auto& e : st.empires) {
        if (e.id >= kMaxEmpires || !e.alive) continue;
        cash[e.id] = fxMax(e.id == kPlayerId ? st.market.margin.cash : e.treasury, Fixed(0));
        stock[e.id] = fxMax(e.stock[res], Fixed(0));
    }
    std::stable_sort(b.orders.begin(), b.orders.end(), [](const Order& x, const Order& y) {
        if (x.buy != y.buy) return x.buy;
        if (x.px != y.px) return x.buy ? x.px > y.px : x.px < y.px;
        return x.seq < y.seq;
    });
    for (auto& o : b.orders) {
        if (o.owner == kNoEmpire) continue;
        i64 available = 0;
        if (o.owner < kMaxEmpires && o.px.rawValue() > 0) {
            if (o.buy) {
                Fixed unit = o.px + marketFee(exch, o.px);
                available = cash[o.owner].rawValue() / unit.rawValue();
                available = std::max<i64>(0, std::min(available, o.qty - o.filled));
                while (available > 0) {
                    Fixed value = o.px * Fixed(available);
                    if ((value + marketFee(exch, value)).rawValue() <= cash[o.owner].rawValue()) break;
                    --available;
                }
                Fixed value = o.px * Fixed(available);
                cash[o.owner] -= value + marketFee(exch, value);
            } else {
                available = std::max<i64>(0, std::min(stock[o.owner].rawValue() / FIX, o.qty - o.filled));
                stock[o.owner] -= Fixed(available);
            }
        }
        o.qty = o.filled + available;
    }
    b.orders.erase(std::remove_if(b.orders.begin(), b.orders.end(),
        [](const Order& o) { return o.qty <= o.filled; }), b.orders.end());
    bookRebuildLevels(b);
}

void resolveCrossedMarket(GameState& st, Book& b, u8 res, u8 exch) {
    bookExpireDayOrders(b, st.tick);
    validateRestingOrders(st, b, res, exch);
    (void)bookResolveCrossed(b, 64, [&](const Order& bid, const Order& ask, Fixed px, i64 qty) {
        settleSpotFill(st, bid.owner, res, exch, true, qty, px);
        settleSpotFill(st, ask.owner, res, exch, false, qty, px);
        auto& market = st.market;
        auto& mm = market.exchanges[exch].mm[res];
        if (bid.controller == mmController(exch)) { mm.inventory += Fixed(qty); mm.absorbed += qty; }
        if (ask.controller == mmController(exch)) { mm.inventory -= Fixed(qty); mm.absorbed -= qty; }
        ++market.tickFills;
        market.tickNotional += px * Fixed(qty);
        market.exchanges[exch].volumeTick += px * Fixed(qty);
    });
}

void stripController(Book& b, u32 controller) {
    auto it = std::remove_if(b.orders.begin(), b.orders.end(),
                             [controller](const Order& o) { return o.controller == controller; });
    if (it != b.orders.end()) {
        b.orders.erase(it, b.orders.end());
        bookRebuildLevels(b);
    }
}

/// 生成一批 AI 聚合订单流（"块单"），避免逐笔爆炸
void generateAiFlow(GameState& st, int batch) {
    MarketState& m = st.market;
    const std::size_t nE = st.empires.size();
    if (nE == 0) return;
    for (int e = 0; e < kExchangeCount; ++e) {
        ExchangeMarket& x = m.exchanges[static_cast<std::size_t>(e)];
        for (int c = 0; c < kCommodityCount; ++c) {
            const CommodityInfo& ci = commodityInfo(c);
            if (!ci.tradable) continue;
            Book& b = x.books[static_cast<std::size_t>(c)];
            if (b.halted) continue;
            // 稀疏化：每批只让一部分标的产生流
            if (!st.rng.chance(RngStream::Market, Fixed::pct(45))) continue;

            // 主体 0 是玩家：AI 的聚合流绝不能替玩家下单/结算
            u32 actorId = static_cast<u32>(1 + st.rng.pick(RngStream::Market, nE > 1 ? nE - 1 : 1));
            if (actorId >= nE) continue;
            const Empire& actor = st.empires[actorId];
            // 该主体在本所的净需求：库存低于基准 → 买；高于 → 卖
            Fixed stock = actor.stock[static_cast<std::size_t>(c)];
            const bool insiderHere = insiderTrace(st, actorId, static_cast<u8>(c)).rawValue() > Fixed::pct(50).rawValue();
            Fixed mid = b.mid.rawValue() > 0 ? b.mid : ci.basePrice;
            i64 baseline = ci.typicalVolume / 6 + 1;
            Fixed ratio = stock / Fixed(baseline);
            bool buy = ratio.rawValue() < FIX;
            if (insiderHere) buy = (actorId + static_cast<u32>(st.tick)) % 2 == 0;
            // 少量噪声交易者
            if (st.rng.chance(RngStream::Market, Fixed::pct(20))) buy = !buy;

            i64 qty = std::max<i64>(1, ci.typicalVolume / 900 + static_cast<i64>(st.rng.range(RngStream::Market, 1, 6)));
            // AI 预算约束：单季市场采购不超过国库的 25%，保留周转与维护余量。
            // 否则 AI 会在每季把现金全部换成库存，长期手无余钱、无法应对危机。
            if (buy) {
                Fixed mid2 = b.mid.rawValue() > 0 ? b.mid : ci.basePrice;
                Fixed estCost = Fixed::raw(mulDivSat(mid2.rawValue(), qty, 1));
                // 逐 tick 累计预算：预算按「本季起始国库的 25%」计，
                // 而不是逐笔判断 —— 逐笔判断会让 30 笔订单依次通过，
                // 合计仍能花掉九成储备。
                ActorMarketStats& st2 = actorStatsOf(m, actorId);
                // 采购预算按**上季净收入**计算，而不是国库百分比。
                // 国库百分比是几何式抽干：每季花掉 25% ⇒ 约 20 季抽空，
                // 与收入完全脱钩（实测 AI 合计国库在 30 季内由 39 万跌到 -30 万）。
                // 以收入为上限则天然可持续：支出随收入同步伸缩。
                //
                // 但纯流量口径在"国库与收入量纲脱节"时会失灵：lastIncome ≈ 70
                // 时预算恒为 300 cr/季，帝国坐拥 6,000 万也几乎不采购。
                // 因此再按国库存量给 0.5%/季 的补充额度（有上限，不会抽干国库）。
                Fixed base = actor.lastIncome.rawValue() > 0 ? actor.lastIncome : Fixed(0);
                Fixed budget = base * Fixed::pct(50);
                Fixed stockBudget = actor.treasury * Fixed::pct(1) / Fixed(2);
                budget = fxMax(budget, stockBudget);
                // 保底：收入尚未统计出来时（开局）给一个很小的额度
                if (budget.rawValue() < Fixed(300).rawValue()) budget = Fixed(300);
                // 不超过国库本身
                if (budget.rawValue() > actor.treasury.rawValue()) budget = fxMax(actor.treasury, Fixed(0));
                if (st2.spentThisTick.rawValue() + estCost.rawValue() > budget.rawValue()) continue;
                st2.spentThisTick += estCost;
            }
            OrderRequest req;
            req.owner = actorId;
            req.res = static_cast<u8>(c);
            req.exch = static_cast<u8>(e);
            req.buy = buy;
            req.qty = qty;
            req.kind = OrderKind::Limit;
            req.tif = Tif::Day;
            // 激进程度：库存越极端越激进（吃价）
            Fixed aggression = fxClamp(fxAbs(Fixed(1) - ratio), Fixed(0), Fixed(2));
            Fixed offset = mid * (Fixed(1) - aggression * Fixed::pct(2)) / Fixed(1000);
            if (offset.rawValue() < 1) offset = Fixed::raw(1);
            req.px = buy ? (mid + offset) : (mid - offset);
            if (req.px.rawValue() <= 0) continue;
            (void)marketSubmitOrder(st, req);
            (void)batch;
        }
    }
}

}  // namespace

Fixed marketFee(int exch, Fixed notional) {
    const ExchangeInfo& ei = exchangeInfo(exch);
    return notional * ei.fee;
}

OrderAck marketSubmitOrder(GameState& st, const OrderRequest& req) {
    OrderAck ack;
    if (req.res >= kCommodityCount) {
        ack.reason = "非法资源编号";
        return ack;
    }
    if (req.exch >= kExchangeCount) {
        ack.reason = "非法交易所";
        return ack;
    }
    if (req.qty <= 0) {
        ack.reason = "数量必须为正";
        return ack;
    }
    const CommodityInfo& ci = commodityInfo(req.res);
    if (!ci.tradable) {
        ack.reason = std::string(commodityName(req.res)) + " 不可直接交易";
        return ack;
    }
    MarketState& m = st.market;
    ExchangeMarket& x = m.exchanges[req.exch];
    Book& b = x.books[req.res];
    if (b.halted) {
        ack.reason = "该标的已熔断";
        return ack;
    }
    if (b.mid.rawValue() <= 0) b.mid = ci.basePrice;
    if (b.last.rawValue() <= 0) b.last = b.mid;
    bookExpireDayOrders(b, st.tick);
    validateRestingOrders(st, b, req.res, req.exch);

    // ---- 购买力 / 持仓校验 ----
    // 没有足够的钱不能买，没有足够的货不能卖。做市商与合成流（kNoEmpire）豁免。
    if (req.owner != kNoEmpire) {
        if (st.empire(req.owner) == nullptr || !st.empire(req.owner)->alive) {
            ack.reason = "非法交易主体";
            return ack;
        }
        if (req.buy) {
            Fixed refPx = (req.kind != OrderKind::Market && req.px.rawValue() > 0)
                              ? req.px
                              : bookEstimatePrice(b, true, req.qty, nullptr);
            Fixed need = Fixed::raw(mulDivSat(refPx.rawValue(), req.qty, 1));
            need += marketFee(req.exch, need);
            if (req.kind == OrderKind::Market) {
                // 用相同的撮合规则预演，跳过自己的挂单并保留逐笔手续费精度。
                Book preview = b;
                MkOrder incoming;
                incoming.owner = req.owner; incoming.buy = true; incoming.qty = req.qty;
                incoming.kind = OrderKind::Market;
                const auto result = matchOrder(preview, incoming, Fixed(0));
                need = Fixed(0);
                for (const auto& fill : result.consumed) {
                    Fixed value = fill.px * Fixed(fill.qty);
                    need += value + marketFee(req.exch, value);
                }
            }
            Fixed avail = (req.owner == kPlayerId)
                              ? m.margin.cash
                              : (st.empire(req.owner) != nullptr ? st.empire(req.owner)->treasury : Fixed(0));
            if (avail.rawValue() < need.rawValue()) {
                ack.reason = "购买力不足：需要 " + fixedStr(need, 0) + "，可用 " + fixedStr(avail, 0);
                return ack;
            }
        } else {
            const Empire* ow = st.empire(req.owner);
            Fixed avail = ow != nullptr ? ow->stock[req.res] : Fixed(0);
            if (avail.rawValue() < Fixed(req.qty).rawValue()) {
                ack.reason = "持仓不足：要卖 " + groupDigits(req.qty) + "，持有 " +
                             groupDigits(avail.rawValue() / FIX);
                return ack;
            }
        }
    }

    // 涨跌停保护：单笔订单价格不得偏离中值 30%
    Fixed px = req.px;
    if (req.kind != OrderKind::Market) {
        if (px.rawValue() <= 0) {
            ack.reason = "限价单必须给出正价格";
            return ack;
        }
        Fixed band = b.mid * Fixed::pct(30);
        if (fxAbs(px - b.mid).rawValue() > band.rawValue()) {
            ack.reason = "价格超出 ±30% 涨跌停带";
            return ack;
        }
    }

    MkOrder order;
    order.id = m.nextOrderId;
    order.owner = req.owner;
    order.buy = req.buy;
    order.qty = req.qty;
    order.px = px;
    order.kind = req.kind;

    MkResult r = matchOrder(b, order, Fixed(0));
    ack.filled = r.filled;
    ack.remaining = r.remaining;
    ack.avgPx = r.avgPx;
    ack.impact = r.impactTemp;
    for (const auto& fill : r.consumed) {
        Fixed value = fill.px * Fixed(fill.qty);
        ack.notional += value;
        ack.fee += marketFee(req.exch, value);
    }

    // 入簿剩余部分
    if (r.remaining > 0 && req.kind != OrderKind::Market) {
        Order rest;
        rest.id = m.nextOrderId++;
        rest.owner = req.owner;
        rest.seq = static_cast<u32>(m.nextSeq++);
        rest.px = px;
        rest.qty = req.qty;
        rest.filled = r.filled;
        rest.shown = (req.kind == OrderKind::Iceberg && req.shown > 0) ? req.shown : r.remaining;
        rest.buy = req.buy;
        rest.kind = req.kind;
        rest.tif = req.tif;
        rest.exch = req.exch;
        rest.res = req.res;
        rest.placedTick = st.tick;
        rest.controller = req.controller;
        rest.synthetic = false;
        bookInsert(b, rest);
        ack.orderId = rest.id;
        manipRecordPlace(st, req.owner, req.buy, req.qty);
    } else {
        ack.orderId = 0;
        ack.remaining = 0;
    }
    // 做市商库存回填：被动方是某交易所做市商时，其库存随之变化。
    // 这是价格收敛的核心 —— 库存偏移会推动做市商调整报价。
    {
        u32 mmId = mmController(req.exch);
        MarketMakerState& mm = x.mm[req.res];
        if (mm.inventoryLimit <= 0) mm.inventoryLimit = std::max<i64>(500, ci.typicalVolume / 20);
        for (const auto& f : r.consumed) {
            if (f.passiveController != mmId) continue;
            Fixed delta = Fixed(f.qty);
            mm.inventory += req.buy ? -delta : delta;
            mm.absorbed += req.buy ? -f.qty : f.qty;
        }
        Fixed cap = Fixed(mm.inventoryLimit * 6);
        mm.inventory = fxClamp(mm.inventory, -cap, cap);
        // 黑市吸收抛压 → 套利压力上升，压缩溢价
        if (req.exch == kExchBZ && !req.buy) {
            m.arbPressure[req.res] =
                fxClamp(m.arbPressure[req.res] + Fixed::pct(10), Fixed(-1), Fixed(1));
        }
        if (req.exch == kExchCX && req.buy) {
            m.arbPressure[req.res] =
                fxClamp(m.arbPressure[req.res] + Fixed::pct(6), Fixed(-1), Fixed(1));
        }
        // ---- 溢价必须**即时**被套利流压缩 ----
        // 早期只在 blackMarketPhase（tick 推进时）压缩溢价，
        // 于是「买 CX → 卖 BZ」可以在**同一 tick 内无限重复**：
        // 溢价始终停在原处，玩家脚本一轮就把 12 万做到 360 万（30 倍）。
        // 真实市场里套利成交本身就会立刻收窄价差，这里照此建模：
        // 成交量相对典型量越大，压缩越强。
        if (r.filled > 0) {
            const CommodityInfo& cci = commodityInfo(static_cast<int>(req.res));
            Fixed typical = Fixed(std::max<i64>(1, cci.typicalVolume / 4 + 1));
            Fixed flow = Fixed(r.filled) / typical;
            if (flow.rawValue() > Fixed(3).rawValue()) flow = Fixed(3);
            // 每成交 1 倍典型量 → 压缩 18%；跨所套利（一边买白市、一边抛黑市）额外压缩
            Fixed squeeze = flow * Fixed::pct(18);
            if (req.exch == kExchBZ && !req.buy) squeeze += flow * Fixed::pct(12);
            if (req.exch == kExchCX && req.buy) squeeze += flow * Fixed::pct(10);
            m.blackMarketPremium =
                fxClamp(m.blackMarketPremium - squeeze, Fixed::pct(2), Fixed::pct(300));
            // 溢价变了就必须**立刻重新报价**：做市商报价原本只在 tick 推进时刷新，
            // 于是同一 tick 内的连续套利始终看到旧盘口，压缩形同虚设。
        }
    }

    if (r.filled > 0) {
        manipRecordFill(st, req.owner, req.buy, r.filled);
        for (const auto& f : r.consumed) {
            if (f.passiveOwner != kNoEmpire) manipRecordFill(st, f.passiveOwner, !req.buy, f.qty);
        }
        m.tickFills += 1;
        m.tickNotional += ack.notional;
        x.volumeTick += ack.notional;
        // 价格拉升幅度（pump & dump 检测）
        ActorMarketStats& s = actorStatsOf(m, req.owner);
        if (r.impactTemp.rawValue() > 0) s.priceRunUp = fxLerp(s.priceRunUp, r.impactTemp, Fixed::pct(35));
    }
    if (r.filled > 0) {
        ack.accepted = true;
    } else if (r.remaining > 0 && req.kind != OrderKind::Market) {
        ack.accepted = true;
    } else {
        ack.accepted = false;
        ack.reason = "无对手方成交量（残单未入簿：市价单）";
    }

    // 逐笔按实际成交价结算双方，避免平均价舍入破坏资金守恒。
    for (const auto& fill : r.consumed) {
        settleSpotFill(st, req.owner, req.res, req.exch, req.buy, fill.qty, fill.px);
        settleSpotFill(st, fill.passiveOwner, req.res, req.exch, !req.buy, fill.qty, fill.px);
    }
    if (r.filled > 0) marketMakerRequote(st);

    // 订单簿有界
    if (b.orders.size() > kMaxOrdersPerBook) {
        std::size_t excess = b.orders.size() - kMaxOrdersPerBook;
        std::stable_sort(b.orders.begin(), b.orders.end(), [](const Order& p, const Order& q) {
            return p.placedTick < q.placedTick;
        });
        b.orders.erase(b.orders.begin(), b.orders.begin() + static_cast<std::ptrdiff_t>(excess));
        bookRebuildLevels(b);
    }
    resolveCrossedMarket(st, b, req.res, req.exch);
    b.mid = bookMid(b);
    b.spread = bookSpread(b);
    return ack;
}

i64 marketCancelOrder(GameState& st, u8 exch, u8 res, u64 orderId, bool* found) {
    if (exch >= kExchangeCount || res >= kCommodityCount) {
        if (found) *found = false;
        return 0;
    }
    Book& b = st.market.exchanges[exch].books[res];
    Order* o = bookFind(b, orderId);
    if (o == nullptr) {
        if (found) *found = false;
        return 0;
    }
    i64 remaining = o->qty - o->filled;
    u32 actor = o->owner;
    bookRemove(b, orderId);
    if (found) *found = true;
    if (actor != kNoEmpire) manipRecordCancel(st, actor, remaining);
    return remaining;
}

void marketMakerRequote(GameState& st) {
    MarketState& m = st.market;
    for (int e = 0; e < kExchangeCount; ++e) {
        ExchangeMarket& x = m.exchanges[static_cast<std::size_t>(e)];
        for (int c = 0; c < kCommodityCount; ++c) {
            const CommodityInfo& ci = commodityInfo(c);
            Book& b = x.books[static_cast<std::size_t>(c)];
            MarketMakerState& mm = x.mm[static_cast<std::size_t>(c)];
            stripController(b, mmController(e));
            if (!ci.tradable || b.halted) continue;
    
            // 库存上限与报价规模都以该标的的典型成交量为标度
            const i64 typical = std::max<i64>(1000, ci.typicalVolume);
            if (mm.inventoryLimit <= 0) mm.inventoryLimit = std::max<i64>(200, typical / 10);
    
            // 库存回归只在季度结束执行，重报价本身不补库存。
    
            Fixed mid = b.mid.rawValue() > 0 ? b.mid : ci.basePrice;
            // 黑市以「白市价 × (1 + 当前溢价)」为报价锚点；溢价是状态量，
            // 会被库存与套利流压缩，再随结构值缓慢回归 ⇒ 套利窗口会关闭。
            Fixed anchor = mid;
            if (e == kExchBZ) {
                Fixed white = m.exchanges[kExchCX].books[static_cast<std::size_t>(c)].mid;
                if (white.rawValue() <= 0) white = ci.basePrice;
                anchor = white * (Fixed(1) + m.blackMarketPremium);
            }
            if (anchor.rawValue() <= 0) anchor = ci.basePrice;
    
            // ---- 库存偏移（无量纲比值，避免量纲错误）----
            // 持有量占典型成交量 100% ⇒ 报价偏移 12%。
            Fixed invRatio = mm.inventory / Fixed(typical);
            invRatio = fxClamp(invRatio, Fixed(-3), Fixed(3));
            Fixed reservation = anchor - anchor * invRatio * Fixed::pct(12);
    
            Fixed variance = b.sigma * b.sigma;
            Fixed inventoryRisk = fxAbs(invRatio) * Fixed::pct(2);
            Fixed embargoPenalty = x.embargo * Fixed::pct(3);
            Fixed halfSpread = mm.baseSpread + mm.gamma * variance + inventoryRisk + embargoPenalty;
            if (halfSpread.rawValue() < Fixed::ratio(ci.basePrice, 2, 1000).rawValue())
                halfSpread = Fixed::ratio(ci.basePrice, 2, 1000);
    
            // 库存越限时不是"撤单"，而是「加宽价差 + 缩减规模」。
            // 这样市场永远可交易（不会停摆），但继续朝同一方向套利的成本急剧上升。
            Fixed invAbs = fxAbs(invRatio);
            mm.halted = false;
            mm.haltReason.clear();
            if (invAbs.rawValue() > Fixed(1).rawValue()) {
                mm.haltReason = invRatio.rawValue() > 0 ? "多头库存偏高：买盘收窄、价差加宽"
                                                        : "空头库存偏高：卖盘收窄、价差加宽";
            }
            // 价差随库存偏离加宽
            halfSpread = halfSpread * (Fixed(1) + invAbs);
            const Fixed sizeScale = Fixed(1) / (Fixed(1) + invAbs * Fixed(2));
            const int levels = 5;
            // 各场所流动性不同：核心区最深，边疆次之，黑市（走私渠道）最薄。
            // 黑市薄 ⇒ 大额抛售会迅速压低价格，套利窗口关闭得更快。
            static constexpr i64 kLiqNum[kExchangeCount] = {10, 6, 3};
            static constexpr i64 kLiqDen = 10;
            i64 baseQty = std::max<i64>(2, mulDivSat(typical / 40, kLiqNum[e], kLiqDen));
            // ---- 做市商库存上限 ⇒ 停止报价买盘 ----
            //
            // 这是修复「市场凭空印钱」的关键。
            // 做市商是合成主体（owner = kNoEmpire），`settleSpotFill` 对它直接 return，
            // 也就是它的买单**没有资金账户**：任何帝国把库存卖给它都会凭空得到现金。
            // 旧实现里做市商还会以公允价值**无限量**买入，于是它成了库存的无底沉淀池 ——
            // 实测 11 个 AI 在 154 季里通过市场净收 2.1 亿 cr（约 136 万 cr/季），
            // 而它们同期的 economyPhase 净收入只有 40~330 cr/季。
            //
            // 现在：库存达到上限即不再提供买盘。想做市商继续买，必须先有真实买盘
            // 把它的库存买走（或季度回归消耗掉）。这同时给了「抛售压力」真实的定价后果。
            const bool canBid = mm.inventory.rawValue() < (mm.inventoryLimit * 3);
            for (int i = 0; i < levels; ++i) {
                for (int side = 0; side < 2; ++side) {
                    bool isBid = (side == 0);
                    if (isBid && !canBid) continue;   // 库存已满：只报卖盘
                    Order o;
                    o.id = m.nextOrderId++;
                    o.seq = static_cast<u32>(m.nextSeq++);
                    o.owner = kNoEmpire;
                    o.controller = mmController(e);
                    Fixed step = halfSpread + Fixed::ratio(ci.basePrice, i, 400);
                    o.px = isBid ? (reservation - step) : (reservation + step);
                    if (o.px.rawValue() <= 0) continue;
                    // 规模按库存偏离衰减，但至少保留 1 单位 ⇒ 市场永不完全干涸
                    i64 q = mulDivSat(baseQty * (i + 1), sizeScale.rawValue(), FIX);
                    if (q < 1) q = 1;
                    o.qty = q;
                    o.shown = q;
                    o.buy = isBid;
                    o.kind = OrderKind::Limit;
                    o.tif = Tif::Gtc;
                    o.exch = static_cast<u8>(e);
                    o.res = static_cast<u8>(c);
                    o.placedTick = st.tick;
                    o.synthetic = true;
                    b.orders.push_back(o);
                    ++mm.quotesPlaced;
                }
            }
            bookRebuildLevels(b);
            b.mid = bookMid(b);
            b.spread = bookSpread(b);
            // 兜底：单边真空时用最后成交价补一档最小流动性，避免市场彻底停摆
            if (b.bids.empty() || b.asks.empty()) {
                Fixed px = b.last.rawValue() > 0 ? b.last : ci.basePrice;
                i64 q = baseQty * levels;
                for (int side = 0; side < 2; ++side) {
                    bool isBid = (side == 0);
                    if (isBid && !b.bids.empty()) continue;
                    if (!isBid && !b.asks.empty()) continue;
                    // 库存已满时连兜底买盘也不提供 —— 否则「停止买入」会被这里绕过
                    if (isBid && !canBid) continue;
                    Order o;
                    o.id = m.nextOrderId++;
                    o.seq = static_cast<u32>(m.nextSeq++);
                    o.owner = kNoEmpire;
                    o.controller = mmController(e);
                    o.px = isBid ? px * Fixed::pct(88) : px * Fixed::pct(112);
                    if (o.px.rawValue() <= 0) continue;
                    o.qty = q;
                    o.shown = q;
                    o.buy = isBid;
                    o.exch = static_cast<u8>(e);
                    o.res = static_cast<u8>(c);
                    o.placedTick = st.tick;
                    o.synthetic = true;
                    b.orders.push_back(o);
                }
                bookRebuildLevels(b);
                b.mid = bookMid(b);
                b.spread = bookSpread(b);
            }
            resolveCrossedMarket(st, b, static_cast<u8>(c), static_cast<u8>(e));
            b.liquidityDrained = (b.bids.empty() || b.asks.empty());
            mm.skew = mid.rawValue() > 0
                          ? Fixed::raw(mulDivSat(reservation.rawValue() - mid.rawValue(), FIX, mid.rawValue()))
                          : Fixed(0);
        }
    }
}


void marketUpdateSpotIndex(GameState& st) {
    MarketState& m = st.market;
    for (int c = 0; c < kCommodityCount; ++c) {
        // 成交量加权
        Fixed num = Fixed(0);
        Fixed den = Fixed(0);
        for (int e = 0; e < kExchangeCount; ++e) {
            const ExchangeMarket& x = m.exchanges[static_cast<std::size_t>(e)];
            Fixed px = x.books[static_cast<std::size_t>(c)].mid;
            Fixed w = Fixed(1) + (e == kExchCX ? Fixed(2) : Fixed(0));
            num += px * w;
            den += w;
        }
        m.spotIndex[static_cast<std::size_t>(c)] = den.rawValue() > 0 ? num / den : commodityInfo(c).basePrice;
    }
}

void marketFundamentalFlow(GameState& st) {
    // 产能/消耗差异由"知情交易者"的限价单发现进价格
    MarketState& m = st.market;
    for (int c = 0; c < kCommodityCount; ++c) {
        const CommodityInfo& ci = commodityInfo(c);
        if (!ci.tradable) continue;
        i64 net = 0;
        for (const auto& e : st.empires) {
            if (!e.alive) continue;
            net += (e.capacity[static_cast<std::size_t>(c)] - e.demand[static_cast<std::size_t>(c)]).rawValue() / FIX;
        }
        if (net == 0) continue;
        Book& b = m.exchanges[kExchCX].books[static_cast<std::size_t>(c)];
        Fixed mid = b.mid.rawValue() > 0 ? b.mid : ci.basePrice;
        // 净盈余 → 卖方信息；净短缺 → 买方信息
        i64 qty = std::min<i64>(std::abs(net), std::max<i64>(1, ci.typicalVolume / 400));
        if (qty <= 0) continue;
        OrderRequest req;
        req.owner = kNoEmpire;
        req.res = static_cast<u8>(c);
        req.exch = kExchCX;
        req.buy = net < 0;
        req.qty = qty;
        req.kind = OrderKind::Limit;
        req.tif = Tif::Day;
        Fixed offset = mid * Fixed::bp(35);
        if (offset.rawValue() < 1) offset = Fixed::raw(1);
        req.px = req.buy ? mid + offset : mid - offset;
        if (req.px.rawValue() <= 0) continue;
        (void)marketSubmitOrder(st, req);
    }
}

void marketAuctionCalls(GameState& st, TickReport& rep, int calls) {
    for (int i = 0; i < calls; ++i) {
        generateAiFlow(st, i);
    }
    rep.fills = st.market.tickFills;
    rep.notional = st.market.tickNotional;
}

void marketUpdateVolatility(GameState& st) {
    MarketState& m = st.market;
    const bool boom = hasModifier(st.modifierBits, kModBoomCycle);
    for (int c = 0; c < kCommodityCount; ++c) {
        Fixed returns = Fixed(0);
        i64 weight = 0;
        for (const auto& exchange : m.exchanges) {
            const auto& b = exchange.books[c];
            if (b.open.rawValue() <= 0) continue;
            const i64 volume = std::max<i64>(1, b.volume);
            returns += ((b.last - b.open) / b.open) * Fixed(volume);
            weight += volume;
        }
        // 同一商品共享一个波动率状态，每季只能推进一次。
        volUpdate(m.vol[c], weight > 0 ? returns / Fixed(weight) : Fixed(0));
        for (int e = 0; e < kExchangeCount; ++e) {
            Book& b = m.exchanges[static_cast<std::size_t>(e)].books[static_cast<std::size_t>(c)];
            b.sigma = volSigma(m.vol[c]);
            b.var20 = fxMax((b.var20 * Fixed(19) + Fixed(b.volume)) / Fixed(20), Fixed(1));
            if (boom) b.sigma = b.sigma * Fixed::raw(1200);
            // 临时冲击回归（λ=0.5）
            b.impactTemp = impactDecay(b.impactTemp);
            // 移动高低点
            b.open = b.last;
        }
    }
}

void marketSettleMargins(GameState& st, TickReport& rep) {
    marginCascade(st, rep);
    settlementPhase(st, rep);
    futuresSettleExpiry(st, rep);
}

void marketPruneBooks(GameState& st) {
    for (auto& x : st.market.exchanges) {
        for (auto& mm : x.mm) mm.inventory = mm.inventory * Fixed::pct(85);
        for (auto& b : x.books) {
            bookExpireDayOrders(b, st.tick);
            if (b.orders.size() > kMaxOrdersPerBook) {
                std::stable_sort(b.orders.begin(), b.orders.end(), [](const Order& p, const Order& q) {
                    return p.placedTick < q.placedTick;
                });
                b.orders.erase(b.orders.begin(),
                               b.orders.begin() + static_cast<std::ptrdiff_t>(b.orders.size() - kMaxOrdersPerBook));
                bookRebuildLevels(b);
            }
            b.volume = 0;
            b.vwap = Fixed(0);
        }
        x.volumeTick = Fixed(0);
    }
    st.market.tickFills = 0;
    st.market.tickNotional = Fixed(0);
    // 重置逐 tick 采购预算
    for (auto& s : st.market.actorStats) s.spentThisTick = Fixed(0);
}

void marketApplyShock(GameState& st, u8 res, Fixed magnitude, std::string_view cause, int exch) {
    if (res >= kCommodityCount) return;
    MarketState& m = st.market;
    for (int e = 0; e < kExchangeCount; ++e) {
        if (exch >= 0 && e != exch) continue;
        Book& b = m.exchanges[static_cast<std::size_t>(e)].books[res];
        Fixed before = b.sigma;
        Fixed delta = b.mid * magnitude;
        if (delta.rawValue() == 0) continue;
        // 平移整个簿（模拟信息瞬间重定价）
        for (auto& o : b.orders) {
            if (o.owner != kNoEmpire) continue;  // 用户限价是委托条件，行情冲击不能改价。
            o.px = Fixed::raw(o.px.rawValue() + delta.rawValue());
            if (o.px.rawValue() < 1) o.px = Fixed::raw(1);
        }
        b.mid = Fixed::raw(b.mid.rawValue() + delta.rawValue());
        b.last = b.mid;
        if (b.high.rawValue() < b.mid.rawValue()) b.high = b.mid;
        if (b.low.rawValue() > b.mid.rawValue() || b.low.rawValue() == 0) b.low = b.mid;
        bookRebuildLevels(b);
        VolState& v = m.vol[res];
        volInjectJump(v, magnitude * Fixed(2));
        v.lastSigma = volSigma(v);
        b.sigma = v.lastSigma;
        ShockRecord s;
        s.tick = st.tick;
        s.res = res;
        s.magnitude = magnitude;
        s.cause = std::string(cause);
        s.sigmaBefore = before;
        s.sigmaAfter = b.sigma;
        m.shocks.push_back(std::move(s));
    }
    if (m.shocks.size() > 256) m.shocks.erase(m.shocks.begin(), m.shocks.begin() + 64);
    // 期货曲线跟随
    futuresUpdateAll(st);
    // 黑市跟随
    blackMarketUpdate(st);
}

void phaseMarket(GameState& st, TickReport& rep) {
    MarketState& m = st.market;
    rep.fills = 0;
    rep.notional = Fixed(0);

    // 1) 新息到达：基本面漂移以知情限价单的形式进入价格
    marketFundamentalFlow(st);

    // 2) 8 次 auction call：AI 聚合订单流 + 集合竞价/连续竞价混合
    marketAuctionCalls(st, rep, kAuctionCalls);

    // 3) 做市重报价
    marketMakerRequote(st);

    // 4) 内幕信号生成与消费
    insiderEmitSignals(st);
    insiderConsume(st);

    // 5) 操纵检测与监管裁决
    std::vector<ManipFinding> findings = manipulationDetect(st);
    if (!findings.empty()) manipulationEnforce(st, findings);

    // 6) 跨所套利收敛 / 汇价 / 黑市
    arbitrageUpdate(st);
    fxUpdate(st);
    blackMarketUpdate(st);

    // 7) 指数与簿维护
    marketUpdateSpotIndex(st);
    futuresUpdateAll(st);

    for (int c = 0; c < kCommodityCount; ++c) {
        m.blackMarketPrice[static_cast<std::size_t>(c)] =
            m.exchanges[kExchBZ].books[static_cast<std::size_t>(c)].mid;
    }
}

}  // namespace gf
