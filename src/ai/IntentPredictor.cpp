#include "util/Fmt.h"
#include "ai/IntentPredictor.h"

#include <algorithm>
#include <string>

#include "domain/Empire.h"
#include "items/ItemDef.h"
#include "util/Str.h"

namespace gf {
namespace {

/// 单条策略路径：一组"未来 K 期会做的事"
struct Path {
    std::string name;
    Fixed weight = Fixed(0);
    std::array<Fixed, kCommodityCount> demand{};
    /// 该路径是否属于"可被 decoy 放大的错误路径"
    bool decoyProne = false;
};

}  // namespace

IntentPrediction predictIntent(const GameState& st, u32 subject, const ComputeBudget& budget) {
    IntentPrediction p;
    p.horizon = std::max(1, static_cast<int>(budget.foresight) + 1);
    Observable obs = omniscientSnapshot(st, subject);
    const Empire* e = st.empire(subject);
    if (e == nullptr) return p;

    // 基础需求：产能缺口 + 在建工程 + 巨构阶段
    std::array<Fixed, kCommodityCount> base{};
    for (auto& v : base) v = Fixed(0);
    for (int c = 0; c < kCommodityCount; ++c) {
        Fixed deficit = e->demand[static_cast<std::size_t>(c)] - e->capacity[static_cast<std::size_t>(c)];
        if (deficit.rawValue() > 0) base[static_cast<std::size_t>(c)] += deficit * Fixed(p.horizon);
    }
    for (int c = 0; c < kCommodityCount; ++c) base[static_cast<std::size_t>(c)] += obs.futureDemand[static_cast<std::size_t>(c)];

    std::vector<Path> paths;
    Path build;
    build.name = "继续既定工程（巨构/建筑按原计划推进）";
    build.weight = Fixed::pct(45);
    build.demand = base;
    paths.push_back(build);

    Path military;
    military.name = "转向军备（边境紧张时会优先补齐舰队）";
    military.weight = Fixed::pct(25);
    military.decoyProne = true;
    military.demand = base;
    military.demand[static_cast<std::size_t>(Commodity::Alloys)] += Fixed(3000);
    military.demand[static_cast<std::size_t>(Commodity::Components)] += Fixed(1800);
    military.demand[static_cast<std::size_t>(Commodity::Antimatter)] += Fixed(200);
    paths.push_back(military);

    Path trade;
    trade.name = "套利主导（若跨所价差继续扩大）";
    trade.weight = Fixed::pct(20);
    trade.demand = base;
    trade.demand[static_cast<std::size_t>(Commodity::Energy)] += Fixed(2000);
    trade.demand[static_cast<std::size_t>(Commodity::Minerals)] += Fixed(1600);
    paths.push_back(trade);

    Path hoard;
    hoard.name = "防御性囤积（预期封锁）";
    hoard.weight = Fixed::pct(10);
    hoard.decoyProne = true;
    hoard.demand = base;
    hoard.demand[static_cast<std::size_t>(Commodity::Medicines)] += Fixed(1500);
    hoard.demand[static_cast<std::size_t>(Commodity::Food)] += Fixed(2500);
    paths.push_back(hoard);

    // 反情报降低每条路径的置信度；伪造数据（decoy）抬升错误路径的权重
    Fixed counter = subjectCounterIntel(st, subject);
    Fixed deception = Fixed(0);
    for (const auto& it : st.inventory.items) {
        if (subject != kPlayerId) break;
        const ItemDef& d = itemDef(it.def);
        if (d.effect == ItemEffect::BeliefNoise || (d.effect == ItemEffect::MarketFakeStock && it.forged)) {
            deception += d.effectValue;
        }
    }
    deception = fxClamp(deception, Fixed(0), Fixed::pct(80));
    p.deceptionLevel = deception;

    // 路径权重再分配
    Fixed totalW = Fixed(0);
    for (auto& path : paths) {
        // 欺骗会把权重推向"囤积/军备"这类错误路径
        if (deception.rawValue() > 0 && path.decoyProne) {
            path.weight += deception * Fixed::pct(40);
        }
        totalW += path.weight;
    }
    if (totalW.rawValue() <= 0) totalW = Fixed(1);

    // 加权合成
    for (int c = 0; c < kCommodityCount; ++c) {
        Fixed acc = Fixed(0);
        for (const auto& path : paths) acc += path.demand[static_cast<std::size_t>(c)] * path.weight;
        p.netDemand[static_cast<std::size_t>(c)] = acc / totalW;
    }

    // top-M 剪枝
    std::sort(paths.begin(), paths.end(),
              [](const Path& a, const Path& b) { return a.weight.rawValue() > b.weight.rawValue(); });
    int keep = std::max(1, budget.topM);
    if (static_cast<int>(paths.size()) > keep) paths.resize(static_cast<std::size_t>(keep));
    if (!paths.empty()) {
        p.topPath = paths.front().name;
        for (std::size_t i = 1; i < paths.size(); ++i) p.alternatives.push_back(paths[i].name);
    }

    // 置信度
    Fixed conf = Fixed::pct(45) + e->mind.playerModel.modelConfidence * Fixed::pct(40);
    conf -= counter * Fixed::pct(50);
    conf -= deception * Fixed::pct(35);
    if (obs.rollbackCount > 0) conf += Fixed::pct(8) * Fixed(static_cast<i64>(obs.rollbackCount));  // 读档者更好预测
    p.confidence = fxClamp(conf, Fixed::pct(5), Fixed::pct(95));

    // 紧迫度
    for (int c = 0; c < kCommodityCount; ++c) {
        Fixed have = obs.resources[static_cast<std::size_t>(c)];
        Fixed need = p.netDemand[static_cast<std::size_t>(c)];
        if (need.rawValue() <= 0) {
            p.urgency[static_cast<std::size_t>(c)] = Fixed(0);
            continue;
        }
        Fixed ratio = have.rawValue() > 0 ? need / have : Fixed(2);
        p.urgency[static_cast<std::size_t>(c)] = fxClamp(ratio, Fixed(0), Fixed(1));
    }
    return p;
}

void intentLog(GameState& st, u32 observer, const IntentPrediction& p) {
    // 找出最紧迫的 3 个标的
    std::vector<std::pair<Fixed, int>> ranked;
    for (int c = 0; c < kCommodityCount; ++c)
        ranked.emplace_back(p.urgency[static_cast<std::size_t>(c)], c);
    std::sort(ranked.begin(), ranked.end(),
              [](const auto& a, const auto& b) { return a.first.rawValue() > b.first.rawValue(); });
    std::string detail = "预测玩家未来 " + std::to_string(p.horizon) + " 季净需求：";
    int shown = 0;
    for (const auto& kv : ranked) {
        if (kv.first.rawValue() <= 0 || shown >= 3) break;
        detail += std::string(commodityName(kv.second)) + " +" +
                  groupDigits(p.netDemand[static_cast<std::size_t>(kv.second)].rawValue() / FIX) + " ";
        ++shown;
    }
    if (shown == 0) detail += "无显著需求";
    detail += "（置信 " + fixedStrPlain(p.confidence, 2) + "）";
    st.logEvent(LogPhase::Ai, kLogIntent, detail, observer, p.confidence);
}

std::string intentSummary(const IntentPrediction& p, int limit) {
    std::string out;
    out += "预测窗口：未来 " + std::to_string(p.horizon) + " 季\n";
    out += "置信度：" + fixedStrPlain(p.confidence, 2) + "\n";
    out += "最可能路径：" + p.topPath + "\n";
    for (std::size_t i = 0; i < p.alternatives.size(); ++i) {
        out += "备选路径 " + std::to_string(i + 1) + "：" + p.alternatives[i] + "\n";
    }
    std::vector<std::pair<Fixed, int>> ranked;
    for (int c = 0; c < kCommodityCount; ++c) {
        if (p.netDemand[static_cast<std::size_t>(c)].rawValue() <= 0) continue;
        ranked.emplace_back(p.netDemand[static_cast<std::size_t>(c)], c);
    }
    std::sort(ranked.begin(), ranked.end(),
              [](const auto& a, const auto& b) { return a.first.rawValue() > b.first.rawValue(); });
    if (static_cast<int>(ranked.size()) > limit) ranked.resize(static_cast<std::size_t>(limit));
    out += "净需求向量：\n";
    for (const auto& kv : ranked) {
        out += "  " + padRight(std::string(commodityName(kv.second)), 12) + " +" +
               padLeft(groupDigits(kv.first.rawValue() / FIX), 10) + "   紧迫度 " +
               bar(p.urgency[static_cast<std::size_t>(kv.second)], 10) + "\n";
    }
    return out;
}

}  // namespace gf
