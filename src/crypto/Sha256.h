#pragma once
// SHA-256（FIPS 180-4 自实现，零依赖）
#include <array>
#include <cstddef>
#include <string>
#include <string_view>

#include "util/Fixed.h"

namespace gf {

inline constexpr std::size_t kSha256Digest = 32;
inline constexpr std::size_t kSha256Block = 64;

using Sha256Digest = std::array<u8, kSha256Digest>;

class Sha256 {
public:
    Sha256() { reset(); }
    void reset() noexcept;
    void update(const void* data, std::size_t len) noexcept;
    void update(std::string_view s) noexcept { update(s.data(), s.size()); }
    /// 取摘要（可继续 update）
    void finish(u8 out[kSha256Digest]) noexcept;
    [[nodiscard]] Sha256Digest digest() noexcept {
        Sha256Digest d{};
        finish(d.data());
        return d;
    }

private:
    void compress(const u8 block[kSha256Block]) noexcept;

    std::array<u32, 8> h_{};
    u8 buf_[kSha256Block]{};
    std::size_t bufLen_ = 0;
    u64 totalLen_ = 0;
};

[[nodiscard]] Sha256Digest sha256(const void* data, std::size_t len) noexcept;
[[nodiscard]] Sha256Digest sha256(std::string_view s) noexcept;
/// 拼接式多段哈希
[[nodiscard]] Sha256Digest sha256Concat(std::string_view a, std::string_view b) noexcept;

/// CRC32（存档头部校验，非安全用途）
[[nodiscard]] u32 crc32(const void* data, std::size_t len) noexcept;

}  // namespace gf
