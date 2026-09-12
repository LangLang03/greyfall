// 间谍网络：渗透累积 / 特工池 / 任务 / 暴露与破获 / 反间谍对抗
#include <algorithm>

#include "check.h"
#include "core/GameState.h"
#include "core/TickPipeline.h"
#include "domain/Fog.h"
#include "domain/SpyNetwork.h"
#include "gen/WorldGen.h"
#include "plot/BeatResolver.h"
#include "save/Serde.h"

using namespace gf;

namespace {

GameState spyWorld(u64 seed = 4040) {
    WorldGenOptions o;
    o.seed = seed;
    o.difficulty = 2;
    o.empireCount = 8;
    o.systemCount = 48;
    GameState st;
    generateWorld(st, o);
    return st;
}

void spyRunTicks(GameState& st, int n) {
    for (int i = 0; i < n; ++i) {
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);
    }
}

const SpyNetwork* netOf(const GameState& st, u32 empire, u32 target) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return nullptr;
    for (const auto& n : e->spy.networks)
        if (n.target == target && !n.burned) return &n;
    return nullptr;
}

}  // namespace

TEST(spy, mission_table_is_consistent) {
    CHECK_EQ(kSpyMissionCount, 8);
    for (int i = 0; i < kSpyMissionCount; ++i) {
        auto m = static_cast<SpyMission>(i);
        CHECK(!spyMissionName(m).empty());
        CHECK(spyMissionMinInfiltration(m).rawValue() > 0);
        CHECK(spyMissionMinInfiltration(m).rawValue() <= Fixed(1).rawValue());
        CHECK(spyMissionCost(m).rawValue() > 0);
        CHECK(spyMissionUpkeep(m) > 0);
        // 名称可反查
        CHECK_EQ(static_cast<int>(spyMissionFromName(spyMissionName(m))), i);
    }
    // 渗透门槛必须随任务强度递增 —— 但**反渗透是防御性任务**，
    // 它针对本国、门槛刻意压低（20%），不参与这条单调性约束。
    Fixed prev = Fixed(-1);
    for (int i = 0; i < kSpyMissionCount; ++i) {
        auto m = static_cast<SpyMission>(i);
        if (m == SpyMission::CounterIntel) continue;
        Fixed cur = spyMissionMinInfiltration(m);
        if (prev.rawValue() >= 0) CHECK(cur.rawValue() >= prev.rawValue());
        prev = cur;
    }
    CHECK_EQ(static_cast<int>(spyMissionFromName("不存在")), static_cast<int>(SpyMission::Count));
}

TEST(spy, empire_starts_with_agents) {
    GameState st = spyWorld();
    for (const auto& e : st.empires) {
        CHECK(e.spy.totalAgents >= 1);
        CHECK_EQ(e.spy.agentPool, e.spy.totalAgents);
        CHECK(e.spy.networks.empty());
    }
}

TEST(spy, establish_uses_agent_and_treasury) {
    GameState st = spyWorld(11);
    st.empires[kPlayerId].treasury = Fixed(200000);
    st.market.margin.cash = Fixed(200000);
    int poolBefore = st.empires[kPlayerId].spy.agentPool;
    Fixed cashBefore = st.empires[kPlayerId].treasury;
    std::string err;
    CHECK(spyEstablish(st, kPlayerId, 1, &err));
    // 占用一名特工、扣除建网费用
    CHECK_EQ(st.empires[kPlayerId].spy.agentPool, poolBefore - 1);
    CHECK(st.empires[kPlayerId].treasury.rawValue() < cashBefore.rawValue());
    CHECK(netOf(st, kPlayerId, 1) != nullptr);
    // 重复建立必须失败
    CHECK(!spyEstablish(st, kPlayerId, 1, &err));
    CHECK(!err.empty());
    // 不能对自己建立
    CHECK(!spyEstablish(st, kPlayerId, kPlayerId, &err));
    // 非法目标
    CHECK(!spyEstablish(st, kPlayerId, 9999, &err));
}

