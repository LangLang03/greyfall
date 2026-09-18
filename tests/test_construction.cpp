// 建造工期、开局随机化、新政体、腐败
#include <algorithm>
#include <set>

#include "check.h"
#include "core/GameState.h"
#include "core/TickPipeline.h"
#include "domain/Construction.h"
#include "domain/Corruption.h"
#include "domain/Empire.h"
#include "domain/Planet.h"
#include "domain/Species.h"
#include "gen/EmpireGen.h"
#include "gen/WorldGen.h"
#include "plot/EventSystem.h"
#include "plot/BeatResolver.h"
#include "save/Serde.h"

using namespace gf;

namespace {

GameState conWorld(u64 seed = 4242, int systems = 56) {
    WorldGenOptions o;
    o.seed = seed;
    o.difficulty = 3;
    o.empireCount = 8;
    o.systemCount = systems;
    GameState st;
    generateWorld(st, o);
    return st;
}

void conTicks(GameState& st, int n) {
    for (int i = 0; i < n; ++i) {
        advanceOneTick(st);
        while (!st.pending.empty()) resolveChoiceAuto(st, 0);
    }
}

u32 firstOwnPlanet(const GameState& st, u32 empire) {
    for (const auto& p : st.planets)
        if (p.owner == empire) return p.id;
    return kNoSystem;
}

int firstTier1Building() {
    for (int b = 0; b < kBuildingCount; ++b) {
        const BuildingInfo& bi = buildingInfo(b);
        if (bi.tier == 1 && bi.requireTech < 0) return b;
    }
    return -1;
}

}  // namespace

// 回归守卫：build 原先**即时完成** —— 一次调用就把建筑塞进结果里。
// 建造速率与产能加成因此毫无意义，玩家可在同一季瞬间铺满所有行星。
TEST(construction, buildings_take_time) {
    GameState st = conWorld(7701);
    Empire& me = st.empires[kPlayerId];
    me.treasury = Fixed(2000000);
    u32 pid = firstOwnPlanet(st, kPlayerId);
    if (pid == kNoSystem) return;
    int b = firstTier1Building();
    if (b < 0) return;
    Planet* p = st.planet(pid);
    if (p == nullptr) return;
    const std::size_t before = p->buildings.size();

    std::string msg;
    CHECK(enqueueBuilding(st, kPlayerId, pid, b, &msg));
    // 关键：入队后**不能**立刻出现在 buildings 里
    CHECK_EQ(st.planet(pid)->buildings.size(), before);
    CHECK(!st.planet(pid)->buildQueue.empty());
    CHECK(!msg.empty());

    // 必须经过若干季才完工
    const u32 total = st.planet(pid)->buildQueue.front().totalTicks;
    CHECK(total >= 1);
    int done = -1;
    for (int t = 1; t <= static_cast<int>(total) + 6; ++t) {
        conTicks(st, 1);
        if (st.planet(pid)->buildQueue.empty()) {
            done = t;
            break;
        }
    }
    CHECK(done > 0);
    CHECK(st.planet(pid)->buildings.size() > before);
}

TEST(construction, build_queue_can_be_cancelled_with_partial_refund) {
    GameState st = conWorld(7702);
    Empire& me = st.empires[kPlayerId];
    me.treasury = Fixed(2000000);
    u32 pid = firstOwnPlanet(st, kPlayerId);
    if (pid == kNoSystem) return;
    int b = firstTier1Building();
    if (b < 0) return;
    std::string msg;
    CHECK(enqueueBuilding(st, kPlayerId, pid, b, &msg));
    Fixed cash = me.treasury;
    CHECK(cancelBuildOrder(st, kPlayerId, pid, 0, &msg));
    CHECK(st.planet(pid)->buildQueue.empty());
    CHECK(me.treasury.rawValue() > cash.rawValue());   // 退款
    // 队列为空后取消失败
    CHECK(!cancelBuildOrder(st, kPlayerId, pid, 0, &msg));
}

