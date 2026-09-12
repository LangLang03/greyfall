// 舰船设计：舰体定位、模块装配、改造
#include <algorithm>

#include "check.h"
#include "combat/Resolver.h"
#include "core/GameState.h"
#include "domain/Empire.h"
#include "domain/Fleet.h"
#include "gen/WorldGen.h"
#include "save/Serde.h"

using namespace gf;

namespace {

GameState desWorld(u64 seed = 4242, int systems = 56) {
    WorldGenOptions o;
    o.seed = seed;
    o.difficulty = 3;
    o.empireCount = 8;
    o.systemCount = systems;
    GameState st;
    generateWorld(st, o);
    return st;
}

const FleetDesign* designById(const GameState& st, u32 empire, u32 id) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return nullptr;
    for (const auto& d : e->designs)
        if (d.id == id) return &d;
    return nullptr;
}

}  // namespace

// 回归守卫：六种舰体原先只有数值差异，没有角色定位。
TEST(design, hulls_have_distinct_roles) {
    bool seen[static_cast<int>(HullRole::Count)] = {};
    for (int i = 0; i < static_cast<int>(HullClass::Count); ++i) {
        const HullInfo& hi = hullInfo(static_cast<HullClass>(i));
        seen[static_cast<int>(hi.role)] = true;
        CHECK(!hullRoleName(hi.role).empty());
        CHECK(!hullRoleDesc(hi.role).empty());
        CHECK(hi.slots > 0);
        CHECK(hi.baseFirepower.rawValue() > 0);
    }
    int kinds = 0;
    for (bool b : seen) if (b) ++kinds;
    CHECK(kinds >= 3);   // 至少三种不同定位
}

// 回归守卫：设计 id 必须全库唯一 —— 曾因 nextDesignId 从 1 开始
// 而与既有设计撞号，按 id 查找会命中错误的那一份。
TEST(design, ids_are_unique_and_creation_does_not_collide) {
    GameState st = desWorld(7001);
    Empire& me = st.empires[kPlayerId];
    std::vector<u32> before;
    for (const auto& d : me.designs) before.push_back(d.id);
    std::string err;
    for (int i = 0; i < 5; ++i) {
        u32 id = designCreate(me, HullClass::Destroyer, "测试型" + std::to_string(i), &err);
        CHECK(id != 0xFFFFFFFFu);
        // 新 id 不得与任何既有 id 重复
        for (u32 old : before) CHECK(id != old);
    }
    // 全库 id 唯一
    std::vector<u32> all;
    for (const auto& d : me.designs) all.push_back(d.id);
    std::sort(all.begin(), all.end());
    CHECK(std::adjacent_find(all.begin(), all.end()) == all.end());
    // 重名被拒
    CHECK(designCreate(me, HullClass::Destroyer, "测试型0", &err) == 0xFFFFFFFFu);
}

TEST(design, module_install_respects_slots_and_hull) {
    GameState st = desWorld(7002);
    Empire& me = st.empires[kPlayerId];
    std::string err;
    u32 id = designCreate(me, HullClass::Corvette, "槽位测试", &err);
    if (id == 0xFFFFFFFFu) return;
    const int slots = hullInfo(HullClass::Corvette).slots;
    for (int i = 0; i < slots; ++i) {
        // 找该舰体允许的最低级模块
        int pick = -1;
        for (int m = 0; m < kModuleCount; ++m)
            if (static_cast<int>(moduleInfo(m).minHull) <= static_cast<int>(HullClass::Corvette)) {
                pick = m;
                break;
            }
        if (pick < 0) return;
        CHECK(designInstallModule(me, id, pick, &err));
    }
    // 超出槽位必须被拒
    int any = 0;
    for (int m = 0; m < kModuleCount; ++m)
        if (static_cast<int>(moduleInfo(m).minHull) <= static_cast<int>(HullClass::Corvette)) {
            any = m;
            break;
        }
    CHECK(!designInstallModule(me, id, any, &err));
    CHECK(!err.empty());
    // 卸下与清空
    CHECK(designRemoveModule(me, id, 0, &err));
    CHECK(designRemoveModule(me, id, 99, &err) == false);
    CHECK(designClearModules(me, id, &err));
    const FleetDesign* d = designById(st, kPlayerId, id);
    CHECK(d != nullptr);
    CHECK_EQ(d->modules.size(), 0u);
    // 清空后属性必须回落到舰体基线
    CHECK_EQ(d->firepower.rawValue(), hullInfo(HullClass::Corvette).baseFirepower.rawValue());
}