TEST(spy, cannot_establish_without_free_agents) {
    GameState st = spyWorld(22);
    st.empires[kPlayerId].treasury = Fixed(900000);
    st.market.margin.cash = Fixed(900000);
    std::string err;
    // 把所有特工派驻到不同目标，直到用尽
    int agents = st.empires[kPlayerId].spy.totalAgents;
    int made = 0;
    for (u32 t = 1; t <= static_cast<u32>(agents); ++t) {
        if (spyEstablish(st, kPlayerId, t, &err)) ++made;
    }
    CHECK_EQ(made, agents);
    CHECK_EQ(st.empires[kPlayerId].spy.agentPool, 0);
    // 特工用尽后必须拒绝
    CHECK(!spyEstablish(st, kPlayerId, static_cast<u32>(agents + 2), &err));
    CHECK(err.find("特工") != std::string::npos);
}

TEST(spy, infiltration_grows_over_time) {
    GameState st = spyWorld(33);
    st.empires[kPlayerId].treasury = Fixed(200000);
    st.market.margin.cash = Fixed(200000);
    std::string err;
    CHECK(spyEstablish(st, kPlayerId, 1, &err));
    Fixed start = netOf(st, kPlayerId, 1)->infiltration;
    spyRunTicks(st, 20);
    const SpyNetwork* n = netOf(st, kPlayerId, 1);
    CHECK(n != nullptr);
    CHECK(n->infiltration.rawValue() > start.rawValue());
    // 渗透度必须落在 0..1
    CHECK(n->infiltration.rawValue() >= 0);
    CHECK(n->infiltration.rawValue() <= Fixed(1).rawValue());
    CHECK(n->exposure.rawValue() >= 0);
    CHECK(n->exposure.rawValue() <= Fixed(1).rawValue());
}

TEST(spy, more_agents_infiltrate_faster) {
    // 对照实验：1 名 vs 3 名特工在同样季数后的渗透度
    auto measure = [](int agents) {
        GameState st = spyWorld(44);
        st.empires[kPlayerId].treasury = Fixed(900000);
        st.market.margin.cash = Fixed(900000);
        std::string err;
        (void)spyEstablish(st, kPlayerId, 1, &err);
        if (agents > 1) (void)spyAssignAgents(st, kPlayerId, 1, agents - 1, &err);
        spyRunTicks(st, 10);
        const SpyNetwork* n = netOf(st, kPlayerId, 1);
        return n != nullptr ? n->infiltration : Fixed(0);
    };
    Fixed one = measure(1);
    Fixed three = measure(3);
    CHECK(three.rawValue() > one.rawValue());
}

TEST(spy, mission_requires_infiltration_and_network) {
    GameState st = spyWorld(55);
    st.empires[kPlayerId].treasury = Fixed(900000);
    st.market.margin.cash = Fixed(900000);
    std::string err;
    // 没有网络时不能执行任务
    CHECK(!spyRunMission(st, kPlayerId, 1, SpyMission::Recon, &err));
    CHECK(err.find("网络") != std::string::npos);
    CHECK(spyEstablish(st, kPlayerId, 1, &err));
    // 渗透不足时不能执行高门槛任务
    CHECK(!spyRunMission(st, kPlayerId, 1, SpyMission::Sabotage, &err));
    CHECK(err.find("渗透度") != std::string::npos);
}

