// 和平会议与领土割让
#include <algorithm>

#include "check.h"
#include "core/GameState.h"
#include "core/TickPipeline.h"
#include "domain/Peace.h"
#include "domain/Treaty.h"
#include "gen/WorldGen.h"
#include "plot/BeatResolver.h"
#include "save/Serde.h"

using namespace gf;

namespace {

GameState peaceWorld(u64 seed = 8080, int systems = 48) {
    WorldGenOptions o;
    o.seed = seed;
    o.difficulty = 2;
    o.empireCount = 8;
    o.systemCount = systems;
    GameState st;
    generateWorld(st, o);
    return st;
}

/// 找到一个属于 loser、且不属于 winner 的星系
u32 pickTarget(const GameState& st, u32 loser) {
    const Empire* l = st.empire(loser);
    if (l == nullptr) return kNoSystem;
    for (u32 s : l->systems) {
        const SystemNode* n = st.system(s);
        if (n == nullptr) continue;
        if (n->capital) continue;   // 先避开首都，便于单独测试普通割让
        return s;
    }
    return kNoSystem;
}

}  // namespace

TEST(peace, cost_model_is_affordable) {
    // 割让代价必须与「一场战争能挣到的分数」同量级，
    // 否则胜方打赢了也什么都拿不到（早期普通星系定价 9+ 分，
    // 而一场战争通常只挣 4~12 分）。
    GameState st = peaceWorld();
    Fixed maxCost = Fixed(0);
    int n = 0;
    for (const auto& s : st.map.systems) {
        if (s.owner == kNoEmpire) continue;
        Fixed c = annexCost(st, s.id);
        CHECK(c.rawValue() > 0);
        if (c.rawValue() > maxCost.rawValue()) maxCost = c;
        ++n;
    }
    CHECK(n > 0);
    // 至少有一个普通（非首都、非巨构）星系的代价落在可挣到的范围内
    int affordable = 0;
    for (const auto& s : st.map.systems) {
        if (s.owner == kNoEmpire || s.capital || s.megastructure) continue;
        if (annexCost(st, s.id).rawValue() <= Fixed(8).rawValue()) ++affordable;
    }
    CHECK(affordable > 0);
    // 赔款 / 技术 / 人力 / 附庸的代价必须为正且层级合理
    CHECK(reparationsCost(Fixed(5000)).rawValue() > 0);
    CHECK(techTransferCost().rawValue() > 0);
    CHECK(manpowerCost(Fixed(500)).rawValue() > 0);
    CHECK(vassalizeCost().rawValue() > techTransferCost().rawValue());
}

TEST(peace, convene_requires_war) {
    GameState st = peaceWorld(11);
    std::string err;
    // 未交战不得召开
    CHECK(!convenePeace(st, 1, 2, &err));
    CHECK(!err.empty());
    declareWar(st, 1, 2, true);
    CHECK(convenePeace(st, 1, 2, &err));
    // 重复召开必须失败
    CHECK(!convenePeace(st, 1, 2, &err));
    CHECK(findConference(st, 1, 2) != nullptr);
    CHECK(findConference(st, 2, 1) != nullptr);   // 任一方向都能查到
    // 非法主体
    CHECK(!convenePeace(st, 99, 2, &err));
}

TEST(peace, demands_consume_war_score) {
    GameState st = peaceWorld(22);
    declareWar(st, 1, 2, true);
    st.relation(1, 2).warScore = 100;   // 给足分数
    std::string err;
    CHECK(convenePeace(st, 1, 2, &err));
    Fixed before = peaceRemaining(st, 1);
    CHECK(before.rawValue() > 0);
    // 赔款要求
    CHECK(addDemand(st, 1, makeReparations(Fixed(5000)), &err));
    CHECK(peaceRemaining(st, 1).rawValue() < before.rawValue());
    // 分数耗尽后不得再提
    GameState st2 = peaceWorld(23);
    declareWar(st2, 1, 2, true);
    st2.relation(1, 2).warScore = 1;
    CHECK(convenePeace(st2, 1, 2, &err));
    CHECK(!addDemand(st2, 1, makeReparations(Fixed(999999)), &err));
    CHECK(!err.empty());
}

