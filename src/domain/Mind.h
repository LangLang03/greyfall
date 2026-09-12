#pragma once
// 心智模型的数据结构（算法实现见 ai/ToModel.{h,cpp}）。
// 放在 domain 层是为了保持单向依赖：ai → domain，domain 不反向依赖 ai。
#include <array>
#include <string>
#include <vector>

#include "util/Fixed.h"

namespace gf {

inline constexpr int kMaxEmpires = 16;
inline constexpr int kGoalDim = 8;       // 效用目标维度
inline constexpr int kHabitDim = 8;      // 玩家惯性套路维度
inline constexpr int kTypeCount = 4;     // 合作/剥削/报复/短视

/// 玩家（或任意主体）的"策略类型"
enum class ActorType : u8 { Cooperate = 0, Exploit, Retaliate, Myopic, Count };

/// 效用目标维度：领土 / 财富 / 科技 / 军事 / 声望 / 知识 / 稳定 / 信仰
enum class GoalDim : u8 { Territory = 0, Wealth, Science, Military, Prestige, Knowledge, Stability, Faith, Count };

/// ToModel —— AI 对某一主体的在线参数估计（方案 §7.2）
struct ToModel {
    std::array<Fixed, kGoalDim> wGoal{};               // 目标权重
    Fixed riskAversion = Fixed::pct(50);               // 风险厌恶
    Fixed discount = Fixed::raw(950);                  // 贴现因子 δ
    std::array<Fixed, kTypeCount> typeBelief{};        // 类型后验
    std::array<Fixed, kHabitDim> habits{};             // 惯性套路强度（近因加权）
    Fixed modelConfidence = Fixed(0);                  // 模型置信度 0..1
    u32 observations = 0;                              // 观测样本数
    u32 deceptions = 0;                                // 检测到的欺骗次数
    /// 最近一次更新注入的梯度噪声（decoy 的效果）
    Fixed lastGradientNoise = Fixed(0);
    /// 被伪造数据污染的强度（道具 BeliefIntervene / forge-prove 的效果）
    Fixed contamination = Fixed(0);
};

/// 单个 AI 阵营的完整心智状态
struct EmpireMind {
    ToModel playerModel;
    /// 对每个已知主体的观感（含玩家）——信誉现值
    std::array<Fixed, kMaxEmpires> reputation{};
    /// 怨恨残留（按 grudgeHalfLife 衰减）
    std::array<Fixed, kMaxEmpires> grudge{};
    /// 对每个主体的威胁评估
    std::array<Fixed, kMaxEmpires> threat{};
    /// 本 tick 计算的背叛 EV 分解（供 intel --threat / logs --phase AI 复盘）
    Fixed lastBetrayalEV = Fixed(0);
    Fixed lastBetrayalRepCost = Fixed(0);
    Fixed lastBetrayalWarCost = Fixed(0);
    Fixed lastBetrayalGain = Fixed(0);
    std::string lastBetrayalReason;
    /// 已识别的玩家"套路"（IntentPredictor 的烙印）
    std::array<Fixed, kHabitDim> playerPattern{};
    /// 该 AI 的智能等级（difficulty 推导）：foresight ∈ [1..4]
    u8 foresight = 2;
    /// 本季剩余计算预算
    i64 nodeBudget = 0;
};

/// 某主体对另一主体的观测通道（用于信誉更新的噪声建模）
struct ObservationChannel {
    u32 target = 0;
    Fixed distanceNoise = Fixed(0);   // 距离导致的噪声
    Fixed propaganda = Fixed(0);      // 宣传偏移
    Fixed counterIntel = Fixed(0);    // 反间谍强度
    Fixed weight = Fixed(1);
};

}  // namespace gf