TEST(spy, mission_consumes_infiltration_and_records) {
    GameState st = spyWorld(66);
    st.empires[kPlayerId].treasury = Fixed(9000000);
    st.market.margin.cash = Fixed(9000000);
    std::string err;
    CHECK(spyEstablish(st, kPlayerId, 1, &err));
    // 隔离任务结算；长局可能使目标灭亡或网络被破获。
    for (auto& network : st.player().spy.networks)
        if (network.target == 1) network.infiltration = Fixed::pct(80);
    const SpyNetwork* before = netOf(st, kPlayerId, 1);
    CHECK(before != nullptr);
    if (before == nullptr) return;
    if (before->infiltration.rawValue() < spyMissionMinInfiltration(SpyMission::StealTech).rawValue()) return;
    Fixed infBefore = before->infiltration;
    Fixed expoBefore = before->exposure;
    u32 runsBefore = before->missionsRun;
    bool ok = spyRunMission(st, kPlayerId, 1, SpyMission::StealTech, &err);
    const SpyNetwork* after = netOf(st, kPlayerId, 1);
    // 任务可能因掷骰失败，但无论成败都应记录并消耗渗透度
    CHECK(after != nullptr);
    if (after == nullptr) return;
    CHECK_EQ(after->missionsRun, runsBefore + 1);
    CHECK(after->infiltration.rawValue() < infBefore.rawValue());
    // 执行任务必然提高暴露
    CHECK(after->exposure.rawValue() > expoBefore.rawValue());
    (void)ok;
}

TEST(spy, quiet_network_survives_but_active_one_burns) {
    // 核心权衡：不执行任务 ⇒ 暴露自然降温，网络可长期存活；
    // 频繁执行任务 ⇒ 暴露累积，最终被破获。
    auto lifetime = [](bool active, int maxT) {
        GameState st = spyWorld(77);
        st.empires[kPlayerId].treasury = Fixed(9000000);
        st.market.margin.cash = Fixed(9000000);
        std::string err;
        (void)spyEstablish(st, kPlayerId, 1, &err);
        (void)spyAssignAgents(st, kPlayerId, 1, 2, &err);
        for (int t = 1; t <= maxT; ++t) {
            spyRunTicks(st, 1);
            if (!st.empires[kPlayerId].spy.hasNetwork(1)) return t;
            if (active && t % 12 == 0) {
                err.clear();
                (void)spyRunMission(st, kPlayerId, 1, SpyMission::Recon, &err);
            }
        }
        return 0;   // 存活到底
    };
    int quiet = lifetime(false, 300);
    int active = lifetime(true, 300);
    // 安静网络不被破获（返回 0 表示存活到测试结束）
    CHECK_EQ(quiet, 0);
    // 活跃网络最终被破获
    CHECK(active > 0);
    CHECK(active < 300);
}

TEST(spy, higher_counter_intel_shortens_lifetime) {
    auto lifetime = [](Fixed ci) {
        GameState st = spyWorld(88);
        st.empires[kPlayerId].treasury = Fixed(9000000);
        st.market.margin.cash = Fixed(9000000);
        st.empires[1].counterIntel = ci;
        std::string err;
        (void)spyEstablish(st, kPlayerId, 1, &err);
        (void)spyAssignAgents(st, kPlayerId, 1, 2, &err);
        for (int t = 1; t <= 400; ++t) {
            spyRunTicks(st, 1);
            if (!st.empires[kPlayerId].spy.hasNetwork(1)) return t;
            if (t % 12 == 0) {
                err.clear();
                (void)spyRunMission(st, kPlayerId, 1, SpyMission::Recon, &err);
            }
        }
        return 0;
    };
    int low = lifetime(Fixed(0));
    int high = lifetime(Fixed::pct(40));
    // 对方反间谍越强，网络寿命越短（两者都必须被破获才可比较）
    CHECK(low > 0);
    CHECK(high > 0);
    CHECK(high < low);
    // 反间谍强度可查询
    GameState st = spyWorld(89);
    Fixed base = counterIntelOf(st, 1);
    st.empires[1].counterIntel = st.empires[1].counterIntel + Fixed::pct(30);
    // 提高基础反间谍后，净强度必须同步提高（修正项可能为负，故不直接比 30%）
    CHECK(counterIntelOf(st, 1).rawValue() > base.rawValue());
    CHECK(infiltrationPowerOf(st, kPlayerId).rawValue() > 0);
}

