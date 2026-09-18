// 前线系统：扇区识别 / 战力比 / 兵力分配 / 战略预备队
#include <algorithm>

#include "check.h"
#include "combat/Front.h"
#include "combat/Resolver.h"
#include "core/GameState.h"
#include "core/TickPipeline.h"
#include "domain/Treaty.h"
#include "gen/WorldGen.h"
#include "plot/EventSystem.h"
#include "plot/BeatResolver.h"

using namespace gf;

namespace {

GameState frontWorld(u64 seed = 8080) {
    WorldGenOptions o;
    o.seed = seed;
    o.difficulty = 2;
    o.empireCount = 8;
    o.systemCount = 48;
    GameState st;
    generateWorld(st, o);
    return st;
}

/// 把两个帝国的舰队放进同一星系并宣战
u32 contact(GameState& st, u32 a, u32 b) {
    declareWar(st, a, b, true);
    u32 sys = st.empires[b].systems.empty() ? st.empires[b].capital : st.empires[b].systems[0];
    for (u32 id : st.empire(a)->fleets) {
        Fleet* f = st.fleet(id);
        if (f != nullptr) {
            f->system = sys;
            f->targetSystem = kNoSystem;
            f->order = FleetOrder::Patrol;
        }
    }
    for (u32 id : st.empire(b)->fleets) {
        Fleet* f = st.fleet(id);
        if (f != nullptr) {
            f->system = sys;
            f->targetSystem = kNoSystem;
            f->order = FleetOrder::Patrol;
        }
    }
    return sys;
}

}  // namespace

TEST(front, no_fronts_when_not_at_war) {
    GameState st = frontWorld();
    auto fronts = allFronts(st, kPlayerId);
    CHECK(fronts.empty());
    CHECK(frontsText(st, kPlayerId).find("并未处于战争状态") != std::string::npos);
}

TEST(front, contact_sector_detected_when_fleets_meet) {
    GameState st = frontWorld(11);
    u32 sys = contact(st, 2, 1);
    Front f = computeFront(st, 2, 1);
    CHECK_EQ(f.enemy, 1u);
    CHECK(!f.sectors.empty());
    // 必须包含接触星系
    bool found = false;
    for (const auto& s : f.sectors)
        if (s.system == sys) found = true;
    CHECK(found);
    // 战力必须为正且能算出比值
    CHECK(f.friendlyTotal.rawValue() > 0);
    CHECK(f.enemyTotal.rawValue() > 0);
    CHECK(f.overallRatio.rawValue() > 0);
    CHECK(!f.posture.empty());
    CHECK(!f.enemyName.empty());
}

TEST(front, border_sector_detected_without_contact) {
    GameState st = frontWorld(22);
    declareWar(st, 2, 1, true);
    // 不移动舰队：只要两国接壤，就应识别出边界扇区
    Front f = computeFront(st, 2, 1);
    // 接壤与否取决于星图，若接壤则扇区非空
    for (const auto& s : f.sectors) {
        const SystemNode* sys = st.system(s.system);
        CHECK(sys != nullptr);
        CHECK_EQ(sys->owner, 1u);          // 扇区目标必须属于敌方
        CHECK(!s.terrain.empty());
    }
}

TEST(front, recommended_sector_is_best_ratio) {
    GameState st = frontWorld(33);
    u32 sys = contact(st, 2, 1);
    // 让接触扇区成为压倒性优势
    for (u32 id : st.empire(2)->fleets) {
        Fleet* f = st.fleet(id);
        if (f != nullptr) f->strength = f->strength * Fixed(10);
    }
    Front f = computeFront(st, 2, 1);
    CHECK_EQ(f.recommendedSector, sys);
    CHECK(f.posture.find("优势") != std::string::npos);
}

TEST(front, posture_reflects_power_balance) {
    GameState st = frontWorld(44);
    contact(st, 2, 1);
    // 削弱进攻方 ⇒ 劣势姿态
    for (u32 id : st.empire(2)->fleets) {
        Fleet* f = st.fleet(id);
        if (f != nullptr) f->strength = f->strength / Fixed(20);
    }
    Front weak = computeFront(st, 2, 1);
    CHECK(weak.overallRatio.rawValue() < Fixed(1).rawValue());
    CHECK(weak.posture.find("劣势") != std::string::npos);
    // 强化 ⇒ 优势姿态
    for (u32 id : st.empire(2)->fleets) {
        Fleet* f = st.fleet(id);
        if (f != nullptr) f->strength = f->strength * Fixed(200);
    }
    Front strong = computeFront(st, 2, 1);
    CHECK(strong.overallRatio.rawValue() > weak.overallRatio.rawValue());
    CHECK(strong.posture.find("优势") != std::string::npos);
}

