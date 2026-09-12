#include "crypto/Hmac.h"

#include <cstring>

#include "util/Bits.h"

namespace gf {

HmacDigest hmacSha256(const u8* key, std::size_t keyLen, const u8* msg, std::size_t msgLen) noexcept {
    u8 k[64];
    std::memset(k, 0, sizeof(k));
    if (keyLen > 64) {
        Sha256 h;
        h.update(key, keyLen);
        Sha256Digest d{};
        h.finish(d.data());
        std::memcpy(k, d.data(), 32);
    } else {
        std::memcpy(k, key, keyLen);
    }
    u8 ipad[64], opad[64];
    for (int i = 0; i < 64; ++i) {
        ipad[i] = static_cast<u8>(k[i] ^ 0x36);
        opad[i] = static_cast<u8>(k[i] ^ 0x5C);
    }
    Sha256 inner;
    inner.update(ipad, 64);
    inner.update(msg, msgLen);
    Sha256Digest innerDigest{};
    inner.finish(innerDigest.data());

    Sha256 outer;
    outer.update(opad, 64);
    outer.update(innerDigest.data(), innerDigest.size());
    Sha256Digest out{};
    outer.finish(out.data());
    return out;
}

HmacDigest hmacSha256(std::string_view key, std::string_view msg) noexcept {
    return hmacSha256(reinterpret_cast<const u8*>(key.data()), key.size(),
                      reinterpret_cast<const u8*>(msg.data()), msg.size());
}

HmacDigest hmacSha256Two(std::string_view key, std::string_view a, std::string_view b) noexcept {
    u8 k[64];
    std::memset(k, 0, sizeof(k));
    if (key.size() > 64) {
        Sha256Digest d = sha256(key);
        std::memcpy(k, d.data(), 32);
    } else {
        std::memcpy(k, key.data(), key.size());
    }
    u8 ipad[64], opad[64];
    for (int i = 0; i < 64; ++i) {
        ipad[i] = static_cast<u8>(k[i] ^ 0x36);
        opad[i] = static_cast<u8>(k[i] ^ 0x5C);
    }
    Sha256 inner;
    inner.update(ipad, 64);
    inner.update(a);
    inner.update(b);
    Sha256Digest innerDigest{};
    inner.finish(innerDigest.data());

    Sha256 outer;
    outer.update(opad, 64);
    outer.update(innerDigest.data(), innerDigest.size());
    Sha256Digest out{};
    outer.finish(out.data());
    return out;
}

bool constantTimeEquals(const u8* a, const u8* b, std::size_t len) noexcept {
    u8 diff = 0;
    for (std::size_t i = 0; i < len; ++i) diff = static_cast<u8>(diff | (a[i] ^ b[i]));
    return diff == 0;
}

}  // namespace gf
