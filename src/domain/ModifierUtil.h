#pragma once
// 词缀查询工具（gen/ModifierGen.h 的轻量转发，避免循环包含）
#include "gen/ModifierGen.h"

namespace gf {
// 统一入口：所有子系统通过 hasModifier(st.modifierBits, bit) 查询全局词缀
}  // namespace gf
