#include "ai/FraudDetect.h"

#include <algorithm>

#include "items/ItemDef.h"
#include "util/Str.h"

namespace gf {
Fixed signalCostEstimate(const GameState& st, u32 subject, ProvChannel ch) {
    Fixed base = channelBaseCredibility(ch);
    const Empire* e = st.empire(subject);
    if (e != nullptr) {
        // 影响力高者能更便宜地布置可信数据
        base = base + e->influence / Fixed(2000);
    }
    return fxClamp(base, Fixed(0), Fixed(1));
}

std::vector<FraudFinding> fraudDetect(const GameState& st, u32 observer, u32 subject, const Observable& obs,
                                      const Observable& prev) {
    std::vector<FraudFinding> out;
    (void)observer;

    // ---- 1) 库存跳变：没有对应的市场成交，却有大量库存变化 ----
    for (int c = 0; c < kCommodityCount; ++c) {
        Fixed now = obs.resources[static_cast<std::size_t>(c)];
        Fixed before = prev.resources[static_cast<std::size_t>(c)];
        Fixed change = now - before;
        if (change.rawValue() == 0) continue;
        // 该 tick 的真实成交规模
        i64 traded = 0;
        for (const auto& ex : st.market.exchanges) {
            for (const auto& ord : ex.books[static_cast<std::size_t>(c)].orders) {
                if (ord.owner != subject) continue;
                traded += ord.qty - ord.filled;
            }
        }
        Fixed tradedF = Fixed(traded);
        Fixed unexplained = fxAbs(change) - tradedF;
        Fixed ratio = fxAbs(change).rawValue() > 0
                          ? Fixed::raw(mulDivSat(unexplained.rawValue(), FIX, fxAbs(change).rawValue()))
                          : Fixed(0);
        if (ratio.rawValue() > Fixed::pct(60).rawValue() && fxAbs(change).rawValue() > Fixed::raw(500).rawValue()) {
            FraudFinding f;
            f.field = "resources." + std::string(commodityInfo(c).idName);
            f.score = ratio;
            f.channel = ProvChannel::Forgery;
            f.estimatedSignalCost = signalCostEstimate(st, subject, ProvChannel::Forgery);
            f.reason = "库存变化 " + fixedStrSigned(change, 0) + " 与订单流痕迹不匹配（无法解释比例 " +
                       fixedStrPlain(ratio * Fixed(100), 0) + "%）——他在布置数据";
            out.push_back(std::move(f));
        }
    }

    // ---- 2) 工程进度与需求向量矛盾 ----
    Fixed futureBefore = Fixed(0);
    for (const auto& v : prev.futureDemand) futureBefore += v;
    Fixed futureNow = Fixed(0);
    for (const auto& v : obs.futureDemand) futureNow += v;
    // 声称的工程需求突然消失，但库存并未增加 → 假停工
    if (futureBefore.rawValue() > 0 && futureNow.rawValue() < futureBefore.rawValue() / 2) {
        FraudFinding f;
        f.field = "futureDemand";
        f.score = Fixed::pct(70);
        f.channel = ProvChannel::Gift;
        f.estimatedSignalCost = signalCostEstimate(st, subject, ProvChannel::Gift);
        f.reason = "在建工程需求骤降 " + fixedStrPlain((Fixed(1) - futureNow / futureBefore) * Fixed(100), 0) +
                   "% 而无对应资源回流：疑似 decoy（假停工信号）";
        out.push_back(std::move(f));
    }

    // ---- 3) 事件/道具层面的伪造痕迹 ----
    for (const auto& it : obs.inventory) {
        if (!it.forged) continue;
        FraudFinding f;
        f.field = "inventory." + std::string(itemDef(static_cast<int>(it.def)).idName);
        f.score = fxClamp(it.contamination + Fixed::pct(40), Fixed(0), Fixed(1));
        f.channel = ProvChannel::Forgery;
        f.estimatedSignalCost = signalCostEstimate(st, subject, ProvChannel::Forgery);
        f.reason = "持有伪造凭证【" + std::string(itemDef(static_cast<int>(it.def)).nameZh) +
                   "】，污染强度 " + fixedStrPlain(it.contamination, 2);
        out.push_back(std::move(f));
    }

    // ---- 4) 线索可信度异常：来源单一却置信极高 ----
    for (const auto& c : obs.clues) {
        if (c.credibility.rawValue() <= Fixed::pct(85).rawValue()) continue;
        if (c.channel != ProvChannel::Forgery && c.channel != ProvChannel::Rumor) continue;
        FraudFinding f;
        f.field = "clues." + std::string(clueDef(static_cast<int>(c.def)).idName);
        f.score = c.credibility;
        f.channel = c.channel;
        f.estimatedSignalCost = signalCostEstimate(st, subject, c.channel);
        f.reason = "低可靠渠道（" + std::string(provChannelName(c.channel)) + "）却给出高可信线索：摆拍痕迹";
        out.push_back(std::move(f));
    }

    // ---- 5) 读档次数作为成本信号 ----
    if (obs.rollbackCount > 0) {
        FraudFinding f;
        f.field = "rollbackCount";
        f.score = fxClamp(Fixed(static_cast<i64>(obs.rollbackCount)) * Fixed::pct(30), Fixed(0), Fixed(1));
        f.channel = ProvChannel::Archive;
        f.estimatedSignalCost = Fixed(0);
        f.reason = "读档 " + std::to_string(obs.rollbackCount) + " 次：这不是谎言，但是最强的成本信号";
        out.push_back(std::move(f));
    }

    std::sort(out.begin(), out.end(),
              [](const FraudFinding& a, const FraudFinding& b) { return a.score.rawValue() > b.score.rawValue(); });
    return out;
}

Fixed fraudOverallScore(const std::vector<FraudFinding>& findings) {
    Fixed acc = Fixed(0);
    for (const auto& f : findings) acc += f.score;
    if (findings.empty()) return Fixed(0);
    return fxClamp(acc / Fixed(static_cast<i64>(findings.size())) + Fixed::pct(5) * Fixed(static_cast<i64>(findings.size())),
                   Fixed(0), Fixed(1));
}

void fraudLog(GameState& st, u32 observer, const std::vector<FraudFinding>& findings) {
    if (findings.empty()) return;
    // 聚合为**一条**日志：逐条记录会让日志被 AI 的一致性检查刷爆
    //（实测 5 季内 900 条日志中有 738 条是这一项，把事件/战斗/外交全部挤掉）。
    // 只列出最严重的若干条，并标明总数。
    std::vector<const FraudFinding*> sorted;
    sorted.reserve(findings.size());
    for (const auto& f : findings) sorted.push_back(&f);
    std::sort(sorted.begin(), sorted.end(),
              [](const FraudFinding* a, const FraudFinding* b) { return a->score.rawValue() > b->score.rawValue(); });
    Fixed overall = fraudOverallScore(findings);
    std::string top;
    for (std::size_t i = 0; i < sorted.size() && i < 3; ++i) {
        if (i > 0) top += "；";
        top += sorted[i]->field + "(" + fixedStrPlain(sorted[i]->score, 2) + ")";
    }
    if (sorted.size() > 3) top += " 等";
    st.logEvent(LogPhase::Ai, kLogFraud,
                "一致性异常 " + std::to_string(findings.size()) + " 项（综合评分 " +
                    fixedStrPlain(overall, 2) + "）：" + top,
                observer, overall);
}

}  // namespace gf
