#include "mkt/Settlement.h"

#include <algorithm>

#include "core/TickPipeline.h"
#include "mkt/Debt.h"
#include "mkt/Margin.h"
#include "mkt/OrderBook.h"

namespace gf {

bool escrowOpen(GameState& st, u32 payer, u32 payee, Fixed amount, u8 res, i64 qty, int termTicks,
                std::string* err) {
    Empire* p = st.empire(payer);
    if (p == nullptr || st.empire(payee) == nullptr) {
        if (err) *err = "非法主体";
        return false;
    }
    if (amount.rawValue() <= 0) {
        if (err) *err = "托管金额必须为正";
        return false;
    }
    Fixed available = payer == kPlayerId ? st.market.margin.cash : p->treasury;
    if (available.rawValue() < amount.rawValue()) {
        if (err) *err = "资金不足：可用 " + fixedStr(available, 0) + "，需要 " + fixedStr(amount, 0);
        return false;
    }
    EscrowRecord e;
    e.id = st.market.nextEscrowId++;
    e.payer = payer;
    e.payee = payee;
    e.amount = amount;
    e.res = res;
    e.qty = qty;
    e.openedTick = st.tick;
    e.termTicks = termTicks < 1 ? 4 : termTicks;
    st.market.escrows.push_back(e);
    if (payer == kPlayerId) {
        st.market.margin.cash -= amount;
        p->treasury = st.market.margin.cash;
    } else {
        p->treasury -= amount;
    }
    st.logEvent(LogPhase::Market, "market.escrow.open",
                "开立托管 #" + std::to_string(e.id) + "：" + fixedStr(amount, 0) + " cr，期限 " +
                    std::to_string(e.termTicks) + " 季（降低对手方违约概率）",
                payer, amount);
    return true;
}

bool escrowRelease(GameState& st, u32 escrowId, bool toPayee, std::string* err) {
    for (auto& e : st.market.escrows) {
        if (e.id != escrowId) continue;
        if (e.released || e.breached) {
            if (err) *err = "该托管已结算";
            return false;
        }
        e.released = true;
        Empire* target = st.empire(toPayee ? e.payee : e.payer);
        if (target != nullptr) {
            target->treasury += e.amount;
            if (target->id == kPlayerId) st.market.margin.cash = target->treasury;
        }
        st.logEvent(LogPhase::Market, "market.escrow.release",
                    "释放托管 #" + std::to_string(e.id) + " → " + (toPayee ? "收款方" : "付款方"), e.payer,
                    e.amount);
        return true;
    }
    if (err) *err = "找不到该托管记录";
    return false;
}

bool insurePosition(GameState& st, u8 res, i64 cover, std::string* err) {
    if (res >= kCommodityCount || !commodityInfo(res).tradable || cover <= 0) {
        if (err) *err = "投保需要可交易资源和正数数量";
        return false;
    }
    i64 covered = 0;
    for (const auto& p : st.market.insurancePolicies)
        if (p.res == res && p.expiresTick > st.tick && p.remaining.rawValue() > 0) covered += p.qty;
    const Empire* player = st.empire(kPlayerId);
    if (player == nullptr || cover > player->stock[res].rawValue() / FIX - covered) {
        if (err) *err = "投保量超过尚未投保的现货库存";
        return false;
    }
    const CommodityInfo& ci = commodityInfo(res);
    Fixed spot = st.market.exchanges[kExchCX].books[res].mid;
    if (spot.rawValue() <= 0) {
        if (err) *err = "当前没有有效价格";
        return false;
    }
    Fixed notional = Fixed::raw(mulDivSat(spot.rawValue(), cover, 1));
    // 保费 = 名义额 × (1% + 2σ)
    Fixed premium = notional * (Fixed::pct(1) + ci.volatility * Fixed(2));
    if (st.market.margin.cash.rawValue() < premium.rawValue()) {
        if (err) *err = "保费不足：需要 " + fixedStr(premium, 0);
        return false;
    }
    st.market.margin.cash -= premium;
    st.empires[kPlayerId].treasury = st.market.margin.cash;
    InsurancePolicy policy;
    policy.res = res;
    policy.qty = cover;
    policy.entry = spot;
    policy.remaining = fxMin(notional, Fixed(50000));
    policy.expiresTick = st.tick + 4;
    st.market.insurancePolicies.push_back(policy);
    st.logEvent(LogPhase::Market, "market.insure",
                "为【" + std::string(commodityName(res)) + "】投保 " + std::to_string(cover) + " 单位，保费 " +
                    fixedStr(premium, 0) + "（σ=" + fixedStrPlain(ci.volatility, 3) + "）",
                kPlayerId, premium);
    return true;
}

void insuranceSettle(GameState& st) {
    auto& m = st.market;
    Empire* player = st.empire(kPlayerId);
    if (player == nullptr) return;
    std::array<i64, kCommodityCount> available{};
    for (int c = 0; c < kCommodityCount; ++c) available[c] = std::max<i64>(0, player->stock[c].rawValue() / FIX);
    for (auto& p : m.insurancePolicies) {
        if (p.res >= kCommodityCount || p.expiresTick <= st.tick || p.remaining.rawValue() <= 0) continue;
        const i64 qty = std::min(p.qty, available[p.res]);
        available[p.res] -= qty;
        Fixed spot = m.exchanges[kExchCX].books[p.res].mid;
        Fixed loss = fxMax((p.entry - spot) * Fixed(qty) - p.paid, Fixed(0));
        Fixed payout = fxMin(fxMin(loss, p.remaining), fxMax(-m.margin.equity, Fixed(0)));
        if (payout.rawValue() <= 0) continue;
        p.remaining -= payout;
        p.paid += payout;
        m.margin.cash += payout;
        m.margin.equity += payout;
        player->treasury = m.margin.cash;
        st.logEvent(LogPhase::Market, "market.insure.payout", "保险赔付 " + fixedStr(payout, 0) + " cr", kPlayerId, payout);
    }
    m.insurancePolicies.erase(std::remove_if(m.insurancePolicies.begin(), m.insurancePolicies.end(),
        [&](const InsurancePolicy& p) { return p.expiresTick <= st.tick || p.remaining.rawValue() <= 0; }), m.insurancePolicies.end());
}

void settlementPhase(GameState& st, TickReport& rep) {
    debtSettle(st, rep);
    insuranceSettle(st);
    // 托管到期由 debtSettle 内的循环处理
}

}  // namespace gf
