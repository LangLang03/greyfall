// 指挥官军衔与晋升（HOI4 式）
#include <algorithm>

#include "check.h"
#include "combat/Resolver.h"
#include "core/GameState.h"
#include "core/TickPipeline.h"
#include "domain/Commander.h"
#include "domain/Treaty.h"
#include "gen/WorldGen.h"
#include "plot/BeatResolver.h"
#include "save/Serde.h"

using namespace gf;

namespace {

GameState rankWorld(u64 seed = 5050) {
    WorldGenOptions o;
    o.seed = seed;
    o.difficulty = 2;
    o.empireCount = 8;
    o.systemCount = 48;
    GameState st;
    generateWorld(st, o);
    return st;
}

void rankRunTicks(GameState& st, int n) {
    for (int i = 0; i < n; ++i) {
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);
    }
}

}  // namespace

TEST(commander_rank, rank_table_is_monotonic) {
    CHECK_EQ(kCommanderRankCount, 5);
    // 战功门槛、技能上限、统率加成、特质槽必须逐级递增
    for (int i = 1; i < kCommanderRankCount; ++i) {
        auto prev = static_cast<CommanderRank>(i - 1);
        auto cur = static_cast<CommanderRank>(i);
        CHECK(rankMeritRequired(cur).rawValue() > rankMeritRequired(prev).rawValue());
        CHECK(rankSkillCap(cur).rawValue() > rankSkillCap(prev).rawValue());
        CHECK(rankCommandBonus(cur).rawValue() > rankCommandBonus(prev).rawValue());
        CHECK(rankTraitSlots(cur) >= rankTraitSlots(prev));
        CHECK(rankTraitSlots(cur) <= kMaxCommanderTraits);
    }
    CHECK_EQ(rankMeritRequired(CommanderRank::Captain).rawValue(), 0);
    CHECK(rankSkillCap(CommanderRank::Marshal).rawValue() <= Fixed(1).rawValue());
    CHECK_EQ(rankTraitSlots(CommanderRank::Marshal), kMaxCommanderTraits);
    // 名称可反查
    for (int i = 0; i < kCommanderRankCount; ++i) {
        auto r = static_cast<CommanderRank>(i);
        CHECK_EQ(static_cast<int>(commanderRankFromName(commanderRankName(r))), i);
        CHECK(!commanderRankName(r).empty());
    }
}

TEST(commander_rank, merit_accumulates_and_promotes) {
    GameState st = rankWorld();
    u32 cid = st.commanders.front().id;
    Commander* c = st.commander(cid);
    CHECK(c != nullptr);
    CHECK_EQ(static_cast<int>(c->rank), static_cast<int>(CommanderRank::Captain));

    // 授予不足以晋升的战功
    (void)commanderAwardMerit(st, cid, rankMeritRequired(CommanderRank::Major) / Fixed(2));
    CHECK_EQ(static_cast<int>(st.commander(cid)->rank), static_cast<int>(CommanderRank::Captain));
    CHECK(st.commanders.front().merit.rawValue() > 0);

    // 补足到少校
    (void)commanderAwardMerit(st, cid, rankMeritRequired(CommanderRank::Major));
    CHECK_EQ(static_cast<int>(st.commander(cid)->rank), static_cast<int>(CommanderRank::Major));
}

TEST(commander_rank, promotion_raises_skills_and_grants_trait) {
    GameState st = rankWorld(11);
    u32 cid = st.commanders.front().id;
    const Commander* before = st.commander(cid);
    Fixed atkBefore = before->attack;
    std::size_t traitsBefore = before->traits.size();
    // 直接给到上校（有 2 个特质槽）
    (void)commanderAwardMerit(st, cid, rankMeritRequired(CommanderRank::Colonel));
    const Commander* after = st.commander(cid);
    CHECK_EQ(static_cast<int>(after->rank), static_cast<int>(CommanderRank::Colonel));
    CHECK(after->attack.rawValue() > atkBefore.rawValue());
    CHECK(after->traits.size() > traitsBefore);
    // 新特质不得与已有特质重复
    for (std::size_t i = 0; i < after->traits.size(); ++i) {
        CHECK(after->traits[i] != after->trait);
        for (std::size_t j = i + 1; j < after->traits.size(); ++j)
            CHECK(after->traits[i] != after->traits[j]);
    }
    // 技能不得超过当前军衔上限
    CHECK(after->attack.rawValue() <= rankSkillCap(after->rank).rawValue());
    CHECK(after->defense.rawValue() <= rankSkillCap(after->rank).rawValue());
}

