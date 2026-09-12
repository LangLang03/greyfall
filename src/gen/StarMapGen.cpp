#include "gen/StarMapGen.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "gen/NameGen.h"
#include "rng/Streams.h"

namespace gf {
namespace {

i64 dist2(const SystemNode& a, const SystemNode& b) {
    i64 dx = static_cast<i64>(a.x) - b.x;
    i64 dy = static_cast<i64>(a.y) - b.y;
    return dx * dx + dy * dy;
}

}  // namespace

void generateStarMap(GameState& st, const StarMapOptions& opts) {
    RngBus& rng = st.rng;
    NameGen names(st.seed ^ 0xA11CEull);

    const int sectors = opts.sectorCount < 3 ? 3 : opts.sectorCount;
    const int systems = opts.systemCount < 16 ? 16 : opts.systemCount;

    st.map.systems.clear();
    st.map.sectors.clear();
    st.map.systems.reserve(static_cast<std::size_t>(systems));

    // 星区中心：网格抖动 + 一轮 Lloyd 松弛
    std::vector<std::pair<i32, i32>> centers;
    int side = 1;
    while (side * side < sectors) ++side;
    for (int i = 0; i < sectors; ++i) {
        int cx = i % side;
        int cy = i / side;
        i32 x = static_cast<i32>(-300 + (600 * (2 * cx + 1)) / (2 * side) + static_cast<int>(rng.range(RngStream::World, -60, 60)));
        i32 y = static_cast<i32>(-300 + (600 * (2 * cy + 1)) / (2 * side) + static_cast<int>(rng.range(RngStream::World, -60, 60)));
        centers.emplace_back(x, y);
    }

    // 系统：围绕中心的高斯撒点
    for (int i = 0; i < systems; ++i) {
        std::size_t c = rng.pick(RngStream::World, centers.size());
        i32 x = 0, y = 0;
        bool ok = false;
        for (int attempt = 0; attempt < 24 && !ok; ++attempt) {
            Fixed nx = rng.normal(RngStream::World);
            Fixed ny = rng.normal(RngStream::World);
            i32 sx = centers[c].first + static_cast<i32>((nx.rawValue() * 46) / FIX);
            i32 sy = centers[c].second + static_cast<i32>((ny.rawValue() * 46) / FIX);
            if (sx < -330) sx = -330;
            if (sx > 330) sx = 330;
            if (sy < -330) sy = -330;
            if (sy > 330) sy = 330;
            x = sx;
            y = sy;
            ok = true;
            for (const auto& s : st.map.systems) {
                i64 dx = s.x - x, dy = s.y - y;
                if (dx * dx + dy * dy < 26 * 26) {
                    ok = false;
                    break;
                }
            }
        }
        SystemNode node;
        node.id = static_cast<u32>(i);
        node.name = names.system() + "-" + std::to_string(i + 1);
        node.x = x;
        node.y = y;
        node.sector = static_cast<u32>(c);
        node.hazard = Fixed::raw(static_cast<i64>(rng.range(RngStream::World, 0, 180)));
        node.pirates = Fixed::raw(static_cast<i64>(rng.range(RngStream::World, 0, 220)));
        node.tradeHub = Fixed::raw(static_cast<i64>(rng.range(RngStream::World, 0, 400)));
        st.map.systems.push_back(std::move(node));
    }

    // 航线：MST（Prim）+ 额外环边
    const std::size_t n = st.map.systems.size();
    std::vector<bool> inTree(n, false);
    std::vector<i64> best(n, std::numeric_limits<i64>::max());
    std::vector<int> parent(n, -1);
    inTree[0] = true;
    for (std::size_t i = 1; i < n; ++i) best[i] = dist2(st.map.systems[0], st.map.systems[i]);
    for (std::size_t iter = 1; iter < n; ++iter) {
        i64 bd = std::numeric_limits<i64>::max();
        int bi = -1;
        for (std::size_t i = 0; i < n; ++i) {
            if (!inTree[i] && best[i] < bd) {
                bd = best[i];
                bi = static_cast<int>(i);
            }
        }
        if (bi < 0) break;
        inTree[static_cast<std::size_t>(bi)] = true;
        if (parent[static_cast<std::size_t>(bi)] >= 0) {
            u32 a = static_cast<u32>(bi);
            u32 b = static_cast<u32>(parent[static_cast<std::size_t>(bi)]);
            st.map.systems[a].links.push_back(b);
            st.map.systems[b].links.push_back(a);
        }
        for (std::size_t i = 0; i < n; ++i) {
            if (inTree[i]) continue;
            i64 d = dist2(st.map.systems[static_cast<std::size_t>(bi)], st.map.systems[i]);
            if (d < best[i]) {
                best[i] = d;
                parent[i] = bi;
            }
        }
    }
    // 额外边：每个系统连最近的两个非环邻居（制造环路与战略咽喉）
    for (std::size_t i = 0; i < n; ++i) {
        std::vector<std::pair<i64, u32>> cands;
        for (std::size_t j = 0; j < n; ++j) {
            if (i == j) continue;
            cands.emplace_back(dist2(st.map.systems[i], st.map.systems[j]), static_cast<u32>(j));
        }
        std::sort(cands.begin(), cands.end());
        int added = 0;
        for (const auto& c : cands) {
            if (added >= 2) break;
            if (static_cast<i64>(c.first) > 120 * 120) break;
            auto& links = st.map.systems[i].links;
            if (std::find(links.begin(), links.end(), c.second) != links.end()) continue;
            bool dup = false;
            for (u32 x : links)
                if (x == c.second) dup = true;
            if (dup) continue;
            st.map.systems[i].links.push_back(c.second);
            st.map.systems[c.second].links.push_back(static_cast<u32>(i));
            ++added;
        }
    }
    // 去重并排序（确定性）
    for (auto& s : st.map.systems) {
        std::sort(s.links.begin(), s.links.end());
        s.links.erase(std::unique(s.links.begin(), s.links.end()), s.links.end());
    }

    // 星区
    for (int i = 0; i < sectors; ++i) {
        Sector sec;
        sec.id = static_cast<u32>(i);
        sec.name = names.sector() + " " + std::to_string(i + 1);
        sec.development = Fixed::raw(static_cast<i64>(rng.range(RngStream::World, 300, 900)));
        st.map.sectors.push_back(std::move(sec));
    }
    for (auto& s : st.map.systems) {
        if (s.sector < st.map.sectors.size()) st.map.sectors[s.sector].systems.push_back(s.id);
    }

    // 首都星标记（由 EmpireGen 决定 owner 后设置）
    generatePlanets(st, opts);
}

void generatePlanets(GameState& st, const StarMapOptions& opts) {
    RngBus& rng = st.rng;
    NameGen names(st.seed ^ 0x9A11E7ull);
    st.planets.clear();

    for (auto& sys : st.map.systems) {
        int count = static_cast<int>(rng.range(RngStream::World, opts.minPlanets, opts.maxPlanets));
        for (int i = 0; i < count; ++i) {
            Planet p;
            p.id = static_cast<u32>(st.planets.size());
            p.system = sys.id;
            p.name = names.planet(static_cast<int>(p.id)) + " " + std::to_string(i + 1);
            p.type = static_cast<PlanetType>(rng.pick(RngStream::World, static_cast<std::size_t>(PlanetType::Count)));
            if (p.type == PlanetType::RingWorld || p.type == PlanetType::Ecumenopolis ||
                p.type == PlanetType::Shattered) {
                p.type = PlanetType::Rocky;  // 这些需要巨构/后期才能出现
            }
            p.size = static_cast<int>(rng.range(RngStream::World, 6, 25));
            p.habitability = Fixed::raw(static_cast<i64>(rng.range(RngStream::World, 100, 950)));
            p.stability = Fixed::raw(static_cast<i64>(rng.range(RngStream::World, 400, 800)));
            p.development = Fixed::raw(static_cast<i64>(rng.range(RngStream::World, 0, 200)));
            planetBaseYield(p.type, p.yield);
            for (auto& y : p.yield) y = y * Fixed::raw(static_cast<i64>(rng.range(RngStream::World, 700, 1300)));
            sys.planets.push_back(p.id);
            st.planets.push_back(std::move(p));
        }
    }
}


/// 播撒太空生物：每个星区若干群，作为可猎杀的资源点。
/// 海星（StarJelly）最常见，虚空鲸与裂隙潜行者稀有且危险。
void scatterFauna(GameState& st, RngBus& rng, int systems) {
    if (systems <= 0) return;
    const int herds = std::max(3, systems / 8);
    for (int i = 0; i < herds; ++i) {
        FaunaHerd f;
        f.system = static_cast<u32>(rng.nextU32(RngStream::World) % static_cast<u32>(systems));
        // 权重：海星 55%、晶簇虫群 25%、虚空鲸 14%、裂隙潜行者 6%
        u32 roll = rng.nextU32(RngStream::World) % 100;
        if (roll < 55) f.kind = FaunaKind::StarJelly;
        else if (roll < 80) f.kind = FaunaKind::CrystalSwarm;
        else if (roll < 94) f.kind = FaunaKind::VoidWhale;
        else f.kind = FaunaKind::RiftStalker;
        f.population = 80 + static_cast<i64>(rng.nextU32(RngStream::World) % 120);
        st.fauna.push_back(f);
    }
}

}  // namespace gf
