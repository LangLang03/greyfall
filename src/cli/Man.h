#pragma once
// 命令手册（man <cmd>）与总览帮助
#include <string>
#include <string_view>
#include <vector>

namespace gf {

struct ManEntry {
    std::string_view name;
    std::string_view group;
    std::string_view usage;
    std::string_view summary;
    std::string_view detail;
    std::string_view examples;
};

/// 全部手册条目（与命令表保持同步的超集）
[[nodiscard]] const std::vector<ManEntry>& manEntries();
[[nodiscard]] const ManEntry* findManEntry(std::string_view name);
/// 单命令手册文本
[[nodiscard]] std::string manPage(std::string_view name);
/// 总览帮助文本
[[nodiscard]] std::string helpText();
/// 程序与构建信息
[[nodiscard]] std::string versionText();

}  // namespace gf
