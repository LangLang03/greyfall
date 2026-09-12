#pragma once
// 字符串工具（零依赖，全部自实现）
#include <string>
#include <string_view>
#include <vector>

#include "util/Fixed.h"

namespace gf {

[[nodiscard]] std::string_view trim(std::string_view s) noexcept;
[[nodiscard]] std::string_view trimLeft(std::string_view s) noexcept;
[[nodiscard]] std::string_view trimRight(std::string_view s) noexcept;

[[nodiscard]] std::vector<std::string_view> split(std::string_view s, char sep, bool keepEmpty = false);
[[nodiscard]] std::vector<std::string> splitOwned(std::string_view s, char sep, bool keepEmpty = false);

[[nodiscard]] std::string join(const std::vector<std::string>& parts, std::string_view sep);
[[nodiscard]] std::string join(std::vector<std::string_view> parts, std::string_view sep);

[[nodiscard]] bool startsWith(std::string_view s, std::string_view pre) noexcept;
[[nodiscard]] bool endsWith(std::string_view s, std::string_view suf) noexcept;
[[nodiscard]] bool iequals(std::string_view a, std::string_view b) noexcept;
[[nodiscard]] std::string toLower(std::string_view s);
[[nodiscard]] std::string toUpper(std::string_view s);
[[nodiscard]] std::string replaceAll(std::string_view s, std::string_view from, std::string_view to);
[[nodiscard]] bool contains(std::string_view hay, std::string_view needle) noexcept;

[[nodiscard]] std::string hexEncode(const u8* data, std::size_t len);
[[nodiscard]] std::string hexEncode(std::string_view s);
/// 解析十六进制（忽略空白与 '-'），失败返回 false
[[nodiscard]] bool hexDecode(std::string_view s, std::vector<u8>& out);

/// 解析 "5EED-C0FFEE" 这类助记种子为 u64（非十六进制字符按 FNV 混合）
[[nodiscard]] u64 parseSeed(std::string_view s) noexcept;

/// 人类可读字节数：1.2 MB
[[nodiscard]] std::string humanBytes(u64 bytes);

}  // namespace gf