TEST(spy, burned_network_frees_agents_and_penalizes) {
    GameState st = spyWorld(99);
    st.empires[kPlayerId].treasury = Fixed(9000000);
    st.market.margin.cash = Fixed(9000000);
    std::string err;
    CHECK(spyEstablish(st, kPlayerId, 1, &err));
    const int agents = netOf(st, kPlayerId, 1)->agents;
    const int poolAfterEstablish = st.empires[kPlayerId].spy.agentPool;
    Fixed creditBefore = st.empires[kPlayerId].creditRating;
    Fixed opinionBefore = st.relation(1, kPlayerId).opinion;
    // 主动把暴露推满
    for (auto& n : st.empires[kPlayerId].spy.networks) n.exposure = Fixed(1);
    spyRunTicks(st, 1);
    // 网络被清除，特工回收，声望与关系受损
    CHECK(!st.empires[kPlayerId].spy.hasNetwork(1));
    CHECK_EQ(st.empires[kPlayerId].spy.agentPool, poolAfterEstablish + agents);
    CHECK(st.empires[kPlayerId].spy.totalBurned >= 1);
    CHECK(st.empires[kPlayerId].creditRating.rawValue() < creditBefore.rawValue());
    CHECK(st.relation(1, kPlayerId).opinion.rawValue() < opinionBefore.rawValue());
}

TEST(spy, disband_returns_agents) {
    GameState st = spyWorld(111);
    st.empires[kPlayerId].treasury = Fixed(900000);
    st.market.margin.cash = Fixed(900000);
    std::string err;
    CHECK(spyEstablish(st, kPlayerId, 1, &err));
    CHECK(spyAssignAgents(st, kPlayerId, 1, 2, &err));
    int total = 3;
    CHECK_EQ(netOf(st, kPlayerId, 1)->agents, total);
    CHECK(spyDisband(st, kPlayerId, 1, &err));
    CHECK(netOf(st, kPlayerId, 1) == nullptr);
    // 三名额外的特工全部回收
    CHECK_EQ(st.empires[kPlayerId].spy.agentPool, st.empires[kPlayerId].spy.totalAgents);
    // 撤销不存在的网络必须失败
    CHECK(!spyDisband(st, kPlayerId, 1, &err));
}

TEST(spy, agent_assignment_validates_bounds) {
    GameState st = spyWorld(222);
    st.empires[kPlayerId].treasury = Fixed(900000);
    st.market.margin.cash = Fixed(900000);
    std::string err;
    CHECK(spyEstablish(st, kPlayerId, 1, &err));
    // 超出空闲特工数必须失败
    CHECK(!spyAssignAgents(st, kPlayerId, 1, 99, &err));
    CHECK(!err.empty());
    // 撤回多于现有特工数量时钳制到 0，不应出现负数
    CHECK(spyAssignAgents(st, kPlayerId, 1, -99, &err));
    const SpyNetwork* n = netOf(st, kPlayerId, 1);
    CHECK(n != nullptr);
    CHECK(n->agents >= 0);
    // 没有网络的目标
    CHECK(!spyAssignAgents(st, kPlayerId, 5, 1, &err));
}

TEST(spy, ai_empires_build_networks) {
    GameState st = spyWorld(333);
    spyRunTicks(st, 60);
    int withNet = 0;
    for (const auto& e : st.empires) {
        if (e.isPlayer) continue;
        if (!e.spy.networks.empty()) ++withNet;
    }
    CHECK(withNet >= 1);
}

