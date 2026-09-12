#pragma once
// 双曲星图生成：星区中心 + 高斯撒点 + MST 航线 + 行星
#include "core/GameState.h"
#include "util/Fixed.h"

namespace gf {

struct StarMapOptions {
    int systemCount = 64;
    int sectorCount = 8;
    int minPlanets = 1;
    int maxPlanets = 4;
};

void generateStarMap(GameState& st, const StarMapOptions& opts);
/// 播撒太空生物（可猎杀的资源点）
void scatterFauna(GameState& st, RngBus& rng, int systems);
void generatePlanets(GameState& st, const StarMapOptions& opts);

}  // namespace gf