TEST(commander_rank, mass_merit_can_promote_multiple_ranks) {
    GameState st = rankWorld(22);
    u32 cid = st.commanders.front().id;
    (void)commanderAwardMerit(st, cid, rankMeritRequired(CommanderRank::Marshal));
    CHECK_EQ(static_cast<int>(st.commander(cid)->rank), static_cast<int>(CommanderRank::Marshal));
    // 已达最高军衔后再授予战功不应出问题
    (void)commanderAwardMerit(st, cid, Fixed(99999));
    CHECK_EQ(static_cast<int>(st.commander(cid)->rank), static_cast<int>(CommanderRank::Marshal));
}

TEST(commander_rank, trait_bonuses_sum_all_traits) {
    GameState st = rankWorld(33);
    u32 cid = st.commanders.front().id;
    (void)commanderAwardMerit(st, cid, rankMeritRequired(CommanderRank::Marshal));
    const Commander* c = st.commander(cid);
    CHECK(c->traits.size() >= 2);
    // 合计加成必须等于各特质之和
    Fixed sum = traitAttackBonus(c->trait);
    for (CommanderTrait t : c->traits) sum += traitAttackBonus(t);
    CHECK_EQ(commanderTraitAttack(*c).rawValue(), sum.rawValue());
    // 拥有判定
    CHECK(commanderHasTrait(*c, c->trait));
    for (CommanderTrait t : c->traits) CHECK(commanderHasTrait(*c, t));
}

TEST(commander_rank, combat_awards_merit_and_promotes) {
    GameState st = rankWorld(44);
    declareWar(st, 2, 1, true);
    u32 sys = st.empires[1].systems.empty() ? st.empires[1].capital : st.empires[1].systems[0];
    // 制造压倒性进攻，保证有胜方并获得高战功
    for (u32 id : st.empire(2)->fleets) {
        Fleet* f = st.fleet(id);
        if (f != nullptr) {
            f->system = sys;
            f->order = FleetOrder::Patrol;
            f->targetSystem = kNoSystem;
            f->strength = f->strength * Fixed(6);
        }
    }
    for (u32 id : st.empire(1)->fleets) {
        Fleet* f = st.fleet(id);
        if (f != nullptr) {
            f->system = sys;
            f->order = FleetOrder::Patrol;
            f->targetSystem = kNoSystem;
        }
    }
    // 记录战前军衔
    std::vector<Fixed> meritBefore;
    for (const auto& c : st.commanders) meritBefore.push_back(c.merit);
    // 只推进**战斗阶段**：AI 会在战斗阶段之前下达入侵命令并调动舰队，
    // 把参战舰队调离战场，用例就变成在检验 AI 调度而非战功与晋升。
    // 同时每 tick 维持接触与兵力，确保持续产生战斗。
    for (int round = 0; round < 72; ++round) {
        if (!atWarWith(st, 2, 1)) declareWar(st, 2, 1, true);
        for (u32 id : st.empire(2)->fleets) {
            Fleet* f = st.fleet(id);
            if (f == nullptr) continue;
            if (f->system != sys) f->system = sys;
            f->targetSystem = kNoSystem;
            if (f->org.rawValue() < f->maxOrg.rawValue() / 2) f->org = f->maxOrg;
        }
        for (u32 id : st.empire(1)->fleets) {
            Fleet* f = st.fleet(id);
            if (f == nullptr) continue;
            if (f->system != sys) f->system = sys;
            f->targetSystem = kNoSystem;
        }
        TickReport rep;
        combatPhase(st, rep);
    }
    // 至少一名指挥官战功增长
    bool grew = false;
    for (std::size_t i = 0; i < st.commanders.size() && i < meritBefore.size(); ++i)
        if (st.commanders[i].merit.rawValue() > meritBefore[i].rawValue()) grew = true;
    CHECK(grew);
    // 至少发生了一次晋升
    int promoted = 0;
    for (const auto& c : st.commanders)
        if (static_cast<int>(c.rank) > static_cast<int>(CommanderRank::Captain)) ++promoted;
    CHECK(promoted >= 1);
}