// 回归守卫：`empires` 曾直接列出所有国家的**精确国库与国力** ——
// 玩家无需任何情报工作就能看出谁虚弱，开战决策退化为纯算术，
// 间谍系统（渗透、侦察、反间谍）因此形同虚设。
// 现行规则：按情报等级分级可见，未达门槛只给区间。
// 回归守卫：间谍系统原先只有 5 种任务，缺少**意识形态渗透**与**主动反渗透** ——
// 渗透只能偷科技/搞破坏，无法长期改变对方社会；防御方也完全被动。
TEST(spy, ideological_infiltration_builds_pressure) {
    GameState st = spyWorld(7401);
    u32 foe = 1;
    st.empires[kPlayerId].treasury = Fixed(5000000);
    st.empires[kPlayerId].influence = Fixed(9000);
    (void)spyEstablish(st, kPlayerId, foe, nullptr);
    (void)spyAssignAgents(st, kPlayerId, foe, 5, nullptr);
    // 提到足够渗透度
    for (int i = 0; i < 60; ++i) {
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);
    }
    Fixed before = ideologyPressureOf(st, foe, kPlayerId);
    int ok = 0;
    for (int i = 0; i < 12; ++i) {
        st.empires[kPlayerId].treasury = Fixed(5000000);
        for (auto& n : st.empires[kPlayerId].spy.networks)
            if (n.target == foe && !n.burned) n.infiltration = Fixed::pct(95);
        std::string err;
        if (spyRunMission(st, kPlayerId, foe, SpyMission::Ideological, &err)) ++ok;
    }
    Fixed after = ideologyPressureOf(st, foe, kPlayerId);
    // 执行成功则压力必须上升（至少有一次成功）
    if (ok > 0) CHECK(after.rawValue() > before.rawValue());
    CHECK(after.rawValue() <= Fixed(1).rawValue());
    // 压力会衰减：停止渗透后应当回落
    Fixed peak = after;
    if (peak.rawValue() > Fixed::pct(20).rawValue()) {
        for (int i = 0; i < 40; ++i) {
            advanceOneTick(st);
            while (!st.pending.empty()) resolveChoiceAuto(st, 0);
        }
        CHECK(ideologyPressureOf(st, foe, kPlayerId).rawValue() < peak.rawValue());
    }
}

TEST(spy, counter_infiltration_weakens_foreign_networks) {
    GameState st = spyWorld(7402);
    st.empires[1].influence = Fixed(9000);
    st.empires[1].treasury = Fixed(5000000);
    std::string err;
    if (!spyEstablish(st, 1, kPlayerId, &err)) return;
    (void)spyAssignAgents(st, 1, kPlayerId, 3, nullptr);
    // 让外国网络积累一些渗透度
    for (int i = 0; i < 5; ++i) {
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);
    }
    Fixed infBefore = Fixed(0);
    int n = 0;
    for (const auto& net : st.empires[1].spy.networks)
        if (net.target == kPlayerId && !net.burned) {
            infBefore = net.infiltration;
            ++n;
        }
    if (n == 0) return;   // 已被动破获
    st.empires[kPlayerId].treasury = Fixed(5000000);
    std::string msg;
    CHECK(counterInfiltrate(st, kPlayerId, &msg));
    CHECK(!msg.empty());
    Fixed infAfter = Fixed(0);
    for (const auto& net : st.empires[1].spy.networks)
        if (net.target == kPlayerId && !net.burned) infAfter = net.infiltration;
    CHECK(infAfter.rawValue() < infBefore.rawValue());
    // 反间谍能力提升
    CHECK(st.empires[kPlayerId].counterIntel.rawValue() > 0);
    // 国库不足时失败
    st.empires[kPlayerId].treasury = Fixed(0);
    CHECK(!counterInfiltrate(st, kPlayerId, &msg));
}

// 回归守卫：网络被破获后曾**立即移除**，防御方看不到境内曾有外国网络，
// 反渗透既无从决策也无从验证。
TEST(spy, burned_networks_remain_visible_for_a_while) {
    GameState st = spyWorld(7403);
    st.empires[1].influence = Fixed(9000);
    st.empires[1].treasury = Fixed(5000000);
    std::string err;
    if (!spyEstablish(st, 1, kPlayerId, &err)) return;
    (void)spyAssignAgents(st, 1, kPlayerId, 4, nullptr);
    bool sawBurned = false;
    for (int i = 0; i < 60; ++i) {
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);
        for (const auto& net : st.empires[1].spy.networks)
            if (net.target == kPlayerId && net.burned) sawBurned = true;
        if (sawBurned) break;
    }
    CHECK(sawBurned);
}