TEST(construction, parallel_limit_is_enforced) {
    GameState st = conWorld(7703);
    Empire& me = st.empires[kPlayerId];
    me.treasury = Fixed(5000000);
    u32 pid = firstOwnPlanet(st, kPlayerId);
    if (pid == kNoSystem) return;
    int b = firstTier1Building();
    if (b < 0) return;
    std::string msg;
    int queued = 0;
    // 同一建筑不可重复（unique 除外），这里换不同建筑试探并行上限
    for (int x = 0; x < kBuildingCount && queued < 3; ++x) {
        if (buildingInfo(x).tier != 1 || buildingInfo(x).requireTech >= 0) continue;
        if (enqueueBuilding(st, kPlayerId, pid, x, &msg)) ++queued;
    }
    // 最多 2 项并行
    CHECK(st.planet(pid)->buildQueue.size() <= 2);
    CHECK(queued >= 1);
}

// 回归守卫：ship-build 原先即时成军，且不要求船坞。
TEST(construction, ships_require_shipyard_and_time) {
    GameState st = conWorld(7704);
    Empire& me = st.empires[kPlayerId];
    me.treasury = Fixed(5000000);
    me.stock[static_cast<std::size_t>(Commodity::Alloys)] = Fixed(50000);
    if (me.designs.empty() || me.systems.empty()) return;
    const u32 cap = me.capital;
    // 首都天然具备造舰能力
    CHECK(hasShipyard(st, kPlayerId, cap));
    // 非首都且无船坞的星系不能造舰
    u32 other = kNoSystem;
    for (u32 s : me.systems)
        if (s != cap) {
            other = s;
            break;
        }
    if (other != kNoSystem) CHECK(!hasShipyard(st, kPlayerId, other));

    const std::size_t fleets0 = me.fleets.size();
    std::string msg;
    CHECK(enqueueShip(st, kPlayerId, cap, me.designs.front().id, &msg));
    CHECK_EQ(me.fleets.size(), fleets0);   // 不能立刻成军
    CHECK(!me.shipQueue.empty());
    // 跑够工期后下水
    for (int t = 0; t < 60 && !me.shipQueue.empty(); ++t) conTicks(st, 1);
    CHECK(me.shipQueue.empty());
    CHECK(st.empires[kPlayerId].fleets.size() > fleets0);
}

// 回归守卫：玩家国名曾恒为「灰域联合体」，任何种子开局都是同一个国家。
TEST(construction, starting_nation_is_randomized) {
    std::set<std::string> names;
    std::set<int> govs;
    for (u64 seed : {11ull, 22ull, 33ull, 44ull, 55ull}) {
        GameState st = conWorld(seed);
        names.insert(st.empires[kPlayerId].name);
        govs.insert(static_cast<int>(st.empires[kPlayerId].government));
    }
    CHECK(names.size() >= 3);   // 国名必须有变化
    CHECK(govs.size() >= 2);    // 政体也应有变化
}

TEST(construction, new_governments_exist_with_distinct_profiles) {
    CHECK(kGovernmentCount >= 18);
    // 找到四个新政体
    auto findGov = [](const char* idName) {
        for (int i = 0; i < kGovernmentCount; ++i)
            if (governmentInfo(i).idName == idName) return i;
        return -1;
    };
    const int soc = findGov("socialist");
    const int fas = findGov("fascist");
    const int cap = findGov("capitalist");
    const int par = findGov("parliamentary");
    CHECK(soc >= 0);
    CHECK(fas >= 0);
    CHECK(cap >= 0);
    CHECK(par >= 0);
    if (soc < 0 || fas < 0 || cap < 0 || par < 0) return;
    // 社会主义：工厂效率最高、民生开销最重
    CHECK(governmentInfo(soc).buildEfficiency.rawValue() >
          governmentInfo(cap).buildEfficiency.rawValue());
    CHECK(governmentInfo(soc).upkeepBias.rawValue() > 0);
    // 法西斯：正当化速度最高
    CHECK(governmentInfo(fas).justificationBonus.rawValue() >
          governmentInfo(par).justificationBonus.rawValue());
    // 民主主义：关系改善最高、腐败倾向最低
    CHECK(governmentInfo(par).opinionGain.rawValue() >
          governmentInfo(fas).opinionGain.rawValue());
    CHECK(governmentInfo(par).corruptionBias.rawValue() < 0);
    // 资本主义：腐败倾向最高
    CHECK(governmentInfo(cap).corruptionBias.rawValue() >
          governmentInfo(soc).corruptionBias.rawValue());
}

