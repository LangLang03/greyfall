#pragma once
// ChaCha20 流密码（RFC 8439 自实现）。存档加密使用 ctr=1 起始的密钥流。
#include <cstddef>

#include "util/Fixed.h"

namespace gf {

inline constexpr std::size_t kChaChaKey = 32;
inline constexpr std::size_t kChaChaNonce = 12;

/// 单块 ChaCha20（20 轮）；key 32 字节，nonce 12 字节，counter 为块计数
void chacha20Block(const u8 key[kChaChaKey], const u8 nonce[kChaChaNonce], u32 counter, u8 out[64]) noexcept;

/// XOR 密钥流：out = in ^ keystream（in 与 out 可相同）
void chacha20Xor(const u8 key[kChaChaKey], const u8 nonce[kChaChaNonce], u32 counter, const u8* in, u8* out,
                 std::size_t len) noexcept;

/// 便捷重载：原地加解密
void chacha20Xor(const u8 key[kChaChaKey], const u8 nonce[kChaChaNonce], u32 counter, u8* data,
                 std::size_t len) noexcept;

}  // namespace gf
