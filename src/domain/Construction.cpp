#include "domain/Construction.h"

#include <algorithm>
#include <string>

#include "cli/TextTable.h"
#include "core/GameState.h"
#include "domain/Building.h"
#include "domain/Empire.h"
#include "domain/Fleet.h"
#include "domain/Planet.h"
#include "gen/EmpireGen.h"
#include "gen/NameGen.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {
namespace {

/// 建筑基础工期：随 tier 递增（1 级 4 季 → 4 级 13 季）
constexpr i64 kBuildTicksBase = 1;
constexpr i64 kBuildTicksPerTier = 3;

/// 舰体基础工期：与吨位同阶（护卫舰 3 季 → 泰坦 20 季）
constexpr i64 kHullTicksBase = 2;
constexpr i64 kHullTicksPerClass = 3;

/// 取消建造的退款比例
constexpr i64 kRefundPct = 40;

}  // namespace

u32 buildingBaseTicks(int buildingIdx) {
    if (buildingIdx < 0 || buildingIdx >= kBuildingCount) return 4;
    const BuildingInfo& bi = buildingInfo(buildingIdx);
    i64 t = kBuildTicksBase + static_cast<i64>(bi.tier) * kBuildTicksPerTier;
    return static_cast<u32>(std::max<i64>(1, t));
}

u32 hullBaseTicks(int hullClass) {
    i64 t = kHullTicksBase + static_cast<i64>(hullClass) * kHullTicksPerClass;
    return static_cast<u32>(std::max<i64>(1, t));
}

Fixed buildSpeedMultiplier(const GameState& st, const Planet& p) {
    const Empire* e = st.empire(p.owner);
    if (e == nullptr) return Fixed(1);
    // 建造速率修正 + 稳定度（动荡的星球施工慢）
    Fixed mult = Fixed(1) + empireModifier(*e, ModKind::BuildRate);
    mult = mult * (Fixed::pct(60) + p.stability * Fixed::pct(60));
    return fxClamp(mult, Fixed::pct(30), Fixed(4));
}

Fixed shipyardSpeedMultiplier(const GameState& st, u32 empire) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return Fixed(1);
    Fixed mult = Fixed(1) + empireModifier(*e, ModKind::BuildRate);
    return fxClamp(mult, Fixed::pct(30), Fixed(4));
}

bool enqueueBuilding(GameState& st, u32 empire, u32 planetId, int buildingIdx, std::string* msg) {
    Empire* e = st.empire(empire);
    Planet* p = st.planet(planetId);
    if (e == nullptr || p == nullptr || p->owner != empire) {
        if (msg) *msg = "非法主体或行星不属于你";
        return false;
    }
    if (buildingIdx < 0 || buildingIdx >= kBuildingCount) {
        if (msg) *msg = "未知建筑";
        return false;
    }
    const BuildingInfo& bi = buildingInfo(buildingIdx);
    // 科技门槛
    if (bi.requireTech >= 0) {
        bool has = false;
        for (u8 t : e->tech.completed)
            if (t == static_cast<u8>(bi.requireTech)) has = true;
        if (!has) {
            if (msg) *msg = "需要先完成科技【" + std::string(techInfo(bi.requireTech).nameZh) + "】";
            return false;
        }
    }
    // 唯一建筑：已建成或已在队列中都算占用
    if (bi.unique) {
        for (u32 b : p->buildings)
            if (static_cast<int>(b & 0xFFu) == buildingIdx) {
                if (msg) *msg = "该行星已建有【" + std::string(bi.nameZh) + "】（唯一建筑）";
                return false;
            }
        for (const auto& o : p->buildQueue)
            if (static_cast<int>(o.building) == buildingIdx) {
                if (msg) *msg = "【" + std::string(bi.nameZh) + "】已在建造队列中";
                return false;
            }
    }
    // 并行限制：每个行星同时只能有 2 项在建
    if (p->buildQueue.size() >= 2) {
        if (msg) *msg = "该行星已有 2 项在建，请等待完工（`greyfall queue`）";
        return false;
    }
    // 费用
    i64 creditNeed = bi.creditCost;
    if (e->treasury.rawValue() < Fixed(creditNeed).rawValue()) {
        if (msg) *msg = "国库不足：需要 " + groupDigits(creditNeed) + " cr";
        return false;
    }
    for (int c = 0; c < kCommodityCount; ++c) {
        const i64 need = bi.cost[static_cast<std::size_t>(c)];
        if (need <= 0) continue;
        if (e->stock[static_cast<std::size_t>(c)].rawValue() < Fixed(need).rawValue()) {
            if (msg)
                *msg = std::string(commodityName(c)) + " 不足：需要 " + fixedStr(Fixed(need), 0);
            return false;
        }
    }
    e->treasury -= Fixed(creditNeed);
    for (int c = 0; c < kCommodityCount; ++c) {
        const i64 need = bi.cost[static_cast<std::size_t>(c)];
        if (need > 0) e->stock[static_cast<std::size_t>(c)] -= Fixed(need);
    }
    if (e->isPlayer) st.market.margin.cash = e->treasury;

    Planet::BuildOrder o;
    o.building = static_cast<u32>(buildingIdx);
    const u32 base = buildingBaseTicks(buildingIdx);
    Fixed speed = buildSpeedMultiplier(st, *p);
    // 速度越高工期越短（速度 1.0 为基准）
    i64 ticks = (static_cast<i64>(base) * FIX + speed.rawValue() - 1) / speed.rawValue();
    o.totalTicks = static_cast<u32>(std::max<i64>(1, ticks));
    o.ticksLeft = o.totalTicks;
    o.paidCredits = creditNeed;
    p->buildQueue.push_back(o);

    if (msg)
        *msg = "已动工：【" + std::string(bi.nameZh) + "】将于 " + std::to_string(o.totalTicks) +
               " 季后完工（建造速度 ×" + fixedStrPlain(speed, 2) + "）";
    st.logEvent(LogPhase::Economy, "build.start",
                e->name + " 在 " + p->name + " 动工建造 " + std::string(bi.nameZh), empire);
    return true;
}

