#pragma once
// 密钥派生：迭代 SHA256 的 PBKDF2 式 KDF（20000 轮）
//
//   K        = iteratedSHA256^20000(salt || APP_SECRET [|| passphrase])
//   encKey   = SHA256(K || 0x01)
//   macKey   = SHA256(K || 0x02)
//
// APP_SECRET 编译期内嵌 ⇒ CLI 事务模型下无需任何交互即可加解密；
// --key / GREYFALL_KEY 追加口令，口令错误 ⇒ MAC 校验失败 ⇒ 退出码 3。
#include <string>
#include <string_view>

#include "crypto/Sha256.h"
#include "util/Fixed.h"

namespace gf {

inline constexpr int kKdfIterations = 20000;

struct DerivedKeys {
    u8 encKey[32]{};
    u8 macKey[32]{};
};

/// 编译期内嵌的全局秘密（非用户口令；与用户口令串联后再派生）
[[nodiscard]] std::string_view appSecret() noexcept;

/// 由 salt + 可选口令派生密钥
[[nodiscard]] DerivedKeys deriveKeys(const u8 salt[16], std::string_view passphrase);

/// 从 salt 派生 12 字节 nonce（文档 §3：nonce 由 salt 派生）
void deriveNonce(const u8 salt[16], const DerivedKeys& keys, u8 nonce[12]) noexcept;

/// 生成密码学强度的 salt（系统熵 + 时间 + 进程信息混合）
void randomSalt(u8 salt[16]) noexcept;

}  // namespace gf