TEST(spy, false_flag_damages_third_party_relations) {
    GameState st = spyWorld(7404);
    u32 foe = 1;
    st.empires[kPlayerId].treasury = Fixed(5000000);
    st.empires[kPlayerId].influence = Fixed(9000);
    (void)spyEstablish(st, kPlayerId, foe, nullptr);
    (void)spyAssignAgents(st, kPlayerId, foe, 5, nullptr);
    for (int i = 0; i < 40; ++i) {
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);
    }
    // 找一个第三方
    u32 third = kNoEmpire;
    for (const auto& e : st.empires)
        if (e.id != kPlayerId && e.id != foe && e.alive) {
            third = e.id;
            break;
        }
    if (third == kNoEmpire) return;
    Fixed before = st.relation(foe, third).opinion;
    int ok = 0;
    for (int i = 0; i < 15; ++i) {
        st.empires[kPlayerId].treasury = Fixed(5000000);
        for (auto& n : st.empires[kPlayerId].spy.networks)
            if (n.target == foe && !n.burned) n.infiltration = Fixed::pct(95);
        std::string err;
        if (spyRunMission(st, kPlayerId, foe, SpyMission::FalseFlag, &err)) ++ok;
    }
    if (ok > 0) CHECK(st.relation(foe, third).opinion.rawValue() <= before.rawValue());
}

TEST(spy, fog_hides_information_without_intel) {
    GameState st = spyWorld(7101);
    u32 foe = 1;
    // 没有任何情报来源时，国库与科技不可见
    CHECK(!intelKnown(st, kPlayerId, foe, IntelField::Treasury));
    CHECK(!intelKnown(st, kPlayerId, foe, IntelField::Tech));
    CHECK(intelLevel(st, kPlayerId, foe).rawValue() < Fixed::pct(40).rawValue());
    // 模糊化输出不得等于精确值
    Fixed exact(123456);
    std::string shown = intelNumber(st, kPlayerId, foe, IntelField::Treasury, exact, 0);
    CHECK(shown.find("约") != std::string::npos);
    CHECK(shown.find("123456") == std::string::npos);
    // 对自己永远完全可见
    CHECK(intelKnown(st, kPlayerId, kPlayerId, IntelField::Policies));
    CHECK_EQ(intelNumber(st, kPlayerId, kPlayerId, IntelField::Treasury, exact, 0),
             fixedStr(exact, 0));
}

TEST(spy, infiltration_reveals_information) {
    GameState st = spyWorld(7102);
    u32 foe = 1;
    (void)spyEstablish(st, kPlayerId, foe, nullptr);
    (void)spyAssignAgents(st, kPlayerId, foe, 5, nullptr);
    // 渗透需要时间：每 20 季补足特工，跑到情报门槛之上
    for (int round = 0; round < 8; ++round) {
        (void)spyAssignAgents(st, kPlayerId, foe, 5, nullptr);
        for (int t = 0; t < 25; ++t) {
            advanceOneTick(st);
            while (!st.pending.empty()) resolveChoiceAuto(st, 0);
        }
    }
    Fixed lvl = intelLevel(st, kPlayerId, foe);
    // 不断言绝对阈值：政体/种族表变化会改变生成的帝国，
    // 目标的反间谍强度随之不同。断言的是**因果关系**：
    // 渗透必须显著提高情报等级，且达到门槛后能看到精确值。
    CHECK(lvl.rawValue() > Fixed::pct(40).rawValue());
    if (intelKnown(st, kPlayerId, foe, IntelField::Treasury)) {
        Fixed exact(500000);
        CHECK_EQ(intelNumber(st, kPlayerId, foe, IntelField::Treasury, exact, 0), fixedStr(exact, 0));
    } else {
        // 未达门槛时必须给出模糊区间
        Fixed exact(500000);
        CHECK(intelNumber(st, kPlayerId, foe, IntelField::Treasury, exact, 0).find("约") !=
              std::string::npos);
    }
}

