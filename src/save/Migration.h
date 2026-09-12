#pragma once
// 存档版本迁移链：Migration::chain(schema → current)
#include <string>

#include "core/Errors.h"
#include "util/Fixed.h"

namespace gf {

struct GameState;

namespace migration {

struct Step {
    int from;
    int to;
    const char* desc;
};

/// 已注册的迁移步骤（按 from 升序）
[[nodiscard]] const Step* steps(std::size_t& count);

/// 把状态从 fromSchema 升级到 kSchemaVersion；失败返回 false 并写 error
[[nodiscard]] bool chain(int fromSchema, GameState& st, std::string* error);

/// 该版本是否可以升级到当前版本
[[nodiscard]] bool canUpgrade(int fromSchema);

/// 人类可读的迁移路径描述
[[nodiscard]] std::string describe(int fromSchema);

}  // namespace migration
}  // namespace gf