TEST(design, module_install_enforces_hull_requirement) {
    GameState st = desWorld(7003);
    Empire& me = st.empires[kPlayerId];
    std::string err;
    u32 id = designCreate(me, HullClass::Corvette, "限制测试", &err);
    if (id == 0xFFFFFFFFu) return;
    // 找需要更高舰体的模块
    int high = -1;
    for (int m = 0; m < kModuleCount; ++m)
        if (static_cast<int>(moduleInfo(m).minHull) > static_cast<int>(HullClass::Corvette)) {
            high = m;
            break;
        }
    if (high < 0) return;
    CHECK(!designInstallModule(me, id, high, &err));
    CHECK(!err.empty());
}

// 回归守卫：设计原先只影响组织度上限，**火力与防御完全不参与战斗** ——
// 装配模块、换装舰体、改造舰队都不影响胜负，整套系统只是装饰。
TEST(design, modules_actually_change_combat_power) {
    GameState st = desWorld(7004);
    Empire& me = st.empires[kPlayerId];
    if (me.fleets.empty()) return;
    u32 fid = me.fleets.front();
    const Fleet* f = st.fleet(fid);
    if (f == nullptr) return;
    u32 did = f->design;
    Fixed base = fleetPower(st, fid);
    CHECK(base.rawValue() > 0);

    std::string err;
    const FleetDesign* d = designById(st, kPlayerId, did);
    if (d == nullptr) return;
    const int slots = hullInfo(d->hull).slots;

    // 清空模块 → 战力必须下降
    CHECK(designClearModules(me, did, &err));
    Fixed bare = fleetPower(st, fid);
    CHECK(bare.rawValue() < base.rawValue());

    // 装满武器类模块 → 战力必须高于裸舰
    for (int i = 0; i < slots; ++i) {
        int pick = -1;
        for (int m = 0; m < kModuleCount; ++m) {
            if (static_cast<int>(moduleInfo(m).minHull) > static_cast<int>(d->hull)) continue;
            if (moduleInfo(m).effect != ModuleEffect::Firepower) continue;
            pick = m;
            break;
        }
        if (pick < 0) break;
        (void)designInstallModule(me, did, pick, &err);
    }
    Fixed armed = fleetPower(st, fid);
    CHECK(armed.rawValue() > bare.rawValue());
}

TEST(design, refit_requires_own_system_and_pays_cost) {
    GameState st = desWorld(7005);
    Empire& me = st.empires[kPlayerId];
    if (me.fleets.empty()) return;
    u32 fid = me.fleets.front();
    std::string err;
    u32 newId = designCreate(me, HullClass::Cruiser, "改造目标", &err);
    if (newId == 0xFFFFFFFFu) return;
    // 把舰队放到中立星系：应当被拒
    Fleet* f = st.fleet(fid);
    if (f == nullptr) return;
    u32 orig = f->system;
    const SystemNode* origNode = st.system(orig);
    if (origNode == nullptr) return;
    // 找一个不属于玩家的星系
    for (const auto& s : st.map.systems) {
        if (s.owner == kPlayerId) continue;
        f->system = s.id;
        break;
    }
    me.stock[static_cast<std::size_t>(Commodity::Alloys)] = Fixed(100000);
    me.treasury = Fixed(1000000);
    CHECK(!fleetRefit(st, kPlayerId, fid, newId, &err));
    CHECK(!err.empty());
    // 放回自家星系：应当成功并扣费
    f->system = orig;
    Fixed alloyBefore = me.stock[static_cast<std::size_t>(Commodity::Alloys)];
    CHECK(fleetRefit(st, kPlayerId, fid, newId, &err));
    CHECK_EQ(f->design, newId);
    CHECK(me.stock[static_cast<std::size_t>(Commodity::Alloys)].rawValue() < alloyBefore.rawValue());
    CHECK_EQ(f->org.rawValue(), 0);   // 改造后需重整训
    // 重复改造到同一设计被拒
    CHECK(!fleetRefit(st, kPlayerId, fid, newId, &err));
}

TEST(design, state_survives_serialization) {
    GameState st = desWorld(7006);
    Empire& me = st.empires[kPlayerId];
    std::string err;
    u32 id = designCreate(me, HullClass::Destroyer, "存档设计", &err);
    if (id == 0xFFFFFFFFu) return;
    (void)designInstallModule(me, id, 0, &err);

    std::vector<u8> bytes = serializeState(st);
    GameState back;
    CHECK(tryDeserializeState(bytes, back));
    CHECK_EQ(back.empires[kPlayerId].nextDesignId, st.empires[kPlayerId].nextDesignId);
    const FleetDesign* b = designById(back, kPlayerId, id);
    CHECK(b != nullptr);
    CHECK(b->name == "存档设计");
    CHECK_EQ(b->modules.size(), designById(st, kPlayerId, id)->modules.size());
    CHECK_EQ(b->firepower.rawValue(), designById(st, kPlayerId, id)->firepower.rawValue());
}