TEST(front, assign_deploys_idle_fleets_to_front) {
    GameState st = frontWorld(55);
    contact(st, 2, 1);
    // 把所有舰队设为待命
    for (auto& f : st.fleets) {
        f.order = FleetOrder::Idle;
        f.targetSystem = kNoSystem;
        f.battle = 0xFFFFFFFFu;
    }
    assignFronts(st);
    // 进攻方应有舰队被派出（移动或交战）
    int deployed = 0;
    for (const auto& f : st.fleets) {
        if (f.owner != 2) continue;
        if (f.order == FleetOrder::Move || f.order == FleetOrder::Engage) ++deployed;
    }
    CHECK(deployed > 0);
    // 非交战方不应被派往前线
    int peaceful = 0;
    for (const auto& f : st.fleets) {
        if (f.owner == 2 || f.owner == 1) continue;
        if (f.order == FleetOrder::Move || f.order == FleetOrder::Engage) ++peaceful;
    }
    CHECK_EQ(peaceful, 0);
}

TEST(front, disadvantaged_side_keeps_reserves) {
    GameState st = frontWorld(66);
    contact(st, 2, 1);
    // 让进攻方处于明显劣势
    for (u32 id : st.empire(2)->fleets) {
        Fleet* f = st.fleet(id);
        if (f != nullptr) f->strength = f->strength / Fixed(10);
    }
    for (u32 id : st.empire(1)->fleets) {
        Fleet* f = st.fleet(id);
        if (f != nullptr) f->strength = f->strength * Fixed(10);
    }
    // 给进攻方更多舰队以便观察「保留预备队」
    for (int i = 0; i < 8; ++i) {
        Fleet f;
        f.id = static_cast<u32>(st.fleets.size());
        f.owner = 2;
        f.system = st.empires[2].systems.empty() ? st.empires[2].capital : st.empires[2].systems[0];
        f.strength = Fixed(100);
        f.org = Fixed(100);
        f.maxOrg = Fixed(100);
        f.order = FleetOrder::Idle;
        st.empires[2].fleets.push_back(f.id);
        st.fleets.push_back(f);
    }
    assignFronts(st);
    int deployed = 0, idleKept = 0;
    for (const auto& f : st.fleets) {
        if (f.owner != 2) continue;
        if (f.order == FleetOrder::Move || f.order == FleetOrder::Engage) ++deployed;
        else if (f.order == FleetOrder::Idle) ++idleKept;
    }
    // 劣势时不应把全部舰队投入
    CHECK(idleKept > 0);
    CHECK(deployed > 0);
}

TEST(front, no_deployment_without_war) {
    GameState st = frontWorld(77);
    for (auto& f : st.fleets) {
        f.order = FleetOrder::Idle;
        f.targetSystem = kNoSystem;
    }
    assignFronts(st);
    int deployed = 0;
    for (const auto& f : st.fleets)
        if (f.order == FleetOrder::Move || f.order == FleetOrder::Engage) ++deployed;
    CHECK_EQ(deployed, 0);
}

TEST(front, front_text_renders) {
    GameState st = frontWorld(88);
    contact(st, 2, 1);
    std::string txt = frontsText(st, 2);
    CHECK(!txt.empty());
    CHECK(txt.find("前线") != std::string::npos);
    CHECK(txt.find("战力比") != std::string::npos);
    // 详情文本
    std::string detail = frontDetailText(st, 2, 1);
    CHECK(!detail.empty());
    // 非交战国应给出明确提示
    std::string noWar = frontDetailText(st, 2, 5);
    CHECK(noWar.find("并未处于战争状态") != std::string::npos);
    // 非法主体不应崩溃
    CHECK(!frontDetailText(st, 2, 9999).empty());
    CHECK(!frontsText(st, 9999).empty());
}

TEST(front, sections_are_deduplicated_per_system) {
    GameState st = frontWorld(99);
    contact(st, 2, 1);
    Front f = computeFront(st, 2, 1);
    std::vector<u32> sysIds;
    for (const auto& s : f.sectors) sysIds.push_back(s.system);
    std::sort(sysIds.begin(), sysIds.end());
    // 同一星系不得出现两次
    CHECK(std::adjacent_find(sysIds.begin(), sysIds.end()) == sysIds.end());
}

