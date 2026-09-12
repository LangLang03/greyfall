#pragma once
// HMAC-SHA256（RFC 2104 自实现）
#include <cstddef>

#include "crypto/Sha256.h"
#include "util/Fixed.h"

namespace gf {

using HmacDigest = Sha256Digest;

[[nodiscard]] HmacDigest hmacSha256(const u8* key, std::size_t keyLen, const u8* msg, std::size_t msgLen) noexcept;
[[nodiscard]] HmacDigest hmacSha256(std::string_view key, std::string_view msg) noexcept;
/// 两段消息（header || ciphertext）的 HMAC
[[nodiscard]] HmacDigest hmacSha256Two(std::string_view key, std::string_view a, std::string_view b) noexcept;

/// 常量时间比较（防时序侧信道）
[[nodiscard]] bool constantTimeEquals(const u8* a, const u8* b, std::size_t len) noexcept;

}  // namespace gf
