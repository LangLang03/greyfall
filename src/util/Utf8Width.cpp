#include "util/Utf8Width.h"

namespace gf {
namespace {

struct Range {
    u32 lo;
    u32 hi;
};

// 东亚宽字符（W）与全角（F）区间，取自 Unicode EastAsianWidth 的主要区段
constexpr Range kWide[] = {
    {0x1100, 0x115F},   {0x231A, 0x231B},   {0x2329, 0x232A},   {0x23E9, 0x23EC},   {0x23F0, 0x23F0},
    {0x23F3, 0x23F3},   {0x25FD, 0x25FE},   {0x2614, 0x2615},   {0x2648, 0x2653},   {0x267F, 0x267F},
    {0x2693, 0x2693},   {0x26A1, 0x26A1},   {0x26AA, 0x26AB},   {0x26BD, 0x26BE},   {0x26C4, 0x26C5},
    {0x26CE, 0x26CE},   {0x26D4, 0x26D4},   {0x26EA, 0x26EA},   {0x26F2, 0x26F3},   {0x26F5, 0x26F5},
    {0x26FA, 0x26FA},   {0x26FD, 0x26FD},   {0x2705, 0x2705},   {0x270A, 0x270B},   {0x2728, 0x2728},
    {0x274C, 0x274C},   {0x274E, 0x274E},   {0x2753, 0x2755},   {0x2757, 0x2757},   {0x2795, 0x2797},
    {0x27B0, 0x27B0},   {0x27BF, 0x27BF},   {0x2B1B, 0x2B1C},   {0x2B50, 0x2B50},   {0x2B55, 0x2B55},
    {0x2E80, 0x2E99},   {0x2E9B, 0x2EF3},   {0x2F00, 0x2FD5},   {0x2FF0, 0x2FFB},   {0x3000, 0x303E},
    {0x3041, 0x3096},   {0x3099, 0x30FF},   {0x3105, 0x312F},   {0x3131, 0x318E},   {0x3190, 0x31E3},
    {0x31F0, 0x321E},   {0x3220, 0x3247},   {0x3250, 0x4DBF},   {0x4E00, 0xA48C},   {0xA490, 0xA4C6},
    {0xA960, 0xA97C},   {0xAC00, 0xD7A3},   {0xF900, 0xFAFF},   {0xFE10, 0xFE19},   {0xFE30, 0xFE52},
    {0xFE54, 0xFE66},   {0xFE68, 0xFE6B},   {0xFF01, 0xFF60},   {0xFFE0, 0xFFE6},   {0x16FE0, 0x16FE4},
    {0x17000, 0x187F7}, {0x18800, 0x18CD5}, {0x1B000, 0x1B2FB}, {0x1F004, 0x1F004}, {0x1F0CF, 0x1F0CF},
    {0x1F18E, 0x1F18E}, {0x1F191, 0x1F19A}, {0x1F200, 0x1F320}, {0x1F32D, 0x1F335}, {0x1F337, 0x1F37C},
    {0x1F37E, 0x1F393}, {0x1F3A0, 0x1F3CA}, {0x1F3CF, 0x1F3D3}, {0x1F3E0, 0x1F3F0}, {0x1F3F4, 0x1F3F4},
    {0x1F3F8, 0x1F43E}, {0x1F440, 0x1F440}, {0x1F442, 0x1F4FC}, {0x1F4FF, 0x1F53D}, {0x1F54B, 0x1F54E},
    {0x1F550, 0x1F567}, {0x1F57A, 0x1F57A}, {0x1F595, 0x1F596}, {0x1F5A4, 0x1F5A4}, {0x1F5FB, 0x1F64F},
    {0x1F680, 0x1F6C5}, {0x1F6CC, 0x1F6CC}, {0x1F6D0, 0x1F6D2}, {0x1F6D5, 0x1F6D7}, {0x1F6EB, 0x1F6EC},
    {0x1F6F4, 0x1F6FC}, {0x1F7E0, 0x1F7EB}, {0x1F90C, 0x1F93A}, {0x1F93C, 0x1F945}, {0x1F947, 0x1F978},
    {0x1F97A, 0x1F9CB}, {0x1F9CD, 0x1F9FF}, {0x1FA70, 0x1FA74}, {0x1FA78, 0x1FA7A}, {0x1FA80, 0x1FA86},
    {0x1FA90, 0x1FAA8}, {0x1FAB0, 0x1FAB6}, {0x1FAC0, 0x1FAC2}, {0x1FAD0, 0x1FAD6}, {0x20000, 0x2FFFD},
    {0x30000, 0x3FFFD},
};

// 零宽：组合附加符号、变体选择符、零宽连接符
constexpr Range kZero[] = {
    {0x0300, 0x036F}, {0x0483, 0x0489}, {0x0591, 0x05BD}, {0x0610, 0x061A}, {0x064B, 0x065F},
    {0x0670, 0x0670}, {0x06D6, 0x06DC}, {0x0E31, 0x0E31}, {0x0E34, 0x0E3A}, {0x0EB1, 0x0EB1},
    {0x200B, 0x200F}, {0x2028, 0x202E}, {0x2060, 0x2064}, {0x20D0, 0x20F0}, {0xFE00, 0xFE0F},
    {0xFE20, 0xFE2F}, {0xFEFF, 0xFEFF}, {0x1F3FB, 0x1F3FF}, {0xE0100, 0xE01EF},
};

constexpr bool inRanges(const Range* table, std::size_t n, u32 cp) noexcept {
    std::size_t lo = 0, hi = n;
    while (lo < hi) {
        std::size_t mid = (lo + hi) / 2;
        if (cp < table[mid].lo)
            hi = mid;
        else if (cp > table[mid].hi)
            lo = mid + 1;
        else
            return true;
    }
    return false;
}

}  // namespace

int utf8Decode(std::string_view s, std::size_t pos, u32& cp) noexcept {
    if (pos >= s.size()) return 0;
    u8 b0 = static_cast<u8>(s[pos]);
    if (b0 < 0x80) {
        cp = b0;
        return 1;
    }
    int need = 0;
    u32 acc = 0;
    if ((b0 & 0xE0) == 0xC0) {
        need = 1;
        acc = b0 & 0x1Fu;
        if (acc < 2) return 0;  // overlong
    } else if ((b0 & 0xF0) == 0xE0) {
        need = 2;
        acc = b0 & 0x0Fu;
    } else if ((b0 & 0xF8) == 0xF0) {
        need = 3;
        acc = b0 & 0x07u;
        if (acc > 4) return 0;
    } else {
        return 0;
    }
    if (pos + static_cast<std::size_t>(need) >= s.size()) return 0;
    for (int i = 1; i <= need; ++i) {
        u8 b = static_cast<u8>(s[pos + static_cast<std::size_t>(i)]);
        if ((b & 0xC0) != 0x80) return 0;
        acc = (acc << 6) | (b & 0x3Fu);
    }
    cp = acc;
    return need + 1;
}

std::string utf8Encode(u32 cp) {
    std::string out;
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return out;
}

bool utf8Valid(std::string_view s) noexcept {
    std::size_t i = 0;
    while (i < s.size()) {
        u32 cp = 0;
        int n = utf8Decode(s, i, cp);
        if (n == 0) return false;
        i += static_cast<std::size_t>(n);
    }
    return true;
}

std::size_t utf8Length(std::string_view s) noexcept {
    std::size_t i = 0, n = 0;
    while (i < s.size()) {
        u32 cp = 0;
        int k = utf8Decode(s, i, cp);
        if (k == 0) {
            ++i;
            ++n;
        } else {
            i += static_cast<std::size_t>(k);
            ++n;
        }
    }
    return n;
}

int cpWidth(u32 cp) noexcept {
    if (cp == 0) return 0;
    if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) return 0;
    if (inRanges(kZero, sizeof(kZero) / sizeof(kZero[0]), cp)) return 0;
    if (inRanges(kWide, sizeof(kWide) / sizeof(kWide[0]), cp)) return 2;
    return 1;
}

int displayWidth(std::string_view s) noexcept {
    int w = 0;
    std::size_t i = 0;
    while (i < s.size()) {
        u32 cp = 0;
        int n = utf8Decode(s, i, cp);
        if (n == 0) {
            ++i;
            ++w;
        } else {
            i += static_cast<std::size_t>(n);
            w += cpWidth(cp);
        }
    }
    return w;
}

std::string truncateToWidth(std::string_view s, int width, std::string_view ellipsis) {
    if (displayWidth(s) <= width) return std::string(s);
    int ell = displayWidth(ellipsis);
    int budget = width - ell;
    if (budget < 0) budget = 0;
    std::string out;
    int used = 0;
    std::size_t i = 0;
    while (i < s.size()) {
        u32 cp = 0;
        int n = utf8Decode(s, i, cp);
        if (n == 0) break;
        int w = cpWidth(cp);
        if (used + w > budget) break;
        out.append(s.substr(i, static_cast<std::size_t>(n)));
        used += w;
        i += static_cast<std::size_t>(n);
    }
    out += ellipsis;
    return out;
}

}  // namespace gf
