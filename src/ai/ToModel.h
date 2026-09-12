#pragma once
// ToModel —— AI 对玩家的在线参数估计（心智理论）
//
// 每 tick 以玩家实际动作做极大似然/一致性更新：
//   θ ← θ + η·(∇_θ U(a_obs) - E[∇U])
// 特征为"动作-效用梯度"。modelConfidence 越高 → AI 越敢提前布防/勒索。
// 玩家可 spy --mission read-mind 读出对方对自己的 θ，并用故意动作注入错误梯度。
#include "ai/OmniscientReader.h"
#include "domain/Mind.h"
#include "util/Fixed.h"

namespace gf {

/// 把观测到的动作编码成 8 维目标特征的梯度
[[nodiscard]] std::array<Fixed, kGoalDim> actionGradient(const GameState& st, const Observable& obs,
                                                         const Observable& prev);

/// 在线更新：θ ← θ + η·(grad - E[grad])
void toModelUpdate(ToModel& m, const std::array<Fixed, kGoalDim>& grad, Fixed learningRate);

/// 类型后验更新（合作/剥削/报复/短视）
void toModelUpdateType(ToModel& m, const Observable& obs, const GameState& st, Fixed eta);

/// 风险偏好估计：由持仓集中度与杠杆推断
[[nodiscard]] Fixed toModelInferRisk(const GameState& st, const Observable& obs);

/// 贴现因子估计：由储蓄率与长期投资推断
[[nodiscard]] Fixed toModelInferDiscount(const GameState& st, const Observable& obs);

/// 惯性套路识别（8 维）：重复出现的动作模式
void toModelUpdateHabits(ToModel& m, const Observable& obs, const GameState& st);

/// 注入梯度噪声（decoy / 伪造道具的效果）
void toModelInjectNoise(ToModel& m, Fixed noise, Fixed contamination);

/// 人类可读的 θ 报告（read-mind 的输出）
[[nodiscard]] std::string toModelReport(const GameState& st, u32 observer, u32 subject);

/// 预测玩家下一步最可能的动作类型
[[nodiscard]] ActorType toModelDominantType(const ToModel& m);

}  // namespace gf
