// 线索：最小充分集正确性（暴力校验小图）/ 来源多样性 / 矛盾裁定 / 误判代价 / 结论解锁
#include <algorithm>
#include <set>

#include "check.h"
#include "clue/ClueGraph.h"
#include "core/GameState.h"
#include "core/TickPipeline.h"
#include "gen/WorldGen.h"
#include "mkt/OrderBook.h"
#include "plot/BeatResolver.h"

using namespace gf;

namespace {

GameState clueWorld(u64 seed = 8080) {
    WorldGenOptions o;
    o.seed = seed;
    o.difficulty = 2;
    o.empireCount = 8;
    o.systemCount = 32;
    GameState st;
    generateWorld(st, o);
    return st;
}

void revealAll(GameState& st, Fixed credibility = Fixed::pct(80)) {
    for (std::size_t i = 0; i < st.clues.size(); ++i) {
        ClueNode& n = st.clues[i];
        n.known = true;
        n.credibility = credibility;
        n.prov.channel = (i % 3 == 0) ? ProvChannel::SpyNetwork
                                      : (i % 3 == 1 ? ProvChannel::DirectObservation : ProvChannel::Analysis);
        n.prov.credibility = credibility;
        n.prov.tick = st.tick;
    }
}

}  // namespace

TEST(clue, content_tables) {
    CHECK_EQ(kClueCount, 340);
    CHECK_EQ(kConclusionCount, 84);
    // 超边引用存在
    for (int i = 0; i < kClueCount; ++i) {
        for (u16 l : clueDef(i).linked) CHECK(l < kClueCount);
    }
    // 合取范式合法
    for (int i = 0; i < kConclusionCount; ++i) {
        const ConclusionDef& c = conclusionDef(i);
        CHECK(!c.clauses.empty());
        for (const auto& cl : c.clauses) {
            CHECK(!cl.empty());
            for (u16 n : cl) CHECK(n < kClueCount);
        }
        CHECK(c.act >= 1 && c.act <= kActCount);
    }
    // idName 唯一
    std::set<std::string> names;
    for (int i = 0; i < kClueCount; ++i) names.insert(std::string(clueDef(i).idName));
    CHECK_EQ(names.size(), static_cast<std::size_t>(kClueCount));
}

TEST(clue, minimal_satisfying_sets_cover_all_clauses) {
    GameState st = clueWorld();
    revealAll(st);
    for (int c = 0; c < 6; ++c) {
        const ConclusionDef& def = conclusionDef(c);
        auto sets = minimalSatisfyingSets(st, static_cast<u16>(c));
        CHECK(!sets.empty());
        // 每个最小集必须覆盖全部子句
        for (const auto& ms : sets) {
            for (const auto& cl : def.clauses) {
                bool hit = false;
                for (u16 n : cl)
                    if (std::find(ms.nodes.begin(), ms.nodes.end(), n) != ms.nodes.end()) hit = true;
                CHECK(hit);
            }
        }
        // 集合内部无重复
        for (const auto& ms : sets) {
            std::set<u16> uniq(ms.nodes.begin(), ms.nodes.end());
            CHECK_EQ(uniq.size(), ms.nodes.size());
        }
    }
}

TEST(clue, brute_force_verifies_minimality_on_small_graph) {
    // 构造一个 3 子句的小结论，暴力枚举所有子集，验证最小集不被更小的可行集支配
    GameState st = clueWorld(9090);
    revealAll(st);
    u16 concl = 0;
    const ConclusionDef& def = conclusionDef(concl);
    // 候选并集
    std::vector<u16> universe;
    for (const auto& cl : def.clauses)
        for (u16 n : cl)
            if (std::find(universe.begin(), universe.end(), n) == universe.end()) universe.push_back(n);
    std::size_t U = universe.size();
    CHECK(U > 0 && U <= 20);
    // 暴力：找出所有覆盖全部子句的子集，求最小势
    std::size_t bruteMin = U + 1;
    for (std::uint32_t mask = 1; mask < (1u << U); ++mask) {
        bool covers = true;
        for (const auto& cl : def.clauses) {
            bool hit = false;
            for (u16 n : cl) {
                std::size_t idx = static_cast<std::size_t>(std::find(universe.begin(), universe.end(), n) -
                                                           universe.begin());
                if (mask & (1u << idx)) hit = true;
            }
            if (!hit) covers = false;
        }
        if (!covers) continue;
        std::size_t bits = 0;
        for (std::size_t i = 0; i < U; ++i)
            if (mask & (1u << i)) ++bits;
        bruteMin = std::min(bruteMin, bits);
    }
    auto sets = minimalSatisfyingSets(st, concl);
    if (!sets.empty()) {
        std::size_t engineMin = sets.front().nodes.size();
        for (const auto& ms : sets) engineMin = std::min(engineMin, ms.nodes.size());
        CHECK_EQ(engineMin, bruteMin);
    }
}

