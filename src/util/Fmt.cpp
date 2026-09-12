#include "util/Fmt.h"

#include <cstdio>

#include "util/Str.h"
#include "util/Utf8Width.h"

namespace gf {
namespace {
bool g_color = false;
int g_verbose = 0;

constexpr std::string_view kReset = "\x1b[0m";

int indentWidth(std::string_view s) { return displayWidth(s); }

}  // namespace

void setColorEnabled(bool on) { g_color = on; }
bool colorEnabled() { return g_color; }

std::string ansiFor(Style s) {
    switch (s) {
        case Style::Heading: return "\x1b[1;36m";
        case Style::Sub: return "\x1b[36m";
        case Style::Good: return "\x1b[32m";
        case Style::Bad: return "\x1b[31m";
        case Style::Warn: return "\x1b[33m";
        case Style::Dim: return "\x1b[2m";
        case Style::Accent: return "\x1b[1;35m";
        case Style::Price: return "\x1b[1;37m";
        case Style::Plain: break;
    }
    return {};
}

std::string style(std::string_view text, Style s) {
    if (!g_color || s == Style::Plain) return std::string(text);
    std::string code = ansiFor(s);
    if (code.empty()) return std::string(text);
    std::string out;
    out.reserve(text.size() + code.size() * 2);
    out += code;
    out += text;
    out += kReset;
    return out;
}

std::string padRight(std::string_view s, int width) {
    int w = displayWidth(s);
    std::string out(s);
    if (w < width) out.append(static_cast<std::size_t>(width - w), ' ');
    return out;
}

std::string padLeft(std::string_view s, int width) {
    int w = displayWidth(s);
    std::string out;
    if (w < width) out.append(static_cast<std::size_t>(width - w), ' ');
    out += s;
    return out;
}

std::string center(std::string_view s, int width) {
    int w = displayWidth(s);
    if (w >= width) return std::string(s);
    int total = width - w;
    int left = total / 2;
    std::string out(static_cast<std::size_t>(left), ' ');
    out += s;
    out.append(static_cast<std::size_t>(total - left), ' ');
    return out;
}

std::vector<std::string> wrapText(std::string_view text, int width, std::string_view firstIndent,
                                  std::string_view restIndent) {
    std::vector<std::string> lines;
    if (width < 8) width = 8;
    std::string cur(firstIndent);
    int curW = indentWidth(firstIndent);
    bool hasContent = false;

    std::size_t i = 0;
    auto flush = [&]() {
        lines.push_back(cur);
        cur.assign(restIndent);
        curW = indentWidth(restIndent);
        hasContent = false;
    };

    while (i < text.size()) {
        if (text[i] == '\n') {
            flush();
            ++i;
            continue;
        }
        // 取出一个"词"（CJK 逐字，拉丁按空格切分）
        std::size_t start = i;
        u32 cp = 0;
        int n = utf8Decode(text, i, cp);
        if (n == 0) {
            ++i;
            continue;
        }
        bool wide = cpWidth(cp) == 2;
        if (wide) {
            i += static_cast<std::size_t>(n);
        } else {
            while (i < text.size()) {
                u32 c2 = 0;
                int n2 = utf8Decode(text, i, c2);
                if (n2 == 0) break;
                if (c2 == ' ' || c2 == '\n' || cpWidth(c2) == 2) break;
                i += static_cast<std::size_t>(n2);
            }
            if (i == start) i += static_cast<std::size_t>(n);
        }
        std::string_view tok = text.substr(start, i - start);
        int tw = displayWidth(tok);
        if (curW + tw > width && hasContent) {
            // 去掉行尾空格后换行
            while (!cur.empty() && cur.back() == ' ') cur.pop_back();
            flush();
            if (tok == " ") continue;
        }
        cur += tok;
        curW += tw;
        hasContent = true;
    }
    if (hasContent || lines.empty()) lines.push_back(cur);
    return lines;
}

std::string wrapJoin(std::string_view text, int width, std::string_view indent) {
    std::vector<std::string> ls = wrapText(text, width, indent, indent);
    std::string out;
    for (std::size_t i = 0; i < ls.size(); ++i) {
        if (i) out.push_back('\n');
        out += ls[i];
    }
    return out;
}

std::string bar(Fixed ratio, int width, char full, char empty) {
    if (width <= 0) return {};
    i64 r = ratio.rawValue();
    if (r < 0) r = 0;
    if (r > FIX) r = FIX;
    int n = static_cast<int>((r * width + FIX / 2) / FIX);
    if (n > width) n = width;
    std::string out;
    out.append(static_cast<std::size_t>(n), full);
    out.append(static_cast<std::size_t>(width - n), empty);
    return out;
}

std::string kvLine(std::string_view key, std::string_view value, int keyWidth) {
    return padRight(key, keyWidth) + " : " + std::string(value);
}

void out(std::string_view line) {
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fputc('\n', stdout);
}

void outLine(const std::string& s) { out(s); }

void setVerbose(int level) { g_verbose = level; }
int verboseLevel() { return g_verbose; }

void note(std::string_view line) {
    if (g_verbose >= 1) {
        std::fwrite(line.data(), 1, line.size(), stdout);
        std::fputc('\n', stdout);
    }
}

void trace(std::string_view line) {
    if (g_verbose >= 2) {
        std::fwrite(line.data(), 1, line.size(), stdout);
        std::fputc('\n', stdout);
    }
}

void warn(std::string_view line) {
    std::string s = std::string("[warn] ") + std::string(line);
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fputc('\n', stdout);
}

}  // namespace gf
