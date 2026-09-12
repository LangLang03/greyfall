#include "crypto/ChaCha20.h"

#include <cstring>

#include "util/Bits.h"

namespace gf {
namespace {

constexpr char kSigma[17] = "expand 32-byte k";

inline void qr(u32& a, u32& b, u32& c, u32& d) noexcept {
    a += b;
    d ^= a;
    d = rotl32(d, 16);
    c += d;
    b ^= c;
    b = rotl32(b, 12);
    a += b;
    d ^= a;
    d = rotl32(d, 8);
    c += d;
    b ^= c;
    b = rotl32(b, 7);
}

}  // namespace

void chacha20Block(const u8 key[kChaChaKey], const u8 nonce[kChaChaNonce], u32 counter, u8 out[64]) noexcept {
    u32 st[16];
    st[0] = readLE32(reinterpret_cast<const u8*>(kSigma));
    st[1] = readLE32(reinterpret_cast<const u8*>(kSigma) + 4);
    st[2] = readLE32(reinterpret_cast<const u8*>(kSigma) + 8);
    st[3] = readLE32(reinterpret_cast<const u8*>(kSigma) + 12);
    for (int i = 0; i < 8; ++i) st[4 + i] = readLE32(key + i * 4);
    st[12] = counter;
    st[13] = readLE32(nonce);
    st[14] = readLE32(nonce + 4);
    st[15] = readLE32(nonce + 8);

    u32 w[16];
    std::memcpy(w, st, sizeof(w));
    for (int i = 0; i < 10; ++i) {
        qr(w[0], w[4], w[8], w[12]);
        qr(w[1], w[5], w[9], w[13]);
        qr(w[2], w[6], w[10], w[14]);
        qr(w[3], w[7], w[11], w[15]);
        qr(w[0], w[5], w[10], w[15]);
        qr(w[1], w[6], w[11], w[12]);
        qr(w[2], w[7], w[8], w[13]);
        qr(w[3], w[4], w[9], w[14]);
    }
    for (int i = 0; i < 16; ++i) writeLE32(out + i * 4, w[i] + st[i]);
}

void chacha20Xor(const u8 key[kChaChaKey], const u8 nonce[kChaChaNonce], u32 counter, const u8* in, u8* out,
                 std::size_t len) noexcept {
    u8 ks[64];
    std::size_t off = 0;
    while (off < len) {
        chacha20Block(key, nonce, counter++, ks);
        std::size_t take = len - off < 64 ? len - off : 64;
        for (std::size_t i = 0; i < take; ++i) out[off + i] = static_cast<u8>(in[off + i] ^ ks[i]);
        off += take;
    }
}

void chacha20Xor(const u8 key[kChaChaKey], const u8 nonce[kChaChaNonce], u32 counter, u8* data,
                 std::size_t len) noexcept {
    chacha20Xor(key, nonce, counter, data, data, len);
}

}  // namespace gf