bool cancelBuildOrder(GameState& st, u32 empire, u32 planetId, std::size_t index, std::string* msg) {
    Empire* e = st.empire(empire);
    Planet* p = st.planet(planetId);
    if (e == nullptr || p == nullptr || p->owner != empire) {
        if (msg) *msg = "非法主体或行星不属于你";
        return false;
    }
    if (index >= p->buildQueue.size()) {
        if (msg) *msg = "队列编号越界";
        return false;
    }
    const Planet::BuildOrder& o = p->buildQueue[index];
    const BuildingInfo& bi = buildingInfo(static_cast<int>(o.building));
    const i64 refund = o.paidCredits * kRefundPct / 100;
    e->treasury += Fixed(refund);
    if (e->isPlayer) st.market.margin.cash = e->treasury;
    std::string name(bi.nameZh);
    p->buildQueue.erase(p->buildQueue.begin() + static_cast<std::ptrdiff_t>(index));
    if (msg)
        *msg = "已取消【" + name + "】，退还 " + groupDigits(refund) + " cr（" +
               std::to_string(kRefundPct) + "%）";
    return true;
}

bool clearBuildQueue(GameState& st, u32 empire, u32 planetId, std::string* msg) {
    Empire* e = st.empire(empire);
    Planet* p = st.planet(planetId);
    if (e == nullptr || p == nullptr || p->owner != empire) {
        if (msg) *msg = "非法主体或行星不属于你";
        return false;
    }
    if (p->buildQueue.empty()) {
        if (msg) *msg = "该行星没有在建项目";
        return false;
    }
    i64 refund = 0;
    for (const auto& o : p->buildQueue) refund += o.paidCredits * kRefundPct / 100;
    e->treasury += Fixed(refund);
    if (e->isPlayer) st.market.margin.cash = e->treasury;
    const std::size_t n = p->buildQueue.size();
    p->buildQueue.clear();
    if (msg)
        *msg = "已取消 " + std::to_string(n) + " 项在建（退还 " + groupDigits(refund) + " cr）";
    return true;
}

bool hasShipyard(const GameState& st, u32 empire, u32 system) {
    const SystemNode* sys = st.system(system);
    if (sys == nullptr || sys->owner != empire) return false;
    const Empire* e = st.empire(empire);
    if (e == nullptr) return false;
    // 首都星系天然具备基础造舰能力 —— 否则「轨道船坞」被锁在高级科技后面，
    // 开局将完全无法造舰，玩家只能干等到研究完成。
    // 船坞建筑的意义是**大幅提升产能**，而不是从零解锁权限。
    if (e->capital == system) return true;
    for (u32 pid : sys->planets) {
        const Planet* p = st.planet(pid);
        if (p == nullptr || p->owner != empire) continue;
        for (u32 b : p->buildings)
            if (buildingInfo(static_cast<int>(b & 0xFFu)).effect == BuildingEffect::Shipyard)
                return true;
    }
    return false;
}

