#include "domain/Sector.h"

#include <deque>

namespace gf {

const SystemNode* StarMap::find(u32 id) const { return id < systems.size() ? &systems[id] : nullptr; }
SystemNode* StarMap::find(u32 id) { return id < systems.size() ? &systems[id] : nullptr; }

int StarMap::hops(u32 from, u32 to) const {
    if (from >= systems.size() || to >= systems.size()) return -1;
    if (from == to) return 0;
    std::vector<int> dist(systems.size(), -1);
    std::deque<u32> q;
    dist[from] = 0;
    q.push_back(from);
    while (!q.empty()) {
        u32 cur = q.front();
        q.pop_front();
        for (u32 nx : systems[cur].links) {
            if (nx >= systems.size() || dist[nx] >= 0) continue;
            dist[nx] = dist[cur] + 1;
            if (nx == to) return dist[nx];
            q.push_back(nx);
        }
    }
    return -1;
}

bool StarMap::connected(u32 a, u32 b) const { return hops(a, b) >= 0; }

}  // namespace gf
