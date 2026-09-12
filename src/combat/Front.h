#pragma once
// 前线系统（HOI4 式）：自动识别战线边界并按兵力分配进攻方向
//
// 设计要点：
//   * 前线 = 两国领土之间所有接壤的「星链接口」集合。
//   * 每个前线被划分为若干**扇区**（sector），每扇区对应一个可进攻的敌方星系。
//   * `assignFronts` 每 tick 把闲置舰队按各扇区的敌我兵力比自动分配
//     —— 兵力占优处堆叠、劣势处只留最低守备。
//   * 玩家可用 `front` 命令查看全局战线，或用 `fleet --order advance` 手动指定。
#include <string>
#include <vector>

#include "core/GameState.h"
#include "util/Fixed.h"

namespace gf {

/// 一个可进攻的敌方星系（前线扇区）
struct FrontSector {
    u32 system = 0;             // 目标星系
    u32 fromSystem = 0;         // 我方出发星系（相邻）
    u32 owner = 0;              // 该扇区归属的敌方帝国
    Fixed friendlyPower = Fixed(0);   // 我方在该扇区可投入的战力
    Fixed enemyPower = Fixed(0);      // 敌方守备战力（含要塞）
    Fixed ratio = Fixed(0);           // 我/敌 战力比
    bool contested = false;           // 是否正在交战
    std::string terrain;
};

/// 一条进攻轴线（多路突破时的一条进攻方向）
struct FrontAxis {
    u32 system = 0;            // 目标星系
    Fixed ratio = Fixed(0);    // 该扇区的战力比
    Fixed required = Fixed(0); // 达成突破所需的战力（敌方守备 × 系数）
    Fixed assigned = Fixed(0); // 已分配的战力
    bool viable = false;       // 是否具备独立突破能力（ratio ≥ 阈值）
};

/// 一条前线（对一个敌国的整条边界）
struct Front {
    u32 enemy = 0;
    std::vector<FrontSector> sectors;
    Fixed friendlyTotal = Fixed(0);
    Fixed enemyTotal = Fixed(0);
    Fixed overallRatio = Fixed(0);
    /// 建议进攻的扇区（战力比最高者）
    u32 recommendedSector = kNoSystem;
    /// 多路突破方案：当总体优势足够时，挑选若干可独立突破的扇区同时进攻。
    /// 优势不足时只保留一条主攻轴（集中兵力）。
    std::vector<FrontAxis> axes;
    /// 采用的进攻轴数（1 = 单点主攻，>1 = 多路突破）
    int axisCount = 0;
    /// 当前是否在实施多路突破
    bool multiAxis = false;
    std::string enemyName;
    std::string posture;
};

/// 计算某帝国对某敌国的前线
[[nodiscard]] Front computeFront(const GameState& st, u32 empire, u32 enemy);

/// 全部前线
[[nodiscard]] std::vector<Front> allFronts(const GameState& st, u32 empire);
/// 规划多路突破方案（在给定可用战力下挑选进攻轴）
void planFrontAxes(const GameState& st, u32 empire, Front& fr, Fixed availablePower);

/// 每 tick：把闲置舰队按前线兵力比自动分配（HOI4 的「前线部署」）
void assignFronts(GameState& st);

/// 战线总览文本
[[nodiscard]] std::string frontsText(const GameState& st, u32 empire);

/// 某条前线的详情
[[nodiscard]] std::string frontDetailText(const GameState& st, u32 empire, u32 enemy);

}  // namespace gf