bool enqueueShip(GameState& st, u32 empire, u32 system, u32 designId, std::string* msg) {
    Empire* e = st.empire(empire);
    const SystemNode* sys = st.system(system);
    if (e == nullptr || sys == nullptr) {
        if (msg) *msg = "非法主体或星系";
        return false;
    }
    if (sys->owner != empire) {
        if (msg) *msg = "只能在自己的星系建造";
        return false;
    }
    if (!hasShipyard(st, empire, system)) {
        if (msg)
            *msg = "该星系没有造舰能力。只有**首都星系**可无条件造舰；"
                   "其他星系需要在行星上建造【轨道船坞】（`greyfall build 轨道船坞 <行星>`）";
        return false;
    }
    const FleetDesign* d = nullptr;
    for (const auto& x : e->designs)
        if (x.id == designId) d = &x;
    if (d == nullptr) {
        if (msg) *msg = "找不到该设计";
        return false;
    }
    // 费用
    const i64 creditNeed = d->creditCost;
    for (int c = 0; c < kCommodityCount; ++c) {
        const auto ci = static_cast<std::size_t>(c);
        if (e->stock[ci].rawValue() < Fixed(d->buildCost[ci]).rawValue()) {
            if (msg) *msg = std::string(commodityName(c)) + " 不足：需要 " + groupDigits(d->buildCost[ci]);
            return false;
        }
    }
    if (e->treasury.rawValue() < Fixed(creditNeed).rawValue()) {
        if (msg) *msg = "国库不足：需要 " + groupDigits(creditNeed) + " cr";
        return false;
    }
    for (int c = 0; c < kCommodityCount; ++c) {
        const auto ci = static_cast<std::size_t>(c);
        e->stock[ci] -= Fixed(d->buildCost[ci]);
    }
    e->treasury -= Fixed(creditNeed);
    if (e->isPlayer) st.market.margin.cash = e->treasury;

    Empire::ShipOrder o;
    o.system = system;
    o.design = designId;
    const u32 base = hullBaseTicks(static_cast<int>(d->hull));
    Fixed speed = shipyardSpeedMultiplier(st, empire);
    i64 ticks = (static_cast<i64>(base) * FIX + speed.rawValue() - 1) / speed.rawValue();
    o.totalTicks = static_cast<u32>(std::max<i64>(1, ticks));
    o.ticksLeft = o.totalTicks;
    o.paidCredits = creditNeed;
    e->shipQueue.push_back(o);
    if (msg)
        *msg = "已铺设龙骨：【" + d->name + "】将于 " + std::to_string(o.totalTicks) + " 季后下水（" +
               sys->name + "）";
    st.logEvent(LogPhase::Economy, "ship.start",
                e->name + " 在 " + sys->name + " 开工建造 " + d->name, empire);
    return true;
}

void constructionPhase(GameState& st) {
    for (auto& p : st.planets) {
        if (p.buildQueue.empty()) continue;
        Empire* e = st.empire(p.owner);
        if (e == nullptr || !e->alive) {
            p.buildQueue.clear();
            continue;
        }
        for (auto it = p.buildQueue.begin(); it != p.buildQueue.end();) {
            // 工期已在开工时计入速度；之后每季倒计时一次。
            constexpr u32 whole = 1;
            if (static_cast<u32>(whole) >= it->ticksLeft) {
                // 完工
                p.buildings.push_back(it->building);
                st.logEvent(LogPhase::Economy, "build.done",
                            p.name + " 完成建造 " +
                                std::string(buildingInfo(static_cast<int>(it->building)).nameZh),
                            p.owner);
                it = p.buildQueue.erase(it);
            } else {
                it->ticksLeft -= static_cast<u32>(whole);
                ++it;
            }
        }
    }

    // ---- 造舰队列 ----
    for (auto& e : st.empires) {
        if (!e.alive || e.shipQueue.empty()) continue;
        for (auto it = e.shipQueue.begin(); it != e.shipQueue.end();) {
            // 船坞若被摧毁则工期暂停（不退款，等待重建）
            if (!hasShipyard(st, e.id, it->system)) {
                ++it;
                continue;
            }
            constexpr u32 whole = 1;
            if (static_cast<u32>(whole) >= it->ticksLeft) {
                // 下水
                const FleetDesign* d = nullptr;
                for (const auto& x : e.designs)
                    if (x.id == it->design) d = &x;
                if (d != nullptr) {
                    NameGen names(st.seed ^ static_cast<u64>(st.fleets.size()) * 7919ull);
                    Fleet f;
                    f.id = static_cast<u32>(st.fleets.size());
                    f.name = names.fleet();
                    f.owner = e.id;
                    f.design = it->design;
                    f.system = it->system;
                    f.strength = d->firepower;
                    f.upkeep = 40 + d->creditCost / 200;
                    e.fleets.push_back(f.id);
                    st.fleets.push_back(std::move(f));
                    st.logEvent(LogPhase::Economy, "ship.done",
                                e.name + " 的【" + d->name + "】下水", e.id);
                }
                it = e.shipQueue.erase(it);
            } else {
                it->ticksLeft -= static_cast<u32>(whole);
                ++it;
            }
        }
    }
}

std::string buildQueueReport(const GameState& st, u32 empire) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return "非法主体\n";
    std::string out;
    TextTable t;
    t.header({"行星", "在建", "剩余", "总工期", "进度"});
    int n = 0;
    for (const auto& p : st.planets) {
        if (p.owner != empire) continue;
        for (const auto& o : p.buildQueue) {
            ++n;
            const BuildingInfo& bi = buildingInfo(static_cast<int>(o.building));
            i64 donePct = o.totalTicks > 0
                              ? (100LL * (static_cast<i64>(o.totalTicks) - static_cast<i64>(o.ticksLeft)) /
                                 static_cast<i64>(o.totalTicks))
                              : 100;
            t.row({p.name, std::string(bi.nameZh), std::to_string(o.ticksLeft) + " 季",
                   std::to_string(o.totalTicks) + " 季", std::to_string(donePct) + "%"});
        }
    }
    if (n == 0) return "  （没有在建项目）\n";
    out += t.render();
    out += "  用 `greyfall queue --cancel <行星> <序号>` 取消，或 `--clear <行星>` 清空。\n";
    return out;
}

}  // namespace gf
