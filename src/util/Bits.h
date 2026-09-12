#pragma once
// 位操作与哈希小工具（零依赖）
#include <bit>
#include <cstdint>
#include <string_view>

#include "util/Fixed.h"

namespace gf {

[[nodiscard]] constexpr u32 rotl32(u32 x, int r) noexcept {
    return static_cast<u32>((x << r) | (x >> (32 - r)));
}
[[nodiscard]] constexpr u32 rotr32(u32 x, int r) noexcept {
    return static_cast<u32>((x >> r) | (x << (32 - r)));
}
[[nodiscard]] constexpr u64 rotl64(u64 x, int r) noexcept {
    return (x << r) | (x >> (64 - r));
}
[[nodiscard]] constexpr u64 rotr64(u64 x, int r) noexcept {
    return (x >> r) | (x << (64 - r));
}

/// 逐字节小端读写（保证跨平台字节级一致）
[[nodiscard]] constexpr u32 readLE32(const u8* p) noexcept {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16) |
           (static_cast<u32>(p[3]) << 24);
}
[[nodiscard]] constexpr u64 readLE64(const u8* p) noexcept {
    return static_cast<u64>(readLE32(p)) | (static_cast<u64>(readLE32(p + 4)) << 32);
}
constexpr void writeLE32(u8* p, u32 x) noexcept {
    p[0] = static_cast<u8>(x & 0xFF);
    p[1] = static_cast<u8>((x >> 8) & 0xFF);
    p[2] = static_cast<u8>((x >> 16) & 0xFF);
    p[3] = static_cast<u8>((x >> 24) & 0xFF);
}
constexpr void writeLE64(u8* p, u64 x) noexcept {
    writeLE32(p, static_cast<u32>(x & 0xFFFFFFFFull));
    writeLE32(p + 4, static_cast<u32>(x >> 32));
}

/// 大端读写（SHA-256 的长度字段与摘要输出使用）
[[nodiscard]] constexpr u32 readBE32(const u8* p) noexcept {
    return (static_cast<u32>(p[0]) << 24) | (static_cast<u32>(p[1]) << 16) | (static_cast<u32>(p[2]) << 8) |
           static_cast<u32>(p[3]);
}
constexpr void writeBE32(u8* p, u32 x) noexcept {
    p[0] = static_cast<u8>((x >> 24) & 0xFF);
    p[1] = static_cast<u8>((x >> 16) & 0xFF);
    p[2] = static_cast<u8>((x >> 8) & 0xFF);
    p[3] = static_cast<u8>(x & 0xFF);
}
constexpr void writeBE64(u8* p, u64 x) noexcept {
    writeBE32(p, static_cast<u32>(x >> 32));
    writeBE32(p + 4, static_cast<u32>(x & 0xFFFFFFFFull));
}

/// 32 位 FNV-1a（用于表索引与调试指纹，不用于安全）
[[nodiscard]] constexpr u32 fnv1a32(std::string_view s) noexcept {
    u32 h = 2166136261u;
    for (char c : s) {
        h ^= static_cast<u32>(static_cast<unsigned char>(c));
        h *= 16777619u;
    }
    return h;
}
[[nodiscard]] constexpr u64 fnv1a64(std::string_view s) noexcept {
    u64 h = 1469598103934665603ull;
    for (char c : s) {
        h ^= static_cast<u64>(static_cast<unsigned char>(c));
        h *= 1099511628211ull;
    }
    return h;
}

}  // namespace gf
