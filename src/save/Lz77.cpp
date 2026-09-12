#include "save/Lz77.h"

#include <cstring>

namespace gf {
namespace {

constexpr int kMinMatch = 3;
constexpr int kMaxMatch = 258;
constexpr int kHashBits = 15;
constexpr std::size_t kHashSize = 1u << kHashBits;
// dist 用两个字节编码（低 8 位 + 高 8 位）⇒ 最大值必须是 65535，不能是 65536
constexpr std::size_t kMaxWindow = 65535;

inline u32 hash3(const u8* p) {
    u32 v = static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16);
    return (v * 2654435761u) >> (32 - kHashBits);
}

}  // namespace

std::vector<u8> lz77Compress(const std::vector<u8>& in, int level) {
    std::vector<u8> out;
    const std::size_t n = in.size();
    if (n == 0) return out;

    const int chainLimit = level >= 3 ? 128 : (level == 2 ? 32 : 8);
    std::vector<int> head(kHashSize, -1);
    std::vector<int> prev(n > 0 ? n : 1, -1);

    auto emitLiteralRun = [&out](const u8* p, std::size_t len) {
        while (len > 0) {
            std::size_t take = len > 0x7F ? 0x7F : len;
            out.push_back(static_cast<u8>(take));  // 0x00..0x7F：字面量游程
            out.insert(out.end(), p, p + take);
            p += take;
            len -= take;
        }
    };
    auto emitMatch = [&out](std::size_t len, std::size_t dist) {
        out.push_back(0x80);  // 匹配标记
        std::size_t l = len - kMinMatch;
        out.push_back(static_cast<u8>(l & 0xFF));
        out.push_back(static_cast<u8>((l >> 8) & 0xFF));
        out.push_back(static_cast<u8>(dist & 0xFF));
        out.push_back(static_cast<u8>((dist >> 8) & 0xFF));
    };

    std::size_t literalStart = 0;
    std::size_t i = 0;
    while (i < n) {
        std::size_t bestLen = 0;
        std::size_t bestDist = 0;
        if (i + kMinMatch <= n) {
            u32 h = hash3(in.data() + i);
            int cand = head[h];
            int tries = 0;
            while (cand >= 0 && tries < chainLimit) {
                std::size_t dist = i - static_cast<std::size_t>(cand);
                if (dist == 0 || dist > kMaxWindow) break;
                // 快速首字节检查
                if (in[static_cast<std::size_t>(cand) + bestLen] == in[i + bestLen]) {
                    std::size_t len = 0;
                    std::size_t maxLen = n - i;
                    if (maxLen > kMaxMatch) maxLen = kMaxMatch;
                    while (len < maxLen && in[static_cast<std::size_t>(cand) + len] == in[i + len]) ++len;
                    if (len > bestLen) {
                        bestLen = len;
                        bestDist = dist;
                        if (len >= kMaxMatch) break;
                    }
                }
                cand = prev[static_cast<std::size_t>(cand)];
                ++tries;
            }
            prev[i] = head[h];
            head[h] = static_cast<int>(i);
        }

        if (bestLen >= kMinMatch) {
            if (i > literalStart) emitLiteralRun(in.data() + literalStart, i - literalStart);
            emitMatch(bestLen, bestDist);
            // 把匹配区间内的位置也填进哈希表
            std::size_t end = i + bestLen;
            for (std::size_t k = i + 1; k < end && k + kMinMatch <= n; ++k) {
                u32 h = hash3(in.data() + k);
                prev[k] = head[h];
                head[h] = static_cast<int>(k);
            }
            i = end;
            literalStart = i;
        } else {
            ++i;
        }
    }
    if (i > literalStart) emitLiteralRun(in.data() + literalStart, i - literalStart);
    return out;
}

bool lz77Decompress(const u8* in, std::size_t inLen, std::vector<u8>& out) {
    out.clear();
    // 输出上限：防御损坏数据导致的无限膨胀
    constexpr std::size_t kMaxOut = 512ull * 1024 * 1024;
    std::size_t p = 0;
    while (p < inLen) {
        u8 tag = in[p++];
        if (tag < 0x80) {
            std::size_t len = tag;
            if (p + len > inLen) return false;
            if (out.size() + len > kMaxOut) return false;
            out.insert(out.end(), in + p, in + p + len);
            p += len;
        } else {
            if (p + 4 > inLen) return false;
            std::size_t l = static_cast<std::size_t>(in[p]) | (static_cast<std::size_t>(in[p + 1]) << 8);
            std::size_t dist = static_cast<std::size_t>(in[p + 2]) | (static_cast<std::size_t>(in[p + 3]) << 8);
            p += 4;
            std::size_t len = l + kMinMatch;
            if (dist == 0 || dist > out.size()) return false;
            if (out.size() + len > kMaxOut) return false;
            std::size_t start = out.size() - dist;
            for (std::size_t k = 0; k < len; ++k) out.push_back(out[start + k]);
        }
    }
    return true;
}

bool lz77Decompress(const std::vector<u8>& in, std::vector<u8>& out) {
    return lz77Decompress(in.data(), in.size(), out);
}

Fixed lz77Ratio(std::size_t inSize, std::size_t outSize) {
    if (inSize == 0) return Fixed(1);
    return Fixed::raw(mulDivSat(static_cast<i64>(outSize), FIX, static_cast<i64>(inSize)));
}

}  // namespace gf
