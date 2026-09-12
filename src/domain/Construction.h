#pragma once
// 建造与造舰的工期系统
//
// 设计动机：
//   早期 `build` 与 `ship-build` 都是**即时**的 —— 一次调用就把建筑或舰队
//   直接塞进结果里。这带来两个后果：
//     1) 建造速率（BuildRate）与产能类加成完全没有意义，因为不存在“工期”；
//     2) 玩家可以在同一季瞬间铺满所有行星、爆出整支舰队，
//        「军力积累需要时间」这条纵深设计被彻底绕过。
//
// 现行规则：
//   * 建筑进入行星的**建造队列**，逐季推进；受建造速率与政体效率影响；
//   * 舰船在**船坞**中建造，工期由舰体吨位决定；需要目标星系有船坞；
//   * 两者都要先付清费用，取消只退还部分。
#include <string>
#include <vector>

#include "util/Fixed.h"

namespace gf {

struct GameState;
struct Empire;
struct Planet;

/// 建筑的基础工期（季）
[[nodiscard]] u32 buildingBaseTicks(int buildingIdx);
/// 舰体的基础工期（季）
[[nodiscard]] u32 hullBaseTicks(int hullClass);
/// 该行星当前的建造速度倍率（建造速率修正 + 政体 + 稳定度）
[[nodiscard]] Fixed buildSpeedMultiplier(const GameState& st, const Planet& p);
/// 该帝国的造舰速度倍率（工业产能 + 政体）
[[nodiscard]] Fixed shipyardSpeedMultiplier(const GameState& st, u32 empire);

/// 把建筑加入行星建造队列（扣费在此完成）
[[nodiscard]] bool enqueueBuilding(GameState& st, u32 empire, u32 planetId, int buildingIdx,
                                   std::string* msg);
/// 取消队列中的某一项（退还部分费用）
[[nodiscard]] bool cancelBuildOrder(GameState& st, u32 empire, u32 planetId, std::size_t index,
                                    std::string* msg);
/// 清空某行星的全部队列
[[nodiscard]] bool clearBuildQueue(GameState& st, u32 empire, u32 planetId, std::string* msg);

/// 该星系是否具备造舰能力（任一己方行星建有船坞）
[[nodiscard]] bool hasShipyard(const GameState& st, u32 empire, u32 system);
/// 把舰船加入造舰队列（先付款）
[[nodiscard]] bool enqueueShip(GameState& st, u32 empire, u32 system, u32 designId,
                               std::string* msg);
/// 每 tick：推进所有行星的建造队列与造舰队列
void constructionPhase(GameState& st);

/// 队列文本
[[nodiscard]] std::string buildQueueReport(const GameState& st, u32 empire);

}  // namespace gf
