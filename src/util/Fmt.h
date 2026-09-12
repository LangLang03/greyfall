#pragma once
// 文本渲染：段落折行、内边距、颜色、进度条
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "util/Fixed.h"

namespace gf {

enum class Style { Plain, Heading, Sub, Good, Bad, Warn, Dim, Accent, Price };

/// 颜色开关（--ascii / NO_COLOR / 重定向时关闭）
void setColorEnabled(bool on);
[[nodiscard]] bool colorEnabled();

[[nodiscard]] std::string style(std::string_view text, Style s);
/// 定宽内边距（按显示宽度补空格）
[[nodiscard]] std::string padRight(std::string_view s, int width);
[[nodiscard]] std::string padLeft(std::string_view s, int width);
[[nodiscard]] std::string center(std::string_view s, int width);

/// 解析 ANSI 样式表（Style → 转义序列）
[[nodiscard]] std::string ansiFor(Style s);

/// 段落折行：按宽度折行，首行前缀 first 之后每行前缀 rest；CJK 安全（不在字符中间断）
[[nodiscard]] std::vector<std::string> wrapText(std::string_view text, int width,
                                                std::string_view firstIndent = {},
                                                std::string_view restIndent = {});
/// 折行并拼接为单个带 \n 的字符串
[[nodiscard]] std::string wrapJoin(std::string_view text, int width, std::string_view indent = {});

/// 横向条形（用于民心/战力等 0..1 指标），width 为字符数
[[nodiscard]] std::string bar(Fixed ratio, int width, char full = '#', char empty = '.');

/// 表格化的键值对输出
[[nodiscard]] std::string kvLine(std::string_view key, std::string_view value, int keyWidth = 16);

/// 输出到 stdout 的一行（自动 \n）
void out(std::string_view line);
void outLine(const std::string& s);

/// 供 --quiet / --verbose 控制
void setVerbose(int level);
[[nodiscard]] int verboseLevel();
/// 详细日志（verbose>=1 才输出）
void note(std::string_view line);
/// 调试日志（verbose>=2 才输出）
void trace(std::string_view line);
/// 警告（始终输出到 stderr 之外的 stdout，保持管道友好）
void warn(std::string_view line);

}  // namespace gf