TEST(peace, annex_requires_target_ownership) {
    GameState st = peaceWorld(33);
    declareWar(st, 1, 2, true);
    st.relation(1, 2).warScore = 200;
    std::string err;
    CHECK(convenePeace(st, 1, 2, &err));
    u32 target = pickTarget(st, 2);
    if (target == kNoSystem) return;
    // 索取战败方的星系
    CHECK(addDemand(st, 1, makeAnnex(st, target), &err));
    // 重复索取必须失败
    CHECK(!addDemand(st, 1, makeAnnex(st, target), &err));
    // 索取自己（或第三方）的星系必须失败
    const Empire* me = st.empire(1);
    if (me != nullptr && !me->systems.empty()) {
        CHECK(!addDemand(st, 1, makeAnnex(st, me->systems.front()), &err));
    }
    // 非法星系
    CHECK(!addDemand(st, 1, makeAnnex(st, 99999), &err));
}

TEST(peace, conclude_transfers_territory_and_ends_war) {
    GameState st = peaceWorld(44);
    declareWar(st, 1, 2, true);
    st.relation(1, 2).warScore = 200;
    std::string err;
    CHECK(convenePeace(st, 1, 2, &err));
    u32 target = pickTarget(st, 2);
    if (target == kNoSystem) return;
    std::size_t winnerBefore = st.empire(1)->systems.size();
    std::size_t loserBefore = st.empire(2)->systems.size();
    Fixed loserCashBefore = st.empire(2)->treasury;
    CHECK(addDemand(st, 1, makeAnnex(st, target), &err));
    CHECK(addDemand(st, 1, makeReparations(Fixed(10000)), &err));
    CHECK(concludePeace(st, 1, &err));

    // 星系易主
    CHECK_EQ(st.system(target)->owner, 1u);
    CHECK_EQ(st.empire(1)->systems.size(), winnerBefore + 1);
    CHECK_EQ(st.empire(2)->systems.size(), loserBefore - 1);
    CHECK(std::find(st.empire(1)->systems.begin(), st.empire(1)->systems.end(), target) !=
          st.empire(1)->systems.end());
    // 行星归属随之转移
    for (u32 pid : st.system(target)->planets) {
        const Planet* p = st.planet(pid);
        if (p != nullptr) CHECK_EQ(p->owner, 1u);
    }
    // 赔款已支付（战败方国库减少或已见底）
    CHECK(st.empire(2)->treasury.rawValue() <= loserCashBefore.rawValue());
    // 战争结束
    CHECK(!atWarWith(st, 1, 2));
    // 会议已归档
    CHECK(findConference(st, 1, 2) == nullptr);
}

TEST(peace, capital_fallen_grants_unconditional_conference) {
    // 首都失守 ⇒ 无条件会议，且获得大量分数
    GameState st = peaceWorld(55);
    declareWar(st, 1, 2, true);
    const Empire* loser = st.empire(2);
    CHECK(loser != nullptr);
    // 直接把战败方首都划给胜方
    st.system(loser->capital)->owner = 1;
    std::string err;
    CHECK(convenePeace(st, 1, 2, &err));
    const PeaceConference* c = findConference(st, 1, 2);
    CHECK(c != nullptr);
    CHECK(c->unconditional);
    // 无条件会议给足分数，足以索取重价要求
    CHECK(c->warScore.rawValue() >= Fixed(20).rawValue());
    // 无条件时提出要求仍然只允许胜方
    CHECK(addDemand(st, 1, makeReparations(Fixed(5000)), &err));
}

TEST(peace, abandon_keeps_war_going) {
    GameState st = peaceWorld(66);
    declareWar(st, 1, 2, true);
    std::string err;
    CHECK(convenePeace(st, 1, 2, &err));
    CHECK(abandonPeace(st, 1, &err));
    CHECK(atWarWith(st, 1, 2));
    CHECK(findConference(st, 1, 2) == nullptr);
    // 战败方无权放弃由胜方主导的会议
    GameState st2 = peaceWorld(67);
    declareWar(st2, 1, 2, true);
    CHECK(convenePeace(st2, 1, 2, &err));
    CHECK(!abandonPeace(st2, 2, &err));
    // 战败方也无权提出要求
    CHECK(!addDemand(st2, 2, makeReparations(Fixed(1000)), &err));
}