// 反间谍：目标加强反间谍后，情报等级下降、信息重新被遮蔽
TEST(spy, counter_intelligence_obscures_information) {
    GameState st = spyWorld(7103);
    u32 foe = 1;
    (void)spyEstablish(st, kPlayerId, foe, nullptr);
    (void)spyAssignAgents(st, kPlayerId, foe, 4, nullptr);
    for (int t = 0; t < 60; ++t) {
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);
    }
    Fixed before = intelLevel(st, kPlayerId, foe);
    CHECK(before.rawValue() > Fixed::pct(50).rawValue());
    st.empires[foe].counterIntel = Fixed::pct(80);
    st.empires[foe].intelDefense = Fixed::pct(90);
    Fixed after = intelLevel(st, kPlayerId, foe);
    CHECK(after.rawValue() < before.rawValue());
    CHECK(!intelKnown(st, kPlayerId, foe, IntelField::Treasury));
    // 情报等级必须有下界，不能变成负数
    CHECK(after.rawValue() >= 0);
}

// 回归守卫：反间谍若不衰减，一次投入就能永久遮蔽，迷雾再也不回来
TEST(spy, counter_intelligence_decays_over_time) {
    GameState st = spyWorld(7104);
    st.empires[1].counterIntel = Fixed::pct(90);
    Fixed start = st.empires[1].counterIntel;
    for (int t = 0; t < 150; ++t) {
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);
    }
    CHECK(st.empires[1].counterIntel.rawValue() < start.rawValue());
}

TEST(spy, intel_thresholds_are_ordered) {
    // 门槛应当分层：军力最容易看到，政策最难
    CHECK(intelThreshold(IntelField::Military).rawValue() <
          intelThreshold(IntelField::Treasury).rawValue());
    CHECK(intelThreshold(IntelField::Treasury).rawValue() <
          intelThreshold(IntelField::Tech).rawValue());
    CHECK(intelThreshold(IntelField::Tech).rawValue() <
          intelThreshold(IntelField::Policies).rawValue());
}

TEST(spy, state_survives_serialization) {
    GameState st = spyWorld(444);
    st.empires[kPlayerId].treasury = Fixed(900000);
    st.market.margin.cash = Fixed(900000);
    std::string err;
    CHECK(spyEstablish(st, kPlayerId, 1, &err));
    CHECK(spyAssignAgents(st, kPlayerId, 1, 1, &err));
    spyRunTicks(st, 10);
    CHECK(!st.empires[kPlayerId].spy.networks.empty());

    std::vector<u8> bytes = serializeState(st);
    GameState back;
    CHECK(tryDeserializeState(bytes, back));
    CHECK_EQ(back.empires[kPlayerId].spy.totalAgents, st.empires[kPlayerId].spy.totalAgents);
    CHECK_EQ(back.empires[kPlayerId].spy.agentPool, st.empires[kPlayerId].spy.agentPool);
    CHECK_EQ(back.empires[kPlayerId].spy.networks.size(), st.empires[kPlayerId].spy.networks.size());
    for (std::size_t i = 0; i < st.empires[kPlayerId].spy.networks.size(); ++i) {
        const auto& a = st.empires[kPlayerId].spy.networks[i];
        const auto& b = back.empires[kPlayerId].spy.networks[i];
        CHECK_EQ(a.target, b.target);
        CHECK_EQ(a.agents, b.agents);
        CHECK_EQ(a.infiltration.rawValue(), b.infiltration.rawValue());
        CHECK_EQ(a.exposure.rawValue(), b.exposure.rawValue());
        CHECK_EQ(a.missionsRun, b.missionsRun);
    }
}
