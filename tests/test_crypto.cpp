// 加密：SHA-256 / ChaCha20 / HMAC / KDF 的已知向量与往返、篡改拒绝
#include <algorithm>
#include <cstring>

#include "check.h"
#include "crypto/ChaCha20.h"
#include "crypto/Hmac.h"
#include "crypto/Kdf.h"
#include "crypto/Sha256.h"
#include "util/Str.h"

using namespace gf;

TEST(crypto, sha256_nist_vectors) {
    CHECK_EQ(hexEncode(sha256("").data(), 32),
             std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK_EQ(hexEncode(sha256("abc").data(), 32),
             std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK_EQ(hexEncode(sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq").data(), 32),
             std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
}

TEST(crypto, sha256_multiblock_and_incremental) {
    // 长消息（跨多个 64 字节块）
    std::string longMsg(1000, 'a');
    Sha256 oneShot;
    oneShot.update(longMsg);
    Sha256Digest a = oneShot.digest();
    Sha256 incremental;
    for (std::size_t i = 0; i < longMsg.size(); i += 7)
        incremental.update(longMsg.data() + i, std::min<std::size_t>(7, longMsg.size() - i));
    CHECK_EQ(hexEncode(a.data(), 32), hexEncode(incremental.digest().data(), 32));
    // 分块喂入必须与一次性喂入一致 ⇒ 流式接口正确
    Sha256 s2;
    s2.update("abc");
    s2.update("def");
    Sha256 s3;
    s3.update("abcdef");
    CHECK_EQ(hexEncode(s2.digest().data(), 32), hexEncode(s3.digest().data(), 32));
}

TEST(crypto, chacha20_rfc8439_vector) {
    u8 key[32];
    for (int i = 0; i < 32; ++i) key[i] = static_cast<u8>(i);
    u8 nonce[12] = {0, 0, 0, 0x09, 0, 0, 0, 0x4a, 0, 0, 0, 0};
    u8 block[64];
    chacha20Block(key, nonce, 1, block);
    CHECK_EQ(hexEncode(block, 16), std::string("10f1e7e4d13b5915500fdd1fa32071c4"));
    CHECK_EQ(hexEncode(block, 64),
             std::string("10f1e7e4d13b5915500fdd1fa32071c4c7d1f4c733c068030422aa9ac3d46c4e"
                         "d2826446079faa0914c2d705d98b02a2b5129cd1de164eb9cbd083e8a2503c4e"));
}

TEST(crypto, chacha20_roundtrip_and_stream_offset) {
    u8 key[32];
    std::memset(key, 0x42, 32);
    u8 nonce[12];
    std::memset(nonce, 0x17, 12);
    std::string data = "灰域纪元 · ChaCha20 往返测试 —— ASCII + 多字节 UTF-8";
    std::vector<u8> buf(data.begin(), data.end());
    std::vector<u8> orig = buf;
    chacha20Xor(key, nonce, 1, buf.data(), buf.size());
    CHECK(buf != orig);
    chacha20Xor(key, nonce, 1, buf.data(), buf.size());
    CHECK(buf == orig);
    // 不同起始 counter 产生不同密钥流
    std::vector<u8> a(data.begin(), data.end()), b(data.begin(), data.end());
    chacha20Xor(key, nonce, 1, a.data(), a.size());
    chacha20Xor(key, nonce, 2, b.data(), b.size());
    CHECK(a != b);
}

TEST(crypto, hmac_sha256_rfc4231) {
    // RFC 4231 Test Case 1：(key=0x0b ×20, data="Hi There") 的期望值前 4 字节 b0344c61
    std::string key(20, '\x0b');
    HmacDigest d = hmacSha256(key, "Hi There");
    CHECK_EQ(hexEncode(d.data(), 4), std::string("b0344c61"));
    // 长于分块长度的密钥（走 hash 分支）
    std::string longKey(131, 'K');
    HmacDigest d2 = hmacSha256(longKey, "greyfall");
    HmacDigest d3 = hmacSha256(longKey, "greyfall");
    CHECK(d2 == d3);
    CHECK(d2 != hmacSha256(longKey, "greyfal"));
}

TEST(crypto, kdf_determinism_and_separation) {
    u8 salt[16];
    std::memset(salt, 0x5A, 16);
    DerivedKeys a = deriveKeys(salt, "");
    DerivedKeys b = deriveKeys(salt, "");
    DerivedKeys c = deriveKeys(salt, "pw");
    CHECK_EQ(std::memcmp(a.encKey, b.encKey, 32), 0);
    CHECK(std::memcmp(a.encKey, c.encKey, 32) != 0);
    CHECK(std::memcmp(a.encKey, a.macKey, 32) != 0);
    u8 salt2[16];
    std::memset(salt2, 0x5B, 16);
    DerivedKeys d = deriveKeys(salt2, "");
    CHECK(std::memcmp(a.encKey, d.encKey, 32) != 0);
    u8 nonce1[12], nonce2[12];
    deriveNonce(salt, a, nonce1);
    deriveNonce(salt, a, nonce2);
    CHECK_EQ(std::memcmp(nonce1, nonce2, 12), 0);
}

TEST(crypto, crc32_and_constant_time) {
    CHECK_EQ(crc32("123456789", 9), 0xCBF43926u);
    u8 x[4] = {1, 2, 3, 4};
    u8 y[4] = {1, 2, 3, 4};
    u8 z[4] = {1, 2, 3, 5};
    CHECK(constantTimeEquals(x, y, 4));
    CHECK(!constantTimeEquals(x, z, 4));
}
