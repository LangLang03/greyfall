#include "mkt/MarketEngine.h"
#include "util/Fmt.h"
#include "clue/ClueGraph.h"

#include <algorithm>
#include <functional>
#include <set>

#include "core/TickPipeline.h"
#include "domain/ModifierUtil.h"
#include "rng/Streams.h"
#include "util/Str.h"

namespace gf {
namespace {

/// 来源多样性：不同 ProvChannel 的条数
int channelDiversity(const std::vector<u16>& nodes, const GameState& st) {
    std::set<int> chans;
    for (u16 n : nodes) {
        if (n >= st.clues.size()) continue;
        chans.insert(static_cast<int>(st.clues[n].prov.channel));
    }
    return static_cast<int>(chans.size());
}

bool hasInsiderSource(const std::vector<u16>& nodes, const GameState& st) {
    for (u16 n : nodes) {
        if (n >= st.clues.size()) continue;
        ProvChannel c = st.clues[n].prov.channel;
        if (c == ProvChannel::SpyNetwork || c == ProvChannel::Testimony || c == ProvChannel::Intercept)
            return true;
    }
    return false;
}

bool hasUnresolvedContradiction(const std::vector<u16>& nodes, const GameState& st) {
    for (const auto& e : st.clueEdges) {
        if (e.kind != ClueEdgeKind::Contradict || e.adjudicated) continue;
        bool a = std::find(nodes.begin(), nodes.end(), e.a) != nodes.end();
        bool b = std::find(nodes.begin(), nodes.end(), e.b) != nodes.end();
        if (a && b) return true;
    }
    return false;
}

}  // namespace

Fixed clueCredibility(const GameState& st, u16 id) {
    if (id >= st.clues.size()) return Fixed(0);
    const ClueNode& n = st.clues[id];
    Fixed base = n.prov.effectiveCredibility(st.tick);
    // 印证的邻居提升可信度，矛盾邻居降低
    Fixed support = Fixed(0);
    for (const auto& e : st.clueEdges) {
        if (e.a != id && e.b != id) continue;
        u16 other = (e.a == id) ? e.b : e.a;
        if (other >= st.clues.size() || !st.clues[other].known) continue;
        Fixed w = st.clues[other].credibility * e.weight;
        if (e.kind == ClueEdgeKind::Corroborate) support += w * Fixed::pct(15);
        else if (e.kind == ClueEdgeKind::Contradict && e.adjudicated && e.winner == id) support += w * Fixed::pct(20);
        else if (e.kind == ClueEdgeKind::Contradict) support -= w * Fixed::pct(10);
    }
    (void)base;
    return fxClamp(n.credibility + support, Fixed(0), Fixed(1));
}

void clueDiscover(GameState& st, u16 id, const Provenance& prov, TickReport* rep) {
    if (id >= st.clues.size()) return;
    ClueNode& n = st.clues[id];
    if (n.known) {
        // 重复获取：相关来源折扣，但仍提升可信度
        n.credibility = fxClamp(n.credibility + Fixed::pct(4), Fixed(0), Fixed(1));
        return;
    }
    n.known = true;
    n.prov = prov;
    n.credibility = fxClamp(prov.effectiveCredibility(st.tick), Fixed::pct(5), Fixed(1));
    n.discoveredTick = st.tick;
    st.plot.knownClues.push_back(id);
    if (rep != nullptr) rep->cluesDiscovered += 1;
    st.logEvent(LogPhase::Clue, kLogClue,
                "发现线索【" + std::string(clueDef(static_cast<int>(id)).nameZh) + "】来源：" +
                    std::string(provChannelName(prov.channel)) + "，可信度 " + fixedStrPlain(n.credibility, 2),
                kPlayerId, n.credibility);
}

bool clueLink(GameState& st, u16 a, u16 b, ClueEdgeKind kind, std::string* err) {
    if (a >= st.clues.size() || b >= st.clues.size()) {
        if (err) *err = "线索编号越界";
        return false;
    }
    if (!st.clues[a].known || !st.clues[b].known) {
        if (err) *err = "未发现的线索不能连接";
        return false;
    }
    if (a == b) {
        if (err) *err = "不能连接自身";
        return false;
    }
    for (auto& e : st.clueEdges) {
        if ((e.a == a && e.b == b) || (e.a == b && e.b == a)) {
            e.kind = kind;
            e.weight = Fixed::pct(60);
            return true;
        }
    }
    ClueEdge e;
    e.a = std::min(a, b);
    e.b = std::max(a, b);
    e.kind = kind;
    e.weight = Fixed::pct(60);
    e.createdTick = st.tick;
    e.playerMade = true;
    st.clueEdges.push_back(e);
    st.logEvent(LogPhase::Clue, kLogClue,
                "连接线索 " + std::string(clueDef(a).idName) + " ↔ " + std::string(clueDef(b).idName) + "（" +
                    std::string(clueEdgeKindName(kind)) + "）",
                kPlayerId);
    return true;
}

bool clueUnlink(GameState& st, u16 a, u16 b, std::string* err) {
    auto it = std::find_if(st.clueEdges.begin(), st.clueEdges.end(), [a, b](const ClueEdge& e) {
        return (e.a == a && e.b == b) || (e.a == b && e.b == a);
    });
    if (it == st.clueEdges.end()) {
        if (err) *err = "这两条线索之间没有边";
        return false;
    }
    st.clueEdges.erase(it);
    return true;
}

bool clueArchive(GameState& st, u16 id, std::string* err) {
    if (id >= st.clues.size() || !st.clues[id].known) {
        if (err) *err = "该线索尚未发现";
        return false;
    }
    st.clues[id].archived = true;
    st.logEvent(LogPhase::Clue, kLogClue,
                "归档线索 " + std::string(clueDef(static_cast<int>(id)).nameZh) + "（移出工作集，保留可信度贡献）",
                kPlayerId);
    return true;
}

void adjudicateContradictions(GameState& st) {
    for (auto& e : st.clueEdges) {
        if (e.kind != ClueEdgeKind::Contradict || e.adjudicated) continue;
        if (e.a >= st.clues.size() || e.b >= st.clues.size()) continue;
        if (!st.clues[e.a].known || !st.clues[e.b].known) continue;
        Fixed ca = clueCredibility(st, e.a);
        Fixed cb = clueCredibility(st, e.b);
        if (fxAbs(ca - cb).rawValue() < Fixed::pct(15).rawValue()) continue;   // 证据不足，暂不裁定
        e.adjudicated = true;
        e.winner = ca.rawValue() > cb.rawValue() ? e.a : e.b;
        u16 loser = e.winner == e.a ? e.b : e.a;
        st.clues[loser].credibility = st.clues[loser].credibility * Fixed::pct(60);
        st.logEvent(LogPhase::Clue, "clue.adjudicate",
                    "矛盾裁定：" + std::string(clueDef(st.clues[e.winner].def).nameZh) + " 胜出（可信度 " +
                        fixedStrPlain(fxMax(ca, cb), 2) + " vs " + fixedStrPlain(fxMin(ca, cb), 2) + "）",
                    kPlayerId);
    }
}

void cluePhase(GameState& st, TickReport& rep) {
    const bool memoryHole = hasModifier(st.modifierBits, kModMemoryHole);
    // 1) 衰减
    for (auto& n : st.clues) {
        if (!n.known) continue;
        Fixed decay = memoryHole ? Fixed::raw(20) : Fixed::raw(10);
        n.credibility = fxClamp(n.credibility - decay, Fixed(0), Fixed(1));
        if (n.credibility.rawValue() < Fixed::pct(5).rawValue()) {
            // 可信度过低 ⇒ 变成"可疑"，仍保留但降权
            n.credibility = Fixed::pct(5);
        }
    }
    // 2) 传播：通过超边把可信度扩散到相邻节点
    std::vector<std::pair<u16, Fixed>> gains;
    for (const auto& e : st.clueEdges) {
        if (e.kind != ClueEdgeKind::Corroborate) continue;
        if (e.a >= st.clues.size() || e.b >= st.clues.size()) continue;
        if (st.clues[e.a].known && !st.clues[e.b].known) {
            Fixed strength = clueCredibility(st, e.a) * e.weight;
            if (strength.rawValue() > Fixed::pct(50).rawValue()) gains.emplace_back(e.b, strength);
        } else if (st.clues[e.b].known && !st.clues[e.a].known) {
            Fixed strength = clueCredibility(st, e.b) * e.weight;
            if (strength.rawValue() > Fixed::pct(50).rawValue()) gains.emplace_back(e.a, strength);
        }
    }
    for (const auto& g : gains) {
        Provenance p;
        p.channel = ProvChannel::Analysis;
        p.credibility = g.second * Fixed::pct(60);
        p.tick = st.tick;
        p.source = kPlayerId;
        clueDiscover(st, g.first, p, &rep);
    }
    // 3) 异常点解析：己方星系上的异常点有概率产出线索
    for (const auto& sys : st.map.systems) {
        if (sys.owner != kPlayerId || sys.anomaly == 0) continue;
        // 档案穹顶（ClueDiscovery）提升解析概率。
        // 早期这个效果只写进 AI 的选型打分、对线索毫无影响 ——
        // 玩家花 26,510 cr 建的档案穹顶是个纯摆设。
        Fixed base = Fixed::pct(12);
        if (const Empire* e = st.empire(kPlayerId); e != nullptr)
            base = base * (Fixed(1) + e->clueBonus);
        if (!st.rng.chance(RngStream::Clue, fxClamp(base, Fixed::pct(1), Fixed::pct(90)))) continue;
        const AnomalyInfo& ai = anomalyInfo(static_cast<int>(sys.anomaly));
        i16 reward = ai.rewardClue;
        if (reward < 0 || reward >= kClueCount) continue;
        Provenance p;
        p.channel = ProvChannel::DirectObservation;
        p.credibility = Fixed::pct(75);
        p.tick = st.tick;
        p.signalCost = ai.difficulty;
        clueDiscover(st, static_cast<u16>(reward), p, &rep);
    }
    // 4) 矛盾裁定
    adjudicateContradictions(st);
    // 5) 归档线索不再计入已知工作集
    st.plot.knownClues.erase(
        std::remove_if(st.plot.knownClues.begin(), st.plot.knownClues.end(),
                       [&st](u16 id) { return id < st.clues.size() && st.clues[id].archived; }),
        st.plot.knownClues.end());
}

std::vector<MinimalSet> minimalSatisfyingSets(const GameState& st, u16 conclusion) {
    std::vector<MinimalSet> out;
    if (conclusion >= kConclusionCount) return out;
    const ConclusionDef& def = conclusionDef(static_cast<int>(conclusion));

    // 合取范式：每个子句必须至少选一个节点；在所有子句的笛卡尔积上做剪枝 DFS
    std::vector<std::vector<u16>> clauses;
    for (const auto& cl : def.clauses) {
        std::vector<u16> usable;
        for (u16 n : cl) {
            if (n >= st.clues.size()) continue;
            if (!st.clues[n].known) continue;
            // 可信度过低的节点不作为充分集的成员
            if (clueCredibility(st, n).rawValue() < Fixed::pct(30).rawValue()) continue;
            usable.push_back(n);
        }
        if (usable.empty()) return out;   // 该子句不可满足
        clauses.push_back(std::move(usable));
    }
    if (clauses.size() > 18) return out;   // 节点数上限剪枝

    // DFS 枚举起见：对每个子句选一个节点，去重后形成候选集
    std::vector<u16> current;
    std::set<std::vector<u16>> seen;
    std::size_t limit = 4096;   // 枚举上限
    std::function<void(std::size_t)> dfs = [&](std::size_t idx) {
        if (out.size() >= kMinimalSetCount || seen.size() >= limit) return;
        if (idx == clauses.size()) {
            std::vector<u16> sorted = current;
            std::sort(sorted.begin(), sorted.end());
            sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
            if (sorted.empty()) return;
            if (!seen.insert(sorted).second) return;
            MinimalSet ms;
            ms.nodes = sorted;
            ms.diverse = channelDiversity(sorted, st) >= 3;
            ms.noUnresolved = !hasUnresolvedContradiction(sorted, st);
            ms.hasInsider = hasInsiderSource(sorted, st);
            ms.satisfiesAll = true;
            out.push_back(std::move(ms));
            return;
        }
        for (u16 n : clauses[idx]) {
            current.push_back(n);
            dfs(idx + 1);
            current.pop_back();
        }
    };
    dfs(0);

    // 支配集剪枝：去掉被其他集合包含的候选
    std::vector<MinimalSet> pruned;
    for (std::size_t i = 0; i < out.size(); ++i) {
        bool dominated = false;
        for (std::size_t j = 0; j < out.size(); ++j) {
            if (i == j) continue;
            if (out[j].nodes.size() >= out[i].nodes.size()) continue;
            if (std::includes(out[i].nodes.begin(), out[i].nodes.end(), out[j].nodes.begin(), out[j].nodes.end())) {
                dominated = true;
                break;
            }
        }
        if (!dominated) pruned.push_back(out[i]);
    }
    std::sort(pruned.begin(), pruned.end(), [](const MinimalSet& a, const MinimalSet& b) {
        if (a.diverse != b.diverse) return a.diverse;
        if (a.hasInsider != b.hasInsider) return a.hasInsider;
        return a.nodes.size() < b.nodes.size();
    });
    if (pruned.size() > kMinimalSetCount) pruned.resize(kMinimalSetCount);
    return pruned;
}

bool conclusionUnlockable(const GameState& st, u16 conclusion, std::string* why) {
    auto sets = minimalSatisfyingSets(st, conclusion);
    if (sets.empty()) {
        if (why) *why = "不存在满足全部子句的已发现线索组合";
        return false;
    }
    const ConclusionDef& def = conclusionDef(static_cast<int>(conclusion));
    for (const auto& ms : sets) {
        if (def.requireDiversity && !ms.diverse) continue;
        if (!ms.noUnresolved) continue;
        if (def.requireInsider && !ms.hasInsider) continue;
        return true;
    }
    if (why) {
        *why = "存在候选集但未通过附加约束（需来源多样性 ≥3";
        if (def.requireInsider) *why += "、至少 1 条来自对手内部";
        *why += "、无未裁定矛盾）";
    }
    return false;
}

std::string deduceReport(const GameState& st, u16 conclusion) {
    if (conclusion >= kConclusionCount) return "非法结论编号";
    const ConclusionDef& def = conclusionDef(static_cast<int>(conclusion));
    std::string out;
    out += "═══ 推断报告 " + std::string(def.idName) + " ═══\n";
    out += "结论：" + std::string(def.nameZh) + "（第 " + std::to_string(static_cast<int>(def.act)) + " 幕）\n";
    out += wrapJoin(def.text, 86, "  ") + "\n\n";
    out += "合取范式（每个子句需至少 1 条成立）：\n";
    for (std::size_t i = 0; i < def.clauses.size(); ++i) {
        out += "  子句 " + std::to_string(i + 1) + "：";
        for (u16 n : def.clauses[i]) {
            bool known = n < st.clues.size() && st.clues[n].known;
            out += std::string(known ? "[✓]" : "[ ]") + std::string(clueDef(static_cast<int>(n)).nameZh) + "  ";
        }
        out += "\n";
    }
    out += "\n";
    auto sets = minimalSatisfyingSets(st, conclusion);
    out += "最小充分集：" + std::to_string(sets.size()) + " 个\n";
    int shown = 0;
    for (const auto& ms : sets) {
        if (shown >= 5) break;
        out += "  · 集合 " + std::to_string(shown + 1) + "（" + std::to_string(ms.nodes.size()) + " 条）：";
        for (u16 n : ms.nodes) out += std::string(clueDef(static_cast<int>(n)).idName) + " ";
        out += "\n      来源多样性 " + std::string(ms.diverse ? "✓" : "✗") + "  无未裁定矛盾 " +
               std::string(ms.noUnresolved ? "✓" : "✗") + "  对手内部来源 " +
               std::string(ms.hasInsider ? "✓" : "✗") + "\n";
        ++shown;
    }
    std::string why;
    bool ok = conclusionUnlockable(st, conclusion, &why);
    out += "\n可提交性：" + std::string(ok ? "满足全部约束 ✓" : ("不满足 ✗ —— " + why)) + "\n";
    if (!def.trueConclusion) {
        out += style("⚠ 注意：此结论可能是陷阱（提交错误结论会导致假剧情分支 + 外交信誉损失 + 市场恐慌）",
                     Style::Warn) +
               "\n";
    }
    return out;
}

bool commitConclusion(GameState& st, u16 conclusion, std::string* err) {
    if (std::find(st.plot.committedConclusions.begin(), st.plot.committedConclusions.end(), conclusion) !=
        st.plot.committedConclusions.end()) {
        if (err) *err = "该结论已经提交";
        return false;
    }
    std::string why;
    if (!conclusionUnlockable(st, conclusion, &why)) {
        if (err) *err = "无法提交：" + why;
        return false;
    }
    const ConclusionDef& def = conclusionDef(static_cast<int>(conclusion));
    st.plot.committedConclusions.push_back(conclusion);
    if (std::find(st.plot.conclusionsReached.begin(), st.plot.conclusionsReached.end(), conclusion) ==
        st.plot.conclusionsReached.end()) {
        st.plot.conclusionsReached.push_back(conclusion);
    }
    st.logEvent(LogPhase::Plot, kLogDeduce,
                "提交结论【" + std::string(def.nameZh) + "】" + (def.trueConclusion ? "（真相）" : "（误判）"), kPlayerId);

    if (def.trueConclusion) {
        // 真相公开：重估 AI 对你意图的先验 + 价格跳跃
        for (auto& e : st.empires) {
            if (e.isPlayer) continue;
            e.mind.playerModel.modelConfidence += Fixed::pct(5);
            e.mind.playerModel.wGoal[static_cast<std::size_t>(GoalDim::Knowledge)] += Fixed::pct(30);
            e.addOpinion(kPlayerId, Fixed::pct(3));
        }
        marketApplyShock(st, static_cast<u8>(Commodity::DataCrystals), def.marketImpact, "真相公开：数据晶重估");
        marketApplyShock(st, static_cast<u8>(Commodity::Relics), def.marketImpact * Fixed::pct(70), "真相公开：遗物重估");
        // 结局向量：知识维度
        st.plot.endingVector[4] += Fixed::pct(20) * def.marketImpact;
    } else {
        // 误判代价
        st.plot.falseConclusions.push_back(conclusion);
        for (auto& e : st.empires) {
            if (e.isPlayer) continue;
            e.addOpinion(kPlayerId, Fixed::pct(-8));
        }
        marketApplyShock(st, static_cast<u8>(Commodity::Credits), -Fixed::pct(3), "错误结论引发市场恐慌");
        st.empires[kPlayerId].influence -= st.empires[kPlayerId].influence * Fixed::pct(10);
        st.logEvent(LogPhase::Plot, kLogDeduce, "误判生效：假剧情分支开启，外交信誉 -8，市场恐慌", kPlayerId);
    }
    return true;
}

std::string clueListText(const GameState& st, bool onlyUnlinked, const std::string& tagFilter,
                         const std::string& subjectFilter) {
    std::string out;
    int shown = 0;
    for (std::size_t i = 0; i < st.clues.size(); ++i) {
        const ClueNode& n = st.clues[i];
        if (!n.known) continue;
        const ClueDef& d = clueDef(static_cast<int>(n.def));
        if (!tagFilter.empty()) {
            bool match = false;
            for (u8 k = 0; k < d.tagCount; ++k) {
                if (clueTagName(d.tags[k]) == tagFilter) match = true;
            }
            if (!match) continue;
        }
        bool linked = false;
        for (const auto& e : st.clueEdges)
            if (e.a == n.def || e.b == n.def) linked = true;
        if (onlyUnlinked && linked) continue;
        if (!subjectFilter.empty()) {
            const Empire* subj = st.empire(static_cast<u32>(parseInt(subjectFilter, 0)));
            if (subj != nullptr) {
                bool about = false;
                for (const auto& e : st.clueEdges)
                    if ((e.a == n.def || e.b == n.def) && e.kind == ClueEdgeKind::About) about = true;
                if (!about) continue;
            }
        }
        out += "  " + padRight(std::string(d.idName), 9) + padRight(std::string(d.nameZh), 24) +
               "可信 " + padLeft(fixedStrPlain(clueCredibility(st, n.def), 2), 5) + "  " +
               padRight(std::string(provChannelName(n.prov.channel)), 12) + "  幕 " +
               std::to_string(static_cast<int>(d.act)) + (linked ? "  [已连接]" : "  [孤立]") +
               (n.archived ? "  [已归档]" : "") + "\n";
        ++shown;
    }
    if (shown == 0) out = "  （没有符合条件的线索）\n";
    return out;
}

}  // namespace gf
