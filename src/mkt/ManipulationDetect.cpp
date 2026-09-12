#include "mkt/ManipulationDetect.h"

#include <algorithm>

#include "domain/ModifierUtil.h"
#include "mkt/OrderBook.h"
#include "rng/Streams.h"

namespace gf {

ActorMarketStats& actorStatsOf(MarketState& m, u32 actor) {
    for (auto& s : m.actorStats)
        if (s.actor == actor) return s;
    ActorMarketStats s;
    s.actor = actor;
    m.actorStats.push_back(s);
    return m.actorStats.back();
}

void manipRecordPlace(GameState& st, u32 actor, bool buy, i64 qty) {
    ActorMarketStats& s = actorStatsOf(st.market, actor);
    ++s.ordersPlaced;
    if (buy) s.buyVolume += qty;
    else s.sellVolume += qty;
    s.lastOrderTick = st.tick;
}

void manipRecordCancel(GameState& st, u32 actor, i64 qty) {
    ActorMarketStats& s = actorStatsOf(st.market, actor);
    ++s.ordersCancelled;
    s.qtyCancelled += qty;
}

void manipRecordFill(GameState& st, u32 actor, bool buy, i64 qty) {
    ActorMarketStats& s = actorStatsOf(st.market, actor);
    s.qtyFilled += qty;
    if (buy) s.buyVolume -= qty;
    else s.sellVolume -= qty;
}

Fixed regulatorHitProbability(const GameState& st, u32 actor) {
    Fixed base = Fixed::pct(3);
    // 监管强度：以 CX 的 regulator 为基准
    base += exchangeInfo(kExchCX).regulator * Fixed::pct(5);
    if (hasModifier(st.modifierBits, kModRegulatorSurge)) base = base * Fixed(2);
    if (hasModifier(st.modifierBits, kModPanopticonBoost)) base += Fixed::pct(20) * Fixed::pct(5);
    // violence：该主体的战争数
    int wars = 0;
    for (const auto& r : st.relations) {
        if (!r.atWar) continue;
        std::size_t idx = static_cast<std::size_t>(&r - st.relations.data());
        if (idx / kMaxEmpires == actor) ++wars;
    }
    base += Fixed::pct(5) * Fixed(wars);
    if (actor < st.empires.size()) base -= st.empires[actor].creditRating * Fixed::pct(3);
    return fxClamp(base, Fixed::pct(1), Fixed::pct(75));
}

std::vector<ManipFinding> manipulationDetect(const GameState& st) {
    std::vector<ManipFinding> out;
    const MarketState& m = st.market;

    for (const auto& s : m.actorStats) {
        // ---- wash trading：自买自卖（同控制人双边成交） ----
        i64 bothSides = std::min(s.buyVolume, s.sellVolume);
        if (bothSides > 0 && s.ordersPlaced >= 4) {
            Fixed score = Fixed::raw(mulDivSat(bothSides, FIX, std::max<i64>(1, s.buyVolume + s.sellVolume)));
            if (score.rawValue() > Fixed::pct(35).rawValue()) {
                ManipFinding f;
                f.actor = s.actor;
                f.kind = ManipKind::WashTrading;
                f.score = score;
                f.detail = "双边挂单比例 " + fixedStrPlain(score * Fixed(100), 1) + "%，疑似制造虚假成交量";
                out.push_back(std::move(f));
            }
        }
        // ---- spoofing：下单后撤比例 vs 成交比例 ----
        if (s.ordersPlaced >= 5) {
            Fixed cancelRatio =
                Fixed::raw(mulDivSat(s.qtyCancelled, FIX, std::max<i64>(1, s.qtyCancelled + s.qtyFilled)));
            Fixed fillRatio = Fixed::raw(mulDivSat(s.qtyFilled, FIX, std::max<i64>(1, s.qtyCancelled + s.qtyFilled)));
            if (cancelRatio.rawValue() > Fixed::pct(85).rawValue() && fillRatio.rawValue() < Fixed::pct(15).rawValue()) {
                ManipFinding f;
                f.actor = s.actor;
                f.kind = ManipKind::Spoofing;
                f.score = cancelRatio;
                f.detail = "撤单率 " + fixedStrPlain(cancelRatio * Fixed(100), 1) + "%，成交率 " +
                           fixedStrPlain(fillRatio * Fixed(100), 1) + "%：挂而不成交，诱导方向";
                out.push_back(std::move(f));
            }
        }
        // ---- corner / 逼空：囤积现货使对手无法交割 ----
        for (const auto& p : m.positions) {
            if (p.qty <= 0) continue;
            const CommodityInfo& ci = commodityInfo(static_cast<int>(p.res));
            i64 floatSupply = ci.typicalVolume * 2 + 1;
            Fixed share = Fixed::raw(mulDivSat(p.qty, FIX, floatSupply));
            if (share.rawValue() > Fixed::pct(55).rawValue()) {
                ManipFinding f;
                f.actor = s.actor;
                f.kind = ManipKind::Corner;
                f.score = share;
                f.detail = "持有 " + std::string(commodityName(p.res)) + " 流通量的 " +
                           fixedStrPlain(share * Fixed(100), 1) + "%，可能触发空头挤压";
                out.push_back(std::move(f));
            }
        }
        // ---- pump & dump：先拉升后抛售 ----
        if (s.priceRunUp.rawValue() > Fixed::pct(12).rawValue() && s.sellVolume > s.buyVolume * 2 + 1) {
            ManipFinding f;
            f.actor = s.actor;
            f.kind = ManipKind::PumpDump;
            f.score = s.priceRunUp;
            f.detail = "价格拉升 " + fixedStrPlain(s.priceRunUp * Fixed(100), 1) + "% 后大额净卖出";
            out.push_back(std::move(f));
        }
        // ---- front-running：透视玩家订单队列后抢先吃档 ----
        if (s.actor != kPlayerId && s.ordersPlaced > 0 && !st.pendingActions.empty()) {
            for (const auto& pa : st.pendingActions) {
                if (pa.kind != PlannedKind::Order) continue;
                if (pa.res == 0xFF) continue;
                ManipFinding f;
                f.actor = s.actor;
                f.kind = ManipKind::FrontRunning;
                f.score = Fixed::pct(60);
                f.detail = "在你的 " + std::string(commodityName(pa.res)) + " 订单进入队列后抢先成交（泛视网络）";
                out.push_back(std::move(f));
                break;
            }
        }
    }

    // ---- insider：事件公布前的知情建仓 ----
    // 注意：每个信号只应产生一次告警。否则同一个信号会在其存续期内
    // 连续多 tick 被反复检测，导致同一行为被重复罚款。
    for (const auto& sig : m.insiderSignals) {
        if (sig.consumed) continue;
        if (st.tick + 2 < sig.fireTick) continue;
        ManipFinding f;
        f.actor = sig.knower;
        f.kind = ManipKind::Insider;
        f.score = sig.magnitude;
        f.detail = "事件公布前在 " + std::string(commodityName(sig.res)) + " 建仓，知情痕迹评分 " +
                   fixedStrPlain(sig.magnitude * Fixed(100), 1);
        out.push_back(std::move(f));
        break;   // 每次检测最多报告一个内幕信号
    }
    return out;
}

void manipulationEnforce(GameState& st, const std::vector<ManipFinding>& findings) {
    // 每 tick 最多裁罚一次：批量罚款会在单季内掏空多个阵营的国库。
    int enforced = 0;
    for (const auto& f : findings) {
        if (enforced >= 1) {
            // 仍记录为「疑似但未裁罚」，保持可复盘性
            ManipulationRecord rec;
            rec.actor = f.actor;
            rec.kind = f.kind;
            rec.tick = st.tick;
            rec.score = f.score;
            rec.detail = f.detail + "（本季已有裁罚，暂缓处理）";
            st.market.manipulations.push_back(std::move(rec));
            continue;
        }
        MarketState& m = st.market;
        Fixed p = regulatorHitProbability(st, f.actor) * fxClamp(f.score, Fixed(0), Fixed(1));
        bool caught = st.rng.chance(RngStream::Market, p);
        ManipulationRecord rec;
        rec.actor = f.actor;
        rec.kind = f.kind;
        rec.tick = st.tick;
        rec.score = f.score;
        rec.detail = f.detail;
        if (caught) {
            rec.penalized = true;
            Empire* e = st.empire(f.actor);
            // 罚金按违规者国库比例计算：固定巨额罚金会让 AI 在几季内集体破产，
            // 进而使整个经济系统失去意义。监管罚金应与违规者体量相称。
            Fixed base = (e != nullptr) ? fxMax(e->treasury, Fixed(0)) : Fixed(0);
            Fixed rate = Fixed::pct(8) + f.score * Fixed::pct(12);   // 8% ~ 20%
            rec.fine = base * rate;
            if (rec.fine.rawValue() < Fixed(500).rawValue()) rec.fine = Fixed(500);
            if (rec.fine.rawValue() > Fixed(25000).rawValue()) rec.fine = Fixed(25000);
            if (e != nullptr) {
                e->treasury -= rec.fine;
                e->creditRating = fxClamp(e->creditRating - Fixed::pct(25), Fixed::pct(1), Fixed::pct(99));
                if (f.actor == kPlayerId) {
                    m.margin.cash -= rec.fine;
                    e->treasury = m.margin.cash;
                }
                for (auto& other : st.empires)
                    if (other.id != f.actor) other.addOpinion(f.actor, Fixed::pct(-15));
            }
            st.logEvent(LogPhase::Market, kLogManip,
                        "监管裁罚【" + std::string(manipKindName(f.kind)) + "】：罚没 " + fixedStr(rec.fine, 0) +
                            " cr，信用评级下降（" + f.detail + "）",
                        f.actor, rec.fine);
            ++enforced;
        } else {
            st.logEvent(LogPhase::Market, "market.manip.suspected",
                        "未被裁罚的疑似操纵【" + std::string(manipKindName(f.kind)) + "】：" + f.detail,
                        f.actor, f.score);
        }
        m.manipulations.push_back(std::move(rec));
    }
    if (st.market.manipulations.size() > 512) {
        st.market.manipulations.erase(st.market.manipulations.begin(),
                                      st.market.manipulations.begin() + 128);
    }
}

}  // namespace gf
