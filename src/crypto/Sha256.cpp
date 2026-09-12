#include "crypto/Sha256.h"

#include <cstring>

#include "util/Bits.h"

namespace gf {
namespace {

constexpr u32 kK[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

constexpr u32 kInit[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                          0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};

}  // namespace

void Sha256::reset() noexcept {
    for (int i = 0; i < 8; ++i) h_[static_cast<std::size_t>(i)] = kInit[i];
    bufLen_ = 0;
    totalLen_ = 0;
}

void Sha256::compress(const u8 block[kSha256Block]) noexcept {
    u32 w[64];
    for (int i = 0; i < 16; ++i) w[i] = readBE32(block + i * 4);   // 消息块按大端解析
    for (int i = 16; i < 64; ++i) {
        u32 s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        u32 s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    u32 a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
    for (int i = 0; i < 64; ++i) {
        u32 S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        u32 ch = (e & f) ^ ((~e) & g);
        u32 t1 = h + S1 + ch + kK[i] + w[i];
        u32 S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        u32 maj = (a & b) ^ (a & c) ^ (b & c);
        u32 t2 = S0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h_[0] += a;
    h_[1] += b;
    h_[2] += c;
    h_[3] += d;
    h_[4] += e;
    h_[5] += f;
    h_[6] += g;
    h_[7] += h;
}

void Sha256::update(const void* data, std::size_t len) noexcept {
    const u8* p = static_cast<const u8*>(data);
    totalLen_ += len;
    if (bufLen_ != 0) {
        std::size_t need = kSha256Block - bufLen_;
        std::size_t take = len < need ? len : need;
        std::memcpy(buf_ + bufLen_, p, take);
        bufLen_ += take;
        p += take;
        len -= take;
        if (bufLen_ == kSha256Block) {
            compress(buf_);
            bufLen_ = 0;
        }
    }
    while (len >= kSha256Block) {
        compress(p);
        p += kSha256Block;
        len -= kSha256Block;
    }
    if (len > 0) {
        std::memcpy(buf_, p, len);
        bufLen_ = len;
    }
}

void Sha256::finish(u8 out[kSha256Digest]) noexcept {
    // 在本地缓冲里做填充，压缩后恢复 h_ ⇒ finish 可重复调用且不破坏可继续 update 的语义
    const u64 bitLen = totalLen_ * 8ull;
    u8 block[128];
    std::size_t n = bufLen_;
    std::memcpy(block, buf_, n);
    block[n++] = 0x80;
    const std::size_t total = (n <= 56) ? 64 : 128;
    std::memset(block + n, 0, total - n);
    writeBE64(block + total - 8, bitLen);   // 长度字段为大端

    const std::array<u32, 8> saved = h_;
    for (std::size_t off = 0; off < total; off += kSha256Block) compress(block + off);
    for (int i = 0; i < 8; ++i) writeBE32(out + i * 4, h_[static_cast<std::size_t>(i)]);   // 摘要为大端
    h_ = saved;
}

Sha256Digest sha256(const void* data, std::size_t len) noexcept {
    Sha256 h;
    h.update(data, len);
    Sha256Digest d{};
    h.finish(d.data());
    return d;
}

Sha256Digest sha256(std::string_view s) noexcept { return sha256(s.data(), s.size()); }

Sha256Digest sha256Concat(std::string_view a, std::string_view b) noexcept {
    Sha256 h;
    h.update(a);
    h.update(b);
    Sha256Digest d{};
    h.finish(d.data());
    return d;
}

u32 crc32(const void* data, std::size_t len) noexcept {
    static u32 table[256];
    static bool init = false;
    if (!init) {
        for (u32 i = 0; i < 256; ++i) {
            u32 c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    const u8* p = static_cast<const u8*>(data);
    u32 c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

}  // namespace gf