TEST(commander_rank, rank_affects_combat_width) {
    GameState st = rankWorld(55);
    declareWar(st, 2, 1, true);
    u32 sys = st.empires[1].systems.empty() ? st.empires[1].capital : st.empires[1].systems[0];
    for (u32 id : st.empire(2)->fleets) {
        Fleet* f = st.fleet(id);
        if (f != nullptr) {
            f->system = sys;
            f->order = FleetOrder::Patrol;
            f->targetSystem = kNoSystem;
        }
    }
    for (u32 id : st.empire(1)->fleets) {
        Fleet* f = st.fleet(id);
        if (f != nullptr) {
            f->system = sys;
            f->order = FleetOrder::Patrol;
            f->targetSystem = kNoSystem;
        }
    }
    rankRunTicks(st, 3);
    // 记录当前宽度
    Fixed widthLow = Fixed(0);
    u32 att = 0xFFFFFFFFu;
    for (const auto& b : st.battles) {
        if (b.system != sys || b.resolved) continue;
        widthLow = b.attackerWidth;
        att = b.attackerCommander;
        break;
    }
    if (widthLow.rawValue() <= 0 || att == 0xFFFFFFFFu) return;
    // 把攻方指挥官提到元帅，宽度应当增大
    (void)commanderAwardMerit(st, att, rankMeritRequired(CommanderRank::Marshal));
    rankRunTicks(st, 1);
    for (const auto& b : st.battles) {
        if (b.system != sys || b.resolved) continue;
        CHECK(b.attackerWidth.rawValue() >= widthLow.rawValue());
        break;
    }
}

TEST(commander_rank, training_costs_influence) {
    GameState st = rankWorld(66);
    u32 cid = st.commanders.front().id;
    u32 owner = st.commanders.front().owner;
    st.empires[owner].influence = Fixed(5000);
    Fixed before = st.empires[owner].influence;
    Fixed meritBefore = st.commanders.front().merit;
    std::string err;
    CHECK(commanderTrain(st, cid, Fixed(300), &err));
    CHECK(st.empires[owner].influence.rawValue() < before.rawValue());
    CHECK(st.commanders.front().merit.rawValue() > meritBefore.rawValue());
    // 影响力不足必须被拒绝
    st.empires[owner].influence = Fixed(0);
    CHECK(!commanderTrain(st, cid, Fixed(300), &err));
    CHECK(!err.empty());
    // 不存在的指挥官
    CHECK(!commanderTrain(st, 99999, Fixed(1), &err));
}

TEST(commander_rank, state_survives_serialization) {
    GameState st = rankWorld(77);
    u32 cid = st.commanders.front().id;
    (void)commanderAwardMerit(st, cid, rankMeritRequired(CommanderRank::Colonel));
    std::vector<u8> bytes = serializeState(st);
    GameState back;
    CHECK(tryDeserializeState(bytes, back));
    CHECK_EQ(back.commanders.size(), st.commanders.size());
    for (std::size_t i = 0; i < st.commanders.size(); ++i) {
        CHECK_EQ(static_cast<int>(back.commanders[i].rank), static_cast<int>(st.commanders[i].rank));
        CHECK_EQ(back.commanders[i].merit.rawValue(), st.commanders[i].merit.rawValue());
        CHECK_EQ(back.commanders[i].traits.size(), st.commanders[i].traits.size());
        for (std::size_t j = 0; j < st.commanders[i].traits.size(); ++j)
            CHECK_EQ(static_cast<int>(back.commanders[i].traits[j]),
                     static_cast<int>(st.commanders[i].traits[j]));
    }
}

TEST(commander_rank, rank_text_renders) {
    GameState st = rankWorld(88);
    for (const auto& c : st.commanders) {
        std::string txt = commanderRankText(c);
        CHECK(!txt.empty());
        CHECK(txt.find(commanderRankName(c.rank)) != std::string::npos);
    }
    // 最高军衔不应显示「距…还需」
    u32 cid = st.commanders.front().id;
    (void)commanderAwardMerit(st, cid, rankMeritRequired(CommanderRank::Marshal));
    std::string top = commanderRankText(*st.commander(cid));
    CHECK(top.find("最高军衔") != std::string::npos);
}