// 回归守卫：多路推进曾只是姿态提示文字，实际分配永远堆在单点。
// 现在优势足够时必须真的分兵多路，且轴数上限与姿态文字一致。
TEST(front, multi_axis_requires_decisive_advantage) {
    GameState st = frontWorld(3001);
    declareWar(st, 2, 1, true);
    // 把攻方舰队散布到敌方多个星系，制造多扇区战线
    std::vector<u32> targets = st.empires[1].systems;
    if (targets.empty()) return;
    int k = 0;
    for (u32 id : st.empire(2)->fleets) {
        Fleet* f = st.fleet(id);
        if (f == nullptr) continue;
        f->system = targets[static_cast<std::size_t>(k) % targets.size()];
        f->order = FleetOrder::Patrol;
        f->targetSystem = kNoSystem;
        ++k;
    }
    // 补足舰队数量以便观察多路
    while (st.empire(2)->fleets.size() < 6) {
        Fleet nf;
        nf.id = static_cast<u32>(st.fleets.size());
        nf.owner = 2;
        nf.name = "增援";
        nf.system = targets[st.empire(2)->fleets.size() % targets.size()];
        nf.strength = Fixed(400);
        nf.org = Fixed(100);
        nf.maxOrg = Fixed(100);
        nf.order = FleetOrder::Patrol;
        st.empire(2)->fleets.push_back(nf.id);
        st.fleets.push_back(nf);
    }
    // 均势时：单轴
    Front weak = computeFront(st, 2, 1);
    CHECK(!weak.multiAxis);
    CHECK_EQ(weak.axisCount, 1);
    // 压倒性优势时：多轴
    for (u32 id : st.empire(2)->fleets) {
        Fleet* f = st.fleet(id);
        if (f != nullptr) f->strength = f->strength * Fixed(30);
    }
    Front strong = computeFront(st, 2, 1);
    if (strong.sectors.size() >= 2) {
        CHECK(strong.overallRatio.rawValue() >= Fixed(2).rawValue());
        CHECK(strong.multiAxis);
        CHECK(strong.axisCount >= 2);
        // 每条轴的目标必须真实存在于扇区列表
        for (const auto& ax : strong.axes) {
            bool found = false;
            for (const auto& s : strong.sectors)
                if (s.system == ax.system) found = true;
            CHECK(found);
            CHECK(ax.required.rawValue() > 0);
        }
    }
}

// 回归守卫：轴数上限必须与姿态文字一致 ——
// 曾出现「文字说建议单点突破、规划却选了 2 条轴」的自相矛盾。
TEST(front, axis_count_matches_posture_text) {
    GameState st = frontWorld(3002);
    declareWar(st, 2, 1, true);
    std::vector<u32> targets = st.empires[1].systems;
    if (targets.empty()) return;
    int k = 0;
    for (u32 id : st.empire(2)->fleets) {
        Fleet* f = st.fleet(id);
        if (f == nullptr) continue;
        f->system = targets[static_cast<std::size_t>(k) % targets.size()];
        f->order = FleetOrder::Patrol;
        f->targetSystem = kNoSystem;
        ++k;
    }
    while (st.empire(2)->fleets.size() < 6) {
        Fleet nf;
        nf.id = static_cast<u32>(st.fleets.size());
        nf.owner = 2;
        nf.name = "增援";
        nf.system = targets[st.empire(2)->fleets.size() % targets.size()];
        nf.strength = Fixed(400);
        nf.org = Fixed(100);
        nf.maxOrg = Fixed(100);
        nf.order = FleetOrder::Patrol;
        st.empire(2)->fleets.push_back(nf.id);
        st.fleets.push_back(nf);
    }
    // 依次放大兵力，检查「姿态文字」与「是否多路」始终一致
    for (int step = 0; step < 6; ++step) {
        Front fr = computeFront(st, 2, 1);
        bool textSaysMulti = fr.posture.find("可多路推进") != std::string::npos;
        // 文字说可多路 ⇒ 规划必须分兵；文字说单点/固守 ⇒ 不得分兵
        if (textSaysMulti) {
            if (fr.sectors.size() >= 2) CHECK(fr.multiAxis);
        } else {
            CHECK(!fr.multiAxis);
        }
        // 放大兵力进入下一档
        for (u32 id : st.empire(2)->fleets) {
            Fleet* f = st.fleet(id);
            if (f != nullptr) f->strength = f->strength * Fixed(2);
        }
    }
}

TEST(front, phase_integration_deploys_over_time) {
    GameState st = frontWorld(111);
    contact(st, 2, 1);
    // 跑若干 tick，前线分配应当在流水线中生效。
    // 注意：AI 现在会主动出兵，战争可能在数 tick 内通过和平会议结束，
    // 因此不能断言「某 tick 仍存在战斗」，而应断言「部署/交战确实发生过」。
    bool deployed = false;
    bool fought = false;
    for (int i = 0; i < 8; ++i) {
        if (!atWarWith(st, 2, 1)) declareWar(st, 2, 1, true);   // 保持战争状态
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);
        if (!st.battles.empty()) fought = true;
        for (const auto& f : st.fleets) {
            if (f.owner != 2) continue;
            if (f.order == FleetOrder::Move || f.order == FleetOrder::Engage) deployed = true;
        }
    }
    CHECK(fought);
    CHECK(deployed);
}
