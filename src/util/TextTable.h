#pragma once
// TextTable —— 东亚宽度感知的等宽表格渲染（不依赖 locale）
//
// 注意它住在 util/ 而不是 cli/：这是一种**纯文本格式化**工具，
// 只依赖 util/{Fixed,Fmt,Str,Utf8Width}，不需要任何命令层能力。
// 它此前放在 cli/ 导致 16 个 domain/combat 文件不得不 include "cli/..."
// 才能渲染报告 —— 那是分层违规（domain 反向依赖 cli）。
// 把工具下沉到 util/ 后，那 16 条反向边一次性消失。
#include <string>
#include <string_view>
#include <vector>

#include "util/Fixed.h"

namespace gf {

enum class Align { Left, Right, Center };

class TextTable {
public:
    explicit TextTable(int minColWidth = 3) : minColWidth_(minColWidth) {}

    /// 设置表头（同时确定列数）
    TextTable& header(std::vector<std::string> cols, std::vector<Align> aligns = {});
    /// 追加一行（列数不足自动补空）
    TextTable& row(std::vector<std::string> cells);
    /// 追加分隔线
    TextTable& separator();
    /// 追加一个跨列小节标题
    TextTable& section(std::string_view title);
    /// 标题（表格上方的说明行）
    TextTable& caption(std::string_view text);

    [[nodiscard]] std::string render() const;
    void print() const;

    [[nodiscard]] std::size_t rows() const { return rows_.size(); }

private:
    struct Row {
        std::vector<std::string> cells;
        bool isSep = false;
        bool isSection = false;
    };
    std::vector<std::string> header_;
    std::vector<Align> aligns_;
    std::vector<Row> rows_;
    std::string caption_;
    int minColWidth_ = 3;
    std::size_t ncols_ = 0;
};

}  // namespace gf