// 回归守卫：腐败系统原先完全不存在 —— 疆域扩张没有任何规模代价。
TEST(construction, corruption_grows_with_scale_and_government) {
    std::array<Fixed, 3> c{};
    const int govs[3] = {14, 16, 17};   // 社会主义 / 资本主义 / 民主主义
    for (int i = 0; i < 3; ++i) {
        GameState st = conWorld(7705);
        st.empires[kPlayerId].government = static_cast<u8>(govs[i]);
        conTicks(st, 60);
        c[static_cast<std::size_t>(i)] = corruptionOf(st, kPlayerId);
    }
    // 资本主义必须比民主主义腐败得多
    CHECK(c[1].rawValue() > c[2].rawValue());
    // 腐败必须有上界
    CHECK(c[1].rawValue() <= Fixed(1).rawValue());
    CHECK(c[0].rawValue() >= 0);
}

TEST(construction, corruption_is_bounded_and_purgeable) {
    GameState st = conWorld(7706);
    // 注意：GameState 的 empires 是 std::vector，`generation`/tick 阶段可能让它扩容。
    // 这里曾经把 `Empire& me` 一直持有到 conTicks 之后 —— 那是**悬垂引用**，
    // 写 me.treasury 实际写进了已释放的内存，断言随机通过/失败。
    // 必须先设政体、跑完 tick，再重新取引用。
    st.empires[kPlayerId].government = 16;   // 资本主义
    conTicks(st, 80);
    Fixed before = corruptionOf(st, kPlayerId);
    if (before.rawValue() <= 0) return;
    // 腐败侵蚀收入：必须为非负且不超过 100%
    CHECK(corruptionIncomeLoss(st, kPlayerId).rawValue() >= 0);
    CHECK(corruptionIncomeLoss(st, kPlayerId).rawValue() <= Fixed(1).rawValue());
    // 反腐（此处重新取引用，见上）
    st.empires[kPlayerId].treasury = Fixed(500000);
    std::string msg;
    CHECK(antiCorruption(st, kPlayerId, &msg));
    CHECK(corruptionOf(st, kPlayerId).rawValue() < before.rawValue());
    // 国库不足时失败
    st.empires[kPlayerId].treasury = Fixed(0);
    CHECK(!antiCorruption(st, kPlayerId, &msg));
}

TEST(construction, state_survives_serialization) {
    GameState st = conWorld(7707);
    Empire& me = st.empires[kPlayerId];
    me.treasury = Fixed(5000000);
    me.stock[static_cast<std::size_t>(Commodity::Alloys)] = Fixed(50000);
    u32 pid = firstOwnPlanet(st, kPlayerId);
    if (pid == kNoSystem) return;
    int b = firstTier1Building();
    std::string msg;
    if (b >= 0) (void)enqueueBuilding(st, kPlayerId, pid, b, &msg);
    if (!me.designs.empty() && hasShipyard(st, kPlayerId, me.capital))
        (void)enqueueShip(st, kPlayerId, me.capital, me.designs.front().id, &msg);
    conTicks(st, 3);

    std::vector<u8> bytes = serializeState(st);
    GameState back;
    CHECK(tryDeserializeState(bytes, back));
    CHECK_EQ(back.stateHash(), st.stateHash());
    // 队列内容必须一致
    CHECK_EQ(back.planet(pid)->buildQueue.size(), st.planet(pid)->buildQueue.size());
    CHECK_EQ(back.empires[kPlayerId].shipQueue.size(), st.empires[kPlayerId].shipQueue.size());
    CHECK_EQ(back.empires[kPlayerId].corruption.rawValue(),
             st.empires[kPlayerId].corruption.rawValue());
    // 续跑必须完全一致
    conTicks(st, 10);
    conTicks(back, 10);
    CHECK_EQ(st.stateHash(), back.stateHash());
}
