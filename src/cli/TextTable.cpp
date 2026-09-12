#include "cli/TextTable.h"

#include <algorithm>

#include "util/Fmt.h"
#include "util/Str.h"
#include "util/Utf8Width.h"

namespace gf {

TextTable& TextTable::header(std::vector<std::string> cols, std::vector<Align> aligns) {
    header_ = std::move(cols);
    ncols_ = std::max(ncols_, header_.size());
    aligns_ = std::move(aligns);
    aligns_.resize(ncols_, Align::Left);
    for (auto& a : aligns_)
        if (a != Align::Right && a != Align::Center) a = Align::Left;
    return *this;
}

TextTable& TextTable::row(std::vector<std::string> cells) {
    ncols_ = std::max(ncols_, cells.size());
    Row r;
    r.cells = std::move(cells);
    rows_.push_back(std::move(r));
    return *this;
}

TextTable& TextTable::separator() {
    Row r;
    r.isSep = true;
    rows_.push_back(std::move(r));
    return *this;
}

TextTable& TextTable::section(std::string_view title) {
    Row r;
    r.isSection = true;
    r.cells.emplace_back(title);
    rows_.push_back(std::move(r));
    return *this;
}

TextTable& TextTable::caption(std::string_view text) {
    caption_ = std::string(text);
    return *this;
}

std::string TextTable::render() const {
    if (ncols_ == 0) return caption_;
    std::vector<int> widths(ncols_, minColWidth_);
    for (std::size_t c = 0; c < header_.size(); ++c)
        widths[c] = std::max(widths[c], displayWidth(header_[c]));
    for (const Row& r : rows_) {
        if (r.isSep || r.isSection) continue;
        for (std::size_t c = 0; c < r.cells.size() && c < ncols_; ++c)
            widths[c] = std::max(widths[c], displayWidth(r.cells[c]));
    }

    std::string out;
    if (!caption_.empty()) {
        out += caption_;
        out.push_back('\n');
    }

    auto renderRow = [&](const std::vector<std::string>& cells, bool head) {
        std::string line = " ";
        for (std::size_t c = 0; c < ncols_; ++c) {
            std::string cell = c < cells.size() ? cells[c] : std::string();
            Align a = c < aligns_.size() ? aligns_[c] : Align::Left;
            if (head) a = a == Align::Right ? Align::Right : Align::Left;
            std::string padded;
            switch (a) {
                case Align::Right: padded = padLeft(cell, widths[c]); break;
                case Align::Center: padded = center(cell, widths[c]); break;
                case Align::Left: padded = padRight(cell, widths[c]); break;
            }
            line += padded;
            if (c + 1 < ncols_) line += "  ";
        }
        // 去掉行尾多余空格
        while (!line.empty() && line.back() == ' ') line.pop_back();
        return line;
    };

    auto ruleChar = [&]() {
        std::string s = " ";
        for (std::size_t c = 0; c < ncols_; ++c) {
            s.append(static_cast<std::size_t>(widths[c]), '-');
            if (c + 1 < ncols_) s += "--";
        }
        return s;
    };

    if (!header_.empty()) {
        out += renderRow(header_, true);
        out.push_back('\n');
        out += ruleChar();
        out.push_back('\n');
    }
    for (const Row& r : rows_) {
        if (r.isSep) {
            out += ruleChar();
            out.push_back('\n');
        } else if (r.isSection) {
            out += "--- ";
            out += r.cells.empty() ? std::string() : r.cells[0];
            out += " ";
            int used = 4 + (r.cells.empty() ? 0 : displayWidth(r.cells[0])) + 1;
            int total = 0;
            for (int w : widths) total += w + 2;
            if (total > used) out.append(static_cast<std::size_t>(total - used), '-');
            out.push_back('\n');
        } else {
            out += renderRow(r.cells, false);
            out.push_back('\n');
        }
    }
    // 去除末尾换行
    while (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

void TextTable::print() const { out(render()); }

}  // namespace gf
