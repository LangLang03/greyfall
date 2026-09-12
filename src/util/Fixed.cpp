#include "util/Fixed.h"

#include <algorithm>
#include <cmath>

namespace gf {
namespace {

bool isDigit(char c) noexcept { return c >= '0' && c <= '9'; }

}  // namespace

Fixed parseFixed(std::string_view s, bool* ok) noexcept {
    if (ok) *ok = false;
    if (s.empty()) return Fixed(0);
    std::size_t i = 0;
    bool neg = false;
    if (s[i] == '+' || s[i] == '-') {
        neg = (s[i] == '-');
        ++i;
    }
    i64 wholePart = 0;
    i64 fracPart = 0;
    int fracDigits = 0;
    bool any = false;
    for (; i < s.size(); ++i) {
        char c = s[i];
        if (isDigit(c)) {
            any = true;
            if (wholePart > (INT64_MAX / 20)) return Fixed(0);  // 溢出保护
            wholePart = wholePart * 10 + (c - '0');
        } else if (c == ',' || c == '_') {
            continue;
        } else {
            break;
        }
    }
    if (i < s.size() && s[i] == '.') {
        ++i;
        for (; i < s.size(); ++i) {
            char c = s[i];
            if (!isDigit(c)) break;
            any = true;
            if (fracDigits < 6) {
                fracPart = fracPart * 10 + (c - '0');
                ++fracDigits;
            }
        }
    }
    if (i != s.size() || !any) return Fixed(0);
    // 把 frac 归一到 3 位小数
    while (fracDigits < 3) {
        fracPart *= 10;
        ++fracDigits;
    }
    while (fracDigits > 3) {
        fracPart /= 10;
        --fracDigits;
    }
    i64 raw = mulSat(wholePart, FIX);
    raw = addSat(raw, fracPart);
    if (neg) raw = -raw;
    if (ok) *ok = true;
    return Fixed::raw(raw);
}

i64 parseInt(std::string_view s, i64 fallback) noexcept {
    if (s.empty()) return fallback;
    std::size_t i = 0;
    bool neg = false;
    if (s[i] == '+' || s[i] == '-') {
        neg = (s[i] == '-');
        ++i;
    }
    i64 acc = 0;
    bool any = false;
    for (; i < s.size(); ++i) {
        char c = s[i];
        if (c == ',' || c == '_') continue;
        if (!isDigit(c)) return fallback;
        any = true;
        if (acc > (INT64_MAX / 20)) return fallback;
        acc = acc * 10 + (c - '0');
    }
    if (!any) return fallback;
    return neg ? -acc : acc;
}

std::string groupDigits(i64 x) {
    bool neg = x < 0;
    u64 mag = neg ? (~static_cast<u64>(x) + 1ull) : static_cast<u64>(x);
    std::string digits = std::to_string(mag);
    std::string out;
    out.reserve(digits.size() + digits.size() / 3 + 1);
    if (neg) out.push_back('-');
    int lead = static_cast<int>(digits.size() % 3);
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i != 0 && (static_cast<int>(i) % 3) == lead) out.push_back(',');
        out.push_back(digits[i]);
    }
    return out;
}

namespace {

std::string formatFixedRaw(i64 raw, int dec, bool group) {
    if (dec < 0) dec = 0;
    if (dec > 3) dec = 3;
    bool neg = raw < 0;
    u64 mag = neg ? (~static_cast<u64>(raw) + 1ull) : static_cast<u64>(raw);
    u64 scale = 1;
    for (int i = dec; i < 3; ++i) scale *= 10;  // 从 milli 收缩到 dec 位
    u64 scaled = mag / scale;
    if (scale > 1) {
        u64 rem = mag % scale;
        if (rem * 2 >= scale) scaled += 1;  // 四舍五入
    }
    u64 unit = 1;
    for (int i = 0; i < dec; ++i) unit *= 10;
    u64 whole = scaled / unit;
    u64 frac = scaled % unit;
    std::string out;
    if (neg && (whole != 0 || frac != 0)) out.push_back('-');
    std::string ws = std::to_string(whole);
    if (group) {
        int lead = static_cast<int>(ws.size() % 3);
        for (std::size_t i = 0; i < ws.size(); ++i) {
            if (i != 0 && (static_cast<int>(i) % 3) == lead) out.push_back(',');
            out.push_back(ws[i]);
        }
    } else {
        out += ws;
    }
    if (dec > 0) {
        out.push_back('.');
        std::string fs = std::to_string(frac);
        out.append(static_cast<std::size_t>(dec) - std::min<std::size_t>(fs.size(), static_cast<std::size_t>(dec)), '0');
        out += fs;
    }
    return out;
}

}  // namespace

std::string fixedStr(Fixed x, int dec) { return formatFixedRaw(x.v, dec, true); }
std::string fixedStrPlain(Fixed x, int dec) { return formatFixedRaw(x.v, dec, false); }
std::string fixedStrSigned(Fixed x, int dec) {
    std::string s = fixedStrPlain(x, dec);
    if (!s.empty() && s[0] != '-') s.insert(s.begin(), '+');
    return s;
}

}  // namespace gf
