// 政体机制：合法性来源、选举、镇压
#include <algorithm>

#include "check.h"
#include "core/GameState.h"
#include "core/TickPipeline.h"
#include "domain/Government.h"
#include "domain/Personnel.h"
#include "gen/WorldGen.h"
#include "plot/BeatResolver.h"
#include "save/Serde.h"

using namespace gf;

namespace {

GameState govWorld(u64 seed = 4242, int systems = 56) {
    WorldGenOptions o;
    o.seed = seed;
    o.difficulty = 3;
    o.empireCount = 8;
    o.systemCount = systems;
    GameState st;
    generateWorld(st, o);
    return st;
}

void govTicks(GameState& st, int n) {
    for (int i = 0; i < n; ++i) {
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);
    }
}

}  // namespace

// 回归守卫：政体原先只有几组数字不同，玩法完全一样 ——
// 没有选举、没有继承危机、没有镇压，玩家感受不到差别。
// 现在以「合法性来源」区分，每种来源有各自的涨落规律与专属手段。
TEST(gov, every_government_has_a_legitimacy_source) {
    for (int g = 0; g < kGovernmentCount; ++g) {
        LegitimacySource s = legitimacySourceOf(static_cast<u8>(g));
        CHECK(s != LegitimacySource::Count);
        CHECK(!legitimacySourceName(s).empty());
        CHECK(!legitimacySourceDesc(s).empty());
    }
    // 至少覆盖四种以上不同来源
    bool seen[static_cast<int>(LegitimacySource::Count)] = {};
    for (int g = 0; g < kGovernmentCount; ++g)
        seen[static_cast<int>(legitimacySourceOf(static_cast<u8>(g)))] = true;
    int kinds = 0;
    for (bool b : seen) if (b) ++kinds;
    CHECK(kinds >= 4);
}

TEST(gov, democracy_holds_elections_and_can_replace_ruler) {
    GameState st = govWorld(7101);
    Empire& me = st.empires[kPlayerId];
    me.government = 0;   // 民主制
    me.influence = Fixed(5000);
    CHECK(isElective(me.government));
    CHECK(!allowsRepression(me.government));

    std::string msg;
    CHECK(callElection(st, kPlayerId, &msg));
    CHECK(me.gov.election.active);
    CHECK(me.gov.election.candidates.size() >= 2);
    // 支持率必须归一化到 100% 附近
    Fixed total = Fixed(0);
    for (const auto& c : me.gov.election.candidates) total += c.support;
    CHECK(total.rawValue() > Fixed::pct(90).rawValue());
    CHECK(total.rawValue() < Fixed::pct(110).rawValue());

    // 公开支持会提升该候选人的支持率
    Fixed before = me.gov.election.candidates[1].support;
    CHECK(endorseCandidate(st, kPlayerId, 1, &msg));
    CHECK(me.gov.election.candidates[1].support.rawValue() > before.rawValue());
    // 越界与无选举时被拒
    CHECK(!endorseCandidate(st, kPlayerId, 99, &msg));

    // 跑完竞选期，必须产生结果
    govTicks(st, 10);
    CHECK(!me.gov.election.active);
    CHECK(me.gov.election.electionsHeld == 1);
    CHECK(!me.ruler.name.empty());
}

TEST(gov, autocracy_cannot_hold_elections) {
    GameState st = govWorld(7102);
    Empire& me = st.empires[kPlayerId];
    me.government = 3;   // 独裁制
    me.influence = Fixed(5000);
    CHECK(!isElective(me.government));
    std::string msg;
    CHECK(!callElection(st, kPlayerId, &msg));
    CHECK(!msg.empty());
}