TEST(peace, remove_demand_refunds_score) {
    GameState st = peaceWorld(77);
    declareWar(st, 1, 2, true);
    st.relation(1, 2).warScore = 200;
    std::string err;
    CHECK(convenePeace(st, 1, 2, &err));
    CHECK(addDemand(st, 1, makeReparations(Fixed(5000)), &err));
    Fixed afterAdd = peaceRemaining(st, 1);
    CHECK(removeDemand(st, 1, 0, &err));
    CHECK(peaceRemaining(st, 1).rawValue() > afterAdd.rawValue());
    // 越界
    CHECK(!removeDemand(st, 1, 99, &err));
}

TEST(peace, should_convene_detects_threshold_and_capital) {
    GameState st = peaceWorld(88);
    declareWar(st, 1, 2, true);
    CHECK(!shouldConvenePeace(st, 1, 2));
    // 战争分数达阈值
    st.relation(1, 2).warScore = 3;
    CHECK(shouldConvenePeace(st, 1, 2));
    // 反向也算
    GameState st2 = peaceWorld(89);
    declareWar(st2, 1, 2, true);
    st2.relation(2, 1).warScore = 5;
    CHECK(shouldConvenePeace(st2, 1, 2));
    // 未交战不算
    CHECK(!shouldConvenePeace(st2, 2, 3));
    // 首都失守
    GameState st3 = peaceWorld(90);
    declareWar(st3, 1, 2, true);
    st3.system(st3.empire(2)->capital)->owner = 1;
    CHECK(shouldConvenePeace(st3, 1, 2));
}

TEST(peace, ai_wars_conclude_with_territory_change) {
    // 端到端（直接驱动 peacePhase）：AI 之间的战争应当以「领土变更 +
    // 签订条约」而非「静默停战」结束。
    //
    // 注意不要依赖「自然涌现的战斗」——那取决于星图距离与 AI 调度，
    // 会随种子与平衡调整而波动。这里直接把战争分数推到阈值之上，
    // 检验 peacePhase 自身的清算逻辑。
    GameState st = peaceWorld(99, 56);
    declareWar(st, 2, 1, true);
    // 战争分数是**有方向的**：relation(2,1) 表示 2 对 1 的战果。
    // 只设一方占优，这才是真实状态（双方分数相同则无人获胜）。
    st.relation(2, 1).warScore = 8;
    st.relation(1, 2).warScore = 1;
    CHECK(shouldConvenePeace(st, 2, 1));

    std::size_t loserBefore = st.empire(1)->systems.size();
    std::size_t winnerBefore = st.empire(2)->systems.size();
    Fixed loserCashBefore = st.empire(1)->treasury;

    peacePhase(st);

    // 战争已结束
    CHECK(!atWarWith(st, 2, 1));
    // 条约已签订（日志中有记录）
    bool sawTreaty = false;
    for (const auto& le : st.log)
        if (le.text.find("和平条约") != std::string::npos) sawTreaty = true;
    CHECK(sawTreaty);
    // 战败方失去领土（或至少付出赔款）—— 战争必须有实际后果
    bool lostLand = st.empire(1)->systems.size() < loserBefore;
    bool paid = st.empire(1)->treasury.rawValue() < loserCashBefore.rawValue();
    CHECK(lostLand || paid);
    // 胜方没有丢掉领土
    CHECK(st.empire(2)->systems.size() >= winnerBefore);
}

TEST(peace, state_survives_serialization) {
    GameState st = peaceWorld(111);
    declareWar(st, 1, 2, true);
    st.relation(1, 2).warScore = 100;
    std::string err;
    CHECK(convenePeace(st, 1, 2, &err));
    u32 target = pickTarget(st, 2);
    if (target != kNoSystem) (void)addDemand(st, 1, makeAnnex(st, target), &err);
    CHECK(!st.peace.empty());

    std::vector<u8> bytes = serializeState(st);
    GameState back;
    CHECK(tryDeserializeState(bytes, back));
    CHECK_EQ(back.peace.size(), st.peace.size());
    for (std::size_t i = 0; i < st.peace.size(); ++i) {
        CHECK_EQ(back.peace[i].active, st.peace[i].active);
        CHECK_EQ(back.peace[i].winner, st.peace[i].winner);
        CHECK_EQ(back.peace[i].loser, st.peace[i].loser);
        CHECK_EQ(back.peace[i].warScore.rawValue(), st.peace[i].warScore.rawValue());
        CHECK_EQ(back.peace[i].demands.size(), st.peace[i].demands.size());
    }
}
