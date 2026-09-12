#pragma once
// UTF-8 解码与东亚显示宽度（自带宽度表，不依赖 locale / wcwidth）
#include <cstddef>
#include <string>
#include <string_view>

#include "util/Fixed.h"

namespace gf {

/// 解码一个 UTF-8 码点；返回消耗字节数（0 表示非法序列）
[[nodiscard]] int utf8Decode(std::string_view s, std::size_t pos, u32& cp) noexcept;
/// 编码一个码点到 UTF-8
[[nodiscard]] std::string utf8Encode(u32 cp);
/// 完整校验 UTF-8 合法性
[[nodiscard]] bool utf8Valid(std::string_view s) noexcept;
/// 码点个数
[[nodiscard]] std::size_t utf8Length(std::string_view s) noexcept;
/// 显示宽度：CJK 全角 2，组合符 0，其余 1
[[nodiscard]] int cpWidth(u32 cp) noexcept;
/// 字符串显示宽度
[[nodiscard]] int displayWidth(std::string_view s) noexcept;
/// 按显示宽度截断（不切碎多字节字符），超出加省略号
[[nodiscard]] std::string truncateToWidth(std::string_view s, int width, std::string_view ellipsis = "…");

}  // namespace gf