// 镇压：立竿见影提升合法性，但累积怨恨 —— 怨恨必须有代价
TEST(gov, suppression_raises_legitimacy_but_accumulates_resentment) {
    GameState st = govWorld(7103);
    Empire& me = st.empires[kPlayerId];
    me.government = 3;   // 独裁制（恐惧镇压）
    me.influence = Fixed(5000);
    CHECK(allowsRepression(me.government));
    Fixed legitBefore = legitimacyFromSource(st, kPlayerId);
    std::string msg;
    CHECK(suppress(st, kPlayerId, &msg));
    CHECK(me.gov.repression.rawValue() > 0);
    CHECK(me.gov.resentment.rawValue() > 0);
    Fixed legitAfter = legitimacyFromSource(st, kPlayerId);
    CHECK(legitAfter.rawValue() > legitBefore.rawValue());

    // 怨恨会随时间消退（否则一次镇压永久免费）
    Fixed r0 = me.gov.resentment;
    // 检查自然衰减，不混入战争、政变和后续派系镇压。
    for (int i = 0; i < 60; ++i) { governmentPhase(st); ++st.tick; }
    CHECK(me.gov.resentment.rawValue() < r0.rawValue());
    // 镇压强度同样衰减，需要持续投入
    CHECK(me.gov.repression.rawValue() < Fixed::pct(20).rawValue());
}

TEST(gov, democracy_cannot_suppress) {
    GameState st = govWorld(7104);
    Empire& me = st.empires[kPlayerId];
    me.government = 0;   // 民主制
    me.influence = Fixed(5000);
    std::string msg;
    CHECK(!suppress(st, kPlayerId, &msg));
    CHECK(!msg.empty());
    CHECK_EQ(me.gov.resentment.rawValue(), 0);
}

// 合法性来源必须真的影响合法性数值，而不是所有政体共用一条公式
TEST(gov, legitimacy_source_changes_legitimacy) {
    GameState st = govWorld(7105);
    Empire& me = st.empires[kPlayerId];
    me.domestic.unrest = Fixed::pct(60);   // 高民怨
    me.stability = Fixed::pct(50);
    // 选举授权对民怨最敏感
    me.government = 0;
    Fixed election = legitimacyFromSource(st, kPlayerId);
    // 共识协同几乎不受影响
    me.government = 9;
    Fixed consensus = legitimacyFromSource(st, kPlayerId);
    CHECK(consensus.rawValue() > election.rawValue());
    // 恐惧镇压在镇压后应当高于未镇压
    me.government = 3;
    Fixed fearPlain = legitimacyFromSource(st, kPlayerId);
    me.gov.repression = Fixed::pct(80);
    Fixed fearSuppressed = legitimacyFromSource(st, kPlayerId);
    CHECK(fearSuppressed.rawValue() > fearPlain.rawValue());
    // 怨恨反噬
    me.gov.resentment = Fixed::pct(80);
    Fixed fearResented = legitimacyFromSource(st, kPlayerId);
    CHECK(fearResented.rawValue() < fearSuppressed.rawValue());
}

TEST(gov, elective_governments_auto_start_elections) {
    GameState st = govWorld(7106);
    Empire& me = st.empires[kPlayerId];
    me.government = 0;
    me.ruler.elected = true;
    me.ruler.termEnd = 10;
    govTicks(st, 14);
    // 应当已经启动（或完成）过选举
    CHECK(me.gov.election.electionsHeld > 0 || me.gov.election.active);
}

TEST(gov, state_survives_serialization) {
    GameState st = govWorld(7107);
    Empire& me = st.empires[kPlayerId];
    me.government = 0;
    me.influence = Fixed(5000);
    std::string msg;
    (void)callElection(st, kPlayerId, &msg);
    (void)suppress(st, kPlayerId, &msg);   // 民主制会失败，但不应崩溃
    me.government = 3;
    (void)suppress(st, kPlayerId, &msg);
    govTicks(st, 5);

    std::vector<u8> bytes = serializeState(st);
    GameState back;
    CHECK(tryDeserializeState(bytes, back));
    CHECK_EQ(back.empires[kPlayerId].gov.resentment.rawValue(),
             st.empires[kPlayerId].gov.resentment.rawValue());
    CHECK_EQ(back.empires[kPlayerId].gov.repression.rawValue(),
             st.empires[kPlayerId].gov.repression.rawValue());
    CHECK_EQ(back.empires[kPlayerId].gov.election.active,
             st.empires[kPlayerId].gov.election.active);
    CHECK_EQ(back.empires[kPlayerId].gov.election.candidates.size(),
             st.empires[kPlayerId].gov.election.candidates.size());
    // 续跑必须完全一致（覆盖未序列化字段的回归）
    CHECK_EQ(st.stateHash(), back.stateHash());
    govTicks(st, 8);
    govTicks(back, 8);
    CHECK_EQ(st.stateHash(), back.stateHash());
}
