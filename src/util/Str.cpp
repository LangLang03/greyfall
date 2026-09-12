#include "util/Str.h"

#include <algorithm>
#include <array>
#include <cstdio>

#include "util/Bits.h"

namespace gf {

std::string_view trimLeft(std::string_view s) noexcept {
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
    return s.substr(i);
}

std::string_view trimRight(std::string_view s) noexcept {
    std::size_t n = s.size();
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) --n;
    return s.substr(0, n);
}

std::string_view trim(std::string_view s) noexcept { return trimRight(trimLeft(s)); }

std::vector<std::string_view> split(std::string_view s, char sep, bool keepEmpty) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == sep) {
            std::string_view part = s.substr(start, i - start);
            if (keepEmpty || !part.empty()) out.push_back(part);
            start = i + 1;
        }
    }
    return out;
}

std::vector<std::string> splitOwned(std::string_view s, char sep, bool keepEmpty) {
    std::vector<std::string> out;
    for (auto p : split(s, sep, keepEmpty)) out.emplace_back(p);
    return out;
}

std::string join(const std::vector<std::string>& parts, std::string_view sep) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) out += sep;
        out += parts[i];
    }
    return out;
}

std::string join(std::vector<std::string_view> parts, std::string_view sep) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) out += sep;
        out += parts[i];
    }
    return out;
}

bool startsWith(std::string_view s, std::string_view pre) noexcept {
    return s.size() >= pre.size() && s.compare(0, pre.size(), pre) == 0;
}

bool endsWith(std::string_view s, std::string_view suf) noexcept {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

bool iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

std::string toLower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

std::string toUpper(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return out;
}

std::string replaceAll(std::string_view s, std::string_view from, std::string_view to) {
    if (from.empty()) return std::string(s);
    std::string out;
    out.reserve(s.size());
    std::size_t i = 0;
    while (i < s.size()) {
        if (s.size() - i >= from.size() && s.compare(i, from.size(), from) == 0) {
            out += to;
            i += from.size();
        } else {
            out.push_back(s[i]);
            ++i;
        }
    }
    return out;
}

bool contains(std::string_view hay, std::string_view needle) noexcept {
    return hay.find(needle) != std::string_view::npos;
}

namespace {
constexpr char kHexDigits[] = "0123456789abcdef";
int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
}  // namespace

std::string hexEncode(const u8* data, std::size_t len) {
    std::string out;
    out.reserve(len * 2);
    for (std::size_t i = 0; i < len; ++i) {
        out.push_back(kHexDigits[data[i] >> 4]);
        out.push_back(kHexDigits[data[i] & 0xF]);
    }
    return out;
}

std::string hexEncode(std::string_view s) {
    return hexEncode(reinterpret_cast<const u8*>(s.data()), s.size());
}

bool hexDecode(std::string_view s, std::vector<u8>& out) {
    out.clear();
    int hi = -1;
    for (char c : s) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '-' || c == ':') continue;
        int v = hexVal(c);
        if (v < 0) return false;
        if (hi < 0) {
            hi = v;
        } else {
            out.push_back(static_cast<u8>((hi << 4) | v));
            hi = -1;
        }
    }
    return hi < 0;
}

u64 parseSeed(std::string_view s) noexcept {
    std::string clean;
    clean.reserve(s.size());
    for (char c : s) {
        if (c == '-' || c == '_' || c == ' ') continue;
        clean.push_back(c);
    }
    if (clean.empty()) return 0;
    bool allHex = true;
    for (char c : clean)
        if (hexVal(c) < 0) {
            allHex = false;
            break;
        }
    if (allHex && clean.size() <= 16) {
        u64 v = 0;
        for (char c : clean) v = (v << 4) | static_cast<u64>(hexVal(c));
        return v;
    }
    // 非十六进制：FNV-1a 64 + SplitMix 收尾，保证分布良好
    u64 h = fnv1a64(clean);
    h += 0x9E3779B97F4A7C15ull;
    h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ull;
    h = (h ^ (h >> 27)) * 0x94D049BB133111EBull;
    return h ^ (h >> 31);
}

std::string humanBytes(u64 bytes) {
    constexpr std::array<std::string_view, 5> units{"B", "KB", "MB", "GB", "TB"};
    u64 whole = bytes;
    int idx = 0;
    u64 frac = 0;
    while (whole >= 1024 && idx + 1 < static_cast<int>(units.size())) {
        frac = (whole % 1024) * 10 / 1024;
        whole /= 1024;
        ++idx;
    }
    if (idx == 0) return std::to_string(whole) + " B";
    std::string out = std::to_string(whole);
    if (frac) {
        out.push_back('.');
        out.push_back(static_cast<char>('0' + frac));
    }
    out.push_back(' ');
    out += units[static_cast<std::size_t>(idx)];
    return out;
}

}  // namespace gf
