#include "mkt/Debt.h"

#include <algorithm>

#include "core/TickPipeline.h"
#include "rng/Streams.h"

namespace gf {

Fixed totalDebt(const GameState& st, u32 borrower) {
    Fixed sum = Fixed(0);
    for (const auto& d : st.market.debts)
        if (d.borrower == borrower && !d.defaulted) sum += d.principal;
    return sum;
}

void creditUpdate(GameState& st) {
    MarketState& m = st.market;
    for (auto& e : st.empires) {
        // 现金比率
        Fixed cash = e.id == kPlayerId ? m.margin.cash : e.treasury;
        Fixed debt = totalDebt(st, e.id);
        Fixed ratio = debt.rawValue() > 0 ? Fixed::raw(mulDivSat(cash.rawValue(), FIX, debt.rawValue())) : Fixed(3);
        Fixed target = Fixed::pct(50) + fxClamp(ratio, Fixed(0), Fixed(2)) * Fixed::pct(20);
        // 声誉：他人的平均观感
        Fixed opi = Fixed(0);
        int n = 0;
        for (const auto& o : e.opinion) {
            opi += o;
            ++n;
        }
        if (n > 0) opi = Fixed::raw(opi.rawValue() / n);
        target += opi * Fixed::pct(15);
        // 战争风险惩罚
        int wars = 0;
        for (const auto& r : st.relations)
            if (r.atWar && (&r - st.relations.data()) / kMaxEmpires == e.id) ++wars;
        target -= Fixed::pct(5) * Fixed(wars);
        // 读档次数：AI 眼中"你会重试"也是信用折损
        target -= Fixed::pct(3) * Fixed(static_cast<i64>(st.rollbackCount));
        e.creditRating = fxClamp(fxLerp(e.creditRating, target, Fixed::pct(20)), Fixed::pct(5), Fixed::pct(98));
        m.creditRating[e.id] = e.creditRating;
    }
}

Fixed borrowingSpread(const GameState& st, u32 borrower) {
    Fixed rating = borrower < st.empires.size() ? st.empires[borrower].creditRating : Fixed::pct(50);
    // 评级 1.0 → 2% 利差；评级 0.05 → 28% 利差
    Fixed spread = Fixed::pct(30) - rating * Fixed::pct(28);
    return fxClamp(spread, Fixed::pct(2), Fixed::pct(45));
}

bool borrowCredits(GameState& st, u32 borrower, Fixed amount, int termTicks, std::string* err) {
    if (amount.rawValue() <= 0) {
        if (err) *err = "借款金额必须为正";
        return false;
    }
    if (termTicks < 1 || termTicks > 64) {
        if (err) *err = "--term 必须在 1..64";
        return false;
    }
    Empire* e = st.empire(borrower);
    if (e == nullptr) {
        if (err) *err = "非法主体";
        return false;
    }
    Fixed spread = borrowingSpread(st, borrower);
    // 借出规模受评级上限约束
    Fixed limit = (e->treasury + Fixed(60000)) * Fixed(3) * e->creditRating;
    Fixed existing = totalDebt(st, borrower);
    if (existing + amount > limit) {
        if (err)
            *err = "超出信用额度：评级 " + fixedStr(e->creditRating, 2) + " 允许至多 " + fixedStr(limit, 0) +
                   "，现有债务 " + fixedStr(existing, 0);
        return false;
    }
    DebtRecord d;
    d.id = st.market.nextDebtId++;
    d.borrower = borrower;
    d.lender = kNoEmpire;
    d.principal = amount;
    d.rate = spread;
    d.termTicks = termTicks;
    d.issuedTick = st.tick;
    st.market.debts.push_back(d);
    e->treasury += amount;
    if (borrower == kPlayerId) st.market.margin.cash += amount;
    st.logEvent(LogPhase::Market, "market.debt.issue",
                "发行债券 " + fixedStr(amount, 0) + " cr，利差 " + fixedStrPlain(spread * Fixed(100), 1) +
                    "%，期限 " + std::to_string(termTicks) + " 季",
                borrower, amount);
    return true;
}

bool repayCredits(GameState& st, u32 borrower, Fixed amount, std::string* err) {
    Empire* e = st.empire(borrower);
    if (e == nullptr) {
        if (err) *err = "非法主体";
        return false;
    }
    Fixed pool = borrower == kPlayerId ? st.market.margin.cash : e->treasury;
    if (pool.rawValue() < amount.rawValue()) {
        if (err) *err = "可用资金不足";
        return false;
    }
    Fixed remaining = amount;
    for (auto& d : st.market.debts) {
        if (d.borrower != borrower || d.defaulted) continue;
        if (remaining.rawValue() <= 0) break;
        Fixed pay = fxMin(d.principal, remaining);
        d.principal -= pay;
        remaining -= pay;
        if (borrower == kPlayerId) st.market.margin.cash -= pay;
        e->treasury -= pay;
    }
    st.market.debts.erase(std::remove_if(st.market.debts.begin(), st.market.debts.end(),
                                         [](const DebtRecord& d) { return d.principal.rawValue() <= 0; }),
                          st.market.debts.end());
    if (borrower == kPlayerId) e->treasury = st.market.margin.cash;
    st.logEvent(LogPhase::Market, "market.debt.repay", "偿还债务 " + fixedStr(amount, 0) + " cr", borrower,
                amount);
    return true;
}

Fixed defaultProbability(const GameState& st, u32 borrower) {
    const Empire* e = st.empire(borrower);
    if (e == nullptr) return Fixed(0);
    Fixed cash = borrower == kPlayerId ? st.market.margin.cash : e->treasury;
    Fixed debt = totalDebt(st, borrower);
    if (debt.rawValue() <= 0) return Fixed(0);
    Fixed cashRatio = Fixed::raw(mulDivSat(cash.rawValue(), FIX, debt.rawValue()));
    Fixed p = Fixed::pct(30);
    p -= fxClamp(cashRatio, Fixed(0), Fixed(3)) * Fixed::pct(10);
    p -= e->creditRating * Fixed::pct(15);
    // 托管降低违约概率
    for (const auto& es : st.market.escrows)
        if (es.payer == borrower && !es.released && !es.breached) p -= Fixed::pct(6);
    for (const auto& r : st.relations)
        if (r.atWar && (&r - st.relations.data()) / kMaxEmpires == borrower) p += Fixed::pct(8);
    return fxClamp(p, Fixed::pct(1), Fixed::pct(85));
}

void debtSettle(GameState& st, TickReport& rep) {
    MarketState& m = st.market;
    for (auto& d : m.debts) {
        if (d.defaulted || d.principal.rawValue() <= 0) continue;
        Fixed interest = d.principal * d.rate / Fixed(4);   // 每季利息
        Empire* b = st.empire(d.borrower);
        if (b == nullptr) continue;
        bool canPay = (d.borrower == kPlayerId ? m.margin.cash : b->treasury).rawValue() >= interest.rawValue();
        if (canPay) {
            if (d.borrower == kPlayerId) m.margin.cash -= interest;
            b->treasury -= interest;
        } else {
            // 付不出利息：违约判定
            Fixed p = defaultProbability(st, d.borrower);
            if (st.rng.chance(RngStream::Market, p)) {
                d.defaulted = true;
                b->creditRating -= Fixed::pct(20);
                b->treasury -= d.principal * Fixed::pct(20);   // 罚息
                st.logEvent(LogPhase::Market, kLogDefault,
                            "违约：主体 " + b->name + " 未能支付利息（本金 " + fixedStr(d.principal, 0) + "）",
                            d.borrower, d.principal);
                for (auto& e : st.empires)
                    if (e.id != d.borrower) e.addOpinion(d.borrower, Fixed::pct(-12));
                rep.eventsFired += 1;
            }
        }
        if (st.tick >= d.issuedTick + static_cast<u64>(d.termTicks)) {
            // 到期：尽量偿还，否则违约
            Fixed pay = fxMin(d.principal, d.borrower == kPlayerId ? m.margin.cash : b->treasury);
            d.principal -= pay;
            if (d.borrower == kPlayerId) {
                m.margin.cash -= pay;
                b->treasury = m.margin.cash;
            } else {
                b->treasury -= pay;
            }
            if (d.principal.rawValue() > 0) {
                d.defaulted = true;
                st.logEvent(LogPhase::Market, kLogDefault,
                            "到期违约：主体 " + b->name + " 未能偿付 " + fixedStr(d.principal, 0), d.borrower,
                            d.principal);
            }
        }
    }
    // 托管到期释放 / 违约
    for (auto& es : m.escrows) {
        if (es.released || es.breached) continue;
        if (st.tick >= es.openedTick + static_cast<u64>(es.termTicks)) {
            Fixed p = defaultProbability(st, es.payer);
            if (st.rng.chance(RngStream::Market, p * Fixed::pct(50))) {
                es.breached = true;
                st.logEvent(LogPhase::Market, kLogDefault,
                            "托管违约：主体 " + std::to_string(es.payer) + " 未履约，金额 " +
                                fixedStr(es.amount, 0),
                            es.payer);
            } else {
                es.released = true;
                Empire* payee = st.empire(es.payee);
                if (payee) payee->treasury += es.amount;
            }
        }
    }
    m.debts.erase(std::remove_if(m.debts.begin(), m.debts.end(),
                                 [](const DebtRecord& d) { return d.defaulted && d.principal.rawValue() <= 0; }),
                  m.debts.end());
    creditUpdate(st);
}

}  // namespace gf
