#pragma once
// 全局「无」常量。
// 放在最基础的头文件里，避免各领域头文件各自定义或依赖包含顺序 ——
// 更关键的是：这些常量必须有一处权威定义，否则字段默认值容易写错
// （例如 Planet::owner 曾默认 0，使无主行星被误判为帝国 0 所有）。
#include "util/Fixed.h"

namespace gf {

inline constexpr u32 kNoEmpire = 0xFFFFFFFFu;
inline constexpr u32 kNoSystem = 0xFFFFFFFFu;
inline constexpr u32 kNoFleet = 0xFFFFFFFFu;

}  // namespace gf
