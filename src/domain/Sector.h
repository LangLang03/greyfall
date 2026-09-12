#pragma once
// 星系与星区（双曲星图）
#include <string>
#include <vector>

#include "util/Fixed.h"
#include "util/Ids.h"

namespace gf {

struct SystemNode {
    u32 id = 0;
    std::string name;
    i32 x = 0;   // 星图坐标（网格单位）
    i32 y = 0;
    u32 owner = kNoEmpire;
    u32 sector = 0;
    bool capital = false;
    bool colonized = false;
    bool megastructure = false;
    u32 megastructureId = 0;
    u32 anomaly = 0;                 // 异常点定义 id
    Fixed hazard = Fixed(0);         // 航行风险
    Fixed blockade = Fixed(0);       // 封锁强度 0..1
    Fixed tradeHub = Fixed(0);       // 贸易枢纽度
    std::vector<u32> links;          // 航线（无向，双向存储）
    std::vector<u32> planets;        // 行星 id
    Fixed pirates = Fixed(0);        // 海盗/私掠活动
};

struct Sector {
    u32 id = 0;
    std::string name;
    std::vector<u32> systems;
    Fixed development = Fixed::pct(50);
};

/// 星图：节点 + 邻接（航线）
struct StarMap {
    std::vector<SystemNode> systems;
    std::vector<Sector> sectors;
    /// 两点间航线跳数（BFS，-1 不可达）
    [[nodiscard]] int hops(u32 from, u32 to) const;
    /// 系统间最短距离（BFS 加权，简化：跳数）
    [[nodiscard]] bool connected(u32 a, u32 b) const;
    [[nodiscard]] const SystemNode* find(u32 id) const;
    [[nodiscard]] SystemNode* find(u32 id);
};

}  // namespace gf
