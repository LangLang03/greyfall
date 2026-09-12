#include "crypto/Kdf.h"

#include <chrono>
#include <cstring>
#include <random>

#include "crypto/Hmac.h"
#include "rng/SplitMix.h"
#include "util/Bits.h"

namespace gf {

std::string_view appSecret() noexcept {
    // 编译期内嵌熵源（分段拼接，避免编译器把整串优化成单一常量池符号）
    static const char kSecret[] =
        "greyfall/panopticon/v1|" "a7f3c19e5b2d8406" "|silent-epoch|" "3e9b17d4c6a80f52" "|panoptic-net";
    return std::string_view(kSecret, sizeof(kSecret) - 1);
}

DerivedKeys deriveKeys(const u8 salt[16], std::string_view passphrase) {
    Sha256 h;
    u8 block[16];
    std::memcpy(block, salt, 16);
    h.update(block, 16);
    h.update(appSecret());
    if (!passphrase.empty()) {
        h.update(passphrase);
    }
    Sha256Digest cur = h.digest();

    for (int i = 1; i < kKdfIterations; ++i) {
        Sha256 round;
        round.update(cur.data(), cur.size());
        round.update(block, 16);
        u8 idx[4];
        writeLE32(idx, static_cast<u32>(i));
        round.update(idx, 4);
        cur = round.digest();
    }

    DerivedKeys keys{};
    {
        Sha256 e;
        e.update(cur.data(), cur.size());
        u8 tag = 0x01;
        e.update(&tag, 1);
        Sha256Digest d = e.digest();
        std::memcpy(keys.encKey, d.data(), 32);
    }
    {
        Sha256 m;
        m.update(cur.data(), cur.size());
        u8 tag = 0x02;
        m.update(&tag, 1);
        Sha256Digest d = m.digest();
        std::memcpy(keys.macKey, d.data(), 32);
    }
    return keys;
}

void deriveNonce(const u8 salt[16], const DerivedKeys& keys, u8 nonce[12]) noexcept {
    Sha256 h;
    h.update(salt, 16);
    h.update(keys.macKey, 32);
    h.update("nonce", 5);
    Sha256Digest d = h.digest();
    std::memcpy(nonce, d.data(), 12);
}

void randomSalt(u8 salt[16]) noexcept {
    std::random_device rd;
    u64 a = (static_cast<u64>(rd()) << 32) ^ static_cast<u64>(rd());
    u64 b = static_cast<u64>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    b ^= static_cast<u64>(reinterpret_cast<std::uintptr_t>(&salt));
    SplitMix64 sm(a ^ (b * 0x9E3779B97F4A7C15ull));
    writeLE64(salt, sm.nextU64());
    writeLE64(salt + 8, sm.nextU64());
}

}  // namespace gf
