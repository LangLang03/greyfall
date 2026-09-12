#pragma once
// 间谍网络（HOI4 式）
//
// 与已有机制的关系：
//   spy   —— 一次性行动（花预算立刻换取一份情报）
//   SpyAgency / SpyNetwork —— **持续性**的情报基础设施：
//       在目标帝国内部建立网络 → 渗透度随时间累积 → 解锁更强任务 →
//       渗透越深越容易被反间谍发现 → 被破获会引发外交事件
//
// 核心权衡：
//   派驻特工越多 ⇒ 渗透越快，但暴露风险也越高
//   渗透越深 ⇒ 情报质量越高、可用任务越强，但一旦被破获损失越大
//   目标的「情报防御」会同时拖慢渗透并加速暴露
#include <string>
#include <vector>

#include "util/Fixed.h"

namespace gf {

struct GameState;

/// 可执行的间谍任务
/// 顺序即渗透门槛由低到高（UI 表格与测试都依赖这一点）
/// 任务按**渗透门槛升序**排列（反渗透除外，见下）。
/// 顺序即强度梯度，测试会校验这一单调性。
enum class SpyMission : u8 {
    Recon = 0,        // 侦察（10%）
    FundUnrest,       // 煽动（25%）
    StealTech,        // 窃取科技（40%）
    Sabotage,         // 破坏（55%）
    FalseFlag,        // 假情报（60%）：污染目标对第三方的认知
    SleeperCell,      // 潜伏（65%）
    Ideological,      // 意识形态渗透（70%）：长期改变目标的伦理与派系格局
    CounterIntel,     // 反渗透（20%，对**本国**执行）：门槛刻意压低，排在最后
    Count,
};

inline constexpr int kSpyMissionCount = static_cast<int>(SpyMission::Count);

/// 在目标帝国内的一个间谍网络
struct SpyNetwork {
    u32 target = 0;                  // 目标帝国
    Fixed infiltration = Fixed(0);   // 渗透度 0..1
    Fixed exposure = Fixed(0);       // 暴露风险 0..1
    int agents = 0;                  // 派驻特工数
    u32 establishedTick = 0;
    bool burned = false;             // 已被破获
    /// 累计
    u32 missionsRun = 0;
    u32 timesBurned = 0;
    /// 被破获的季数（0 = 未被破获）。破获后保留一段时间供防御方查看。
    u64 burnedTick = 0;
    Fixed intelValue = Fixed(0);     // 累计获得的情报价值
    /// 上次任务季数（用于冷却）
    u32 lastMissionTick = 0;
};

/// 一个帝国的情报机构
struct SpyAgency {
    std::vector<SpyNetwork> networks;
    int agentPool = 3;               // 可用特工总数（未被派驻的）
    int totalAgents = 3;             // 特工总数
    Fixed funding = Fixed(0);        // 每季经费（提高渗透速率）
    u32 totalMissions = 0;
    u32 totalBurned = 0;
    /// 对每个目标的情报掌握度（0..1，由渗透度换算，供其他系统查询）
    [[nodiscard]] Fixed infiltrationOf(u32 target) const {
        for (const auto& n : networks) {
            if (n.target != target || n.burned) continue;
            return n.infiltration;
        }
        return Fixed(0);
    }
    [[nodiscard]] bool hasNetwork(u32 target) const {
        for (const auto& n : networks) {
            if (n.target == target && !n.burned) return true;
        }
        return false;
    }
};

[[nodiscard]] std::string_view spyMissionName(SpyMission m);
[[nodiscard]] SpyMission spyMissionFromName(std::string_view s);
/// 该任务所需的最低渗透度
[[nodiscard]] Fixed spyMissionMinInfiltration(SpyMission m);
/// 该任务的渗透度消耗
[[nodiscard]] Fixed spyMissionCost(SpyMission m);
/// 该任务的每季经费
[[nodiscard]] i64 spyMissionUpkeep(SpyMission m);

/// 建立网络（需要空闲特工）
[[nodiscard]] bool spyEstablish(GameState& st, u32 empire, u32 target, std::string* err);
/// 增派 / 撤回特工（delta 可正可负）
[[nodiscard]] bool spyAssignAgents(GameState& st, u32 empire, u32 target, int delta, std::string* err);
/// 执行任务
[[nodiscard]] bool spyRunMission(GameState& st, u32 empire, u32 target, SpyMission m, std::string* err);
/// 撤销网络（回收特工）
[[nodiscard]] bool spyDisband(GameState& st, u32 empire, u32 target, std::string* err);

/// 每季推进：渗透增长、暴露累积、破获判定
void spyPhase(GameState& st);
/// AI 的情报决策：建立网络、派特工、挑时机执行任务
void spyAiPhase(GameState& st);

/// 目标的反间谍强度（含修正）
[[nodiscard]] Fixed counterIntelOf(const GameState& st, u32 empire);
/// 我方的渗透能力（含修正）
[[nodiscard]] Fixed infiltrationPowerOf(const GameState& st, u32 empire);

/// 意识形态压力：source 对 target 的长期渗透积累（0..1）
[[nodiscard]] Fixed ideologyPressureOf(const GameState& st, u32 target, u32 source);
/// 每 tick：意识形态压力衰减；压力高时改变目标的派系格局乃至伦理
void ideologyPhase(GameState& st);
/// 反渗透：花国库扫荡境内的外国网络
[[nodiscard]] bool counterInfiltrate(GameState& st, u32 empire, std::string* msg);
/// 反渗透态势：列出正在渗透我国的外国网络
[[nodiscard]] std::string counterIntelReport(const GameState& st, u32 empire);

/// 文本
[[nodiscard]] std::string spyAgencyText(const GameState& st, u32 empire);
[[nodiscard]] std::string spyNetworkText(const GameState& st, u32 empire, u32 target);

}  // namespace gf