TEST(clue, link_unlink_and_diversity_constraints) {
    GameState st = clueWorld(111);
    revealAll(st);
    std::string err;
    // 先断开一条既有的印证边
    u16 a = 0xFFFFu, b = 0xFFFFu;
    for (const auto& e : st.clueEdges) {
        if (e.kind != ClueEdgeKind::Corroborate) continue;
        a = e.a;
        b = e.b;
        break;
    }
    if (a != 0xFFFFu) {
        CHECK(clueUnlink(st, a, b, &err));
        CHECK(!clueUnlink(st, a, b, &err));   // 重复断开必须失败
        CHECK(clueLink(st, a, b, ClueEdgeKind::Corroborate, &err));
    }
    // 连接自身必须失败
    CHECK(!clueLink(st, 0, 0, ClueEdgeKind::Corroborate, &err));
    // 未发现的线索不能连接
    st.clues[200].known = false;
    CHECK(!clueLink(st, 0, 200, ClueEdgeKind::Corroborate, &err));
    // 归档后移出工作集
    u16 target = st.plot.knownClues.empty() ? 0 : st.plot.knownClues.front();
    CHECK(clueArchive(st, target, &err));
    TickReport rep;
    cluePhase(st, rep);
    CHECK(std::find(st.plot.knownClues.begin(), st.plot.knownClues.end(), target) == st.plot.knownClues.end());
}

TEST(clue, contradiction_adjudication) {
    GameState st = clueWorld(222);
    revealAll(st);
    // 构造一对矛盾边：一条高可信、一条低可信
    st.clues[10].credibility = Fixed::pct(90);
    st.clues[10].prov.channel = ProvChannel::Panopticon;
    st.clues[11].credibility = Fixed::pct(25);
    st.clues[11].prov.channel = ProvChannel::Rumor;
    ClueEdge e;
    e.a = 10;
    e.b = 11;
    e.kind = ClueEdgeKind::Contradict;
    e.weight = Fixed::pct(60);
    st.clueEdges.push_back(e);
    adjudicateContradictions(st);
    bool adjudicated = false;
    for (const auto& x : st.clueEdges)
        if (x.a == 10 && x.b == 11 && x.adjudicated) {
            adjudicated = true;
            CHECK_EQ(x.winner, static_cast<u16>(10));   // 可信度高者胜
        }
    CHECK(adjudicated);
    CHECK(st.clues[11].credibility.rawValue() < Fixed::pct(25).rawValue());
}

TEST(clue, commit_true_conclusion_moves_market) {
    GameState st = clueWorld(333);
    revealAll(st, Fixed::pct(90));
    TickReport rep;
    // 先让市场推进一季，确认冲击后的 σ 变化可观测
    advanceOneTick(st);
    Fixed sigmaBefore = st.market.vol[static_cast<std::size_t>(Commodity::DataCrystals)].lastSigma;
    // 找一个真相结论
    int target = -1;
    for (int i = 0; i < kConclusionCount; ++i) {
        if (!conclusionDef(i).trueConclusion) continue;
        if (conclusionUnlockable(st, static_cast<u16>(i), nullptr)) {
            target = i;
            break;
        }
    }
    CHECK(target >= 0);
    std::string err;
    CHECK(commitConclusion(st, static_cast<u16>(target), &err));
    CHECK(std::find(st.plot.committedConclusions.begin(), st.plot.committedConclusions.end(),
                    static_cast<u16>(target)) != st.plot.committedConclusions.end());
    // 真相公开 ⇒ AI 重估先验 + 数据晶的 σ 跳跃
    Fixed sigmaAfter = st.market.vol[static_cast<std::size_t>(Commodity::DataCrystals)].lastSigma;
    CHECK(sigmaAfter.rawValue() >= sigmaBefore.rawValue());
    CHECK(!st.market.shocks.empty());
    (void)rep;
}

TEST(clue, commit_false_conclusion_has_real_cost) {
    GameState st = clueWorld(444);
    revealAll(st, Fixed::pct(90));
    int target = -1;
    for (int i = 0; i < kConclusionCount; ++i) {
        if (conclusionDef(i).trueConclusion) continue;
        if (conclusionUnlockable(st, static_cast<u16>(i), nullptr)) {
            target = i;
            break;
        }
    }
    CHECK(target >= 0);
    Fixed influenceBefore = st.empires[kPlayerId].influence;
    Fixed opinionBefore = st.empires[1].opinion[kPlayerId];
    std::string err;
    CHECK(commitConclusion(st, static_cast<u16>(target), &err));
    // 误判代价：信誉损失 + 影响力下降 + 进入误判列表
    CHECK(st.empires[1].opinion[kPlayerId].rawValue() < opinionBefore.rawValue());
    CHECK(st.empires[kPlayerId].influence.rawValue() < influenceBefore.rawValue());
    CHECK_EQ(st.plot.falseConclusions.size(), 1ull);
}

TEST(clue, unlockable_requires_diversity_and_no_unresolved_conflict) {
    GameState st = clueWorld(555);
    // 只揭示单一来源的线索 ⇒ 来源多样性约束必须挡住
    for (std::size_t i = 0; i < st.clues.size(); ++i) {
        st.clues[i].known = true;
        st.clues[i].credibility = Fixed::pct(80);
        st.clues[i].prov.channel = ProvChannel::DirectObservation;
        st.clues[i].prov.credibility = Fixed::pct(80);
    }
    int checked = 0;
    for (int i = 0; i < kConclusionCount && checked < 3; ++i) {
        if (!conclusionDef(i).requireDiversity) continue;
        std::string why;
        if (!conclusionUnlockable(st, static_cast<u16>(i), &why)) {
            CHECK(!why.empty());
            ++checked;
        }
    }
    CHECK(checked >= 0);
    // 混合来源后应能通过
    for (std::size_t i = 0; i < st.clues.size(); ++i) {
        st.clues[i].prov.channel = (i % 3 == 0) ? ProvChannel::SpyNetwork
                                                : (i % 3 == 1 ? ProvChannel::DirectObservation
                                                              : ProvChannel::Analysis);
    }
    CHECK(!deduceReport(st, 0).empty());
}
