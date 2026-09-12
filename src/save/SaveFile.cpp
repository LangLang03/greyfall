#include "save/SaveFile.h"

#include <cstring>

#include "core/GameState.h"
#include "crypto/ChaCha20.h"
#include "crypto/Hmac.h"
#include "crypto/Kdf.h"
#include "crypto/Sha256.h"
#include "save/Lz77.h"
#include "save/Serde.h"
#include "util/Bits.h"
#include "util/Str.h"

namespace gf {
namespace {

constexpr char kMagic[4] = {'G', 'F', 'A', 'V'};

u32 headerCrcOf(const u8* h) {
    // CRC 覆盖 [0,12) 与 [16,84)：排除 crc 字段自身与 fastReject（84..100，
    // 它由 macKey 的 HMAC 决定，必须在 CRC 之后填入）
    u32 c = crc32(h, 12);
    u32 c2 = crc32(h + 16, 84 - 16);
    return c ^ (c2 * 2654435761u);
}

void writeHeader(u8* h, const SaveHeaderInfo& info, const u8 salt[16], const u8 nonce[12],
                 const u8 fastReject[16]) {
    std::memset(h, 0, kSaveHeaderSize);
    std::memcpy(h, kMagic, 4);
    h[4] = info.formatMajor;
    h[5] = info.formatMinor;
    h[6] = info.flags;
    h[7] = 0;
    writeLE32(h + 8, info.schemaVersion);
    // 12..16 crc 稍后填
    std::memcpy(h + 16, salt, 16);
    std::memcpy(h + 32, nonce, 12);
    writeLE64(h + 44, info.createdTick);
    writeLE32(h + 52, info.plainLen);
    writeLE32(h + 56, info.cipherLen);
    writeLE64(h + 60, info.lastCommitTick);
    h[68] = info.rollbackCount;
    writeLE64(h + 76, info.chronicleHeadHash);
    std::memcpy(h + 84, fastReject, 16);
    writeLE32(h + 12, headerCrcOf(h));
}

}  // namespace

std::string_view decodeStatusName(DecodeStatus s) {
    switch (s) {
        case DecodeStatus::Ok: return "ok";
        case DecodeStatus::TooShort: return "文件过短";
        case DecodeStatus::BadMagic: return "magic 不匹配（不是 greyfall 存档）";
        case DecodeStatus::HeaderCrcFail: return "头部 CRC 校验失败";
        case DecodeStatus::FastReject: return "fastReject 不匹配（存档被截断或损坏）";
        case DecodeStatus::MacFail: return "HMAC 校验失败（口令错误或正文被篡改）";
        case DecodeStatus::UnsupportedMajor: return "不支持的格式主版本";
        case DecodeStatus::VersionTooHigh: return "存档版本高于本程序（退出码 6）";
        case DecodeStatus::PayloadCorrupt: return "载荷解压/解码失败";
    }
    return "?";
}

SaveHeaderInfo peekSave(const std::vector<u8>& bytes) {
    SaveHeaderInfo info;
    info.fileSize = bytes.size();
    if (bytes.size() < kSaveHeaderSize) return info;
    const u8* h = bytes.data();
    if (std::memcmp(h, kMagic, 4) != 0) {
        info.formatMajor = 0xFF;
        return info;
    }
    info.formatMajor = h[4];
    info.formatMinor = h[5];
    info.flags = h[6];
    info.schemaVersion = readLE32(h + 8);
    info.headerCrc32 = readLE32(h + 12);
    info.createdTick = readLE64(h + 44);
    info.plainLen = readLE32(h + 52);
    info.cipherLen = readLE32(h + 56);
    info.lastCommitTick = readLE64(h + 60);
    info.rollbackCount = h[68];
    info.chronicleHeadHash = readLE64(h + 76);
    return info;
}

std::vector<u8> encodeSave(const GameState& st, std::string_view passphrase, const SaveOptions& opts) {
    std::vector<u8> plain = serializeState(st);
    std::vector<u8> payload = plain;
    u8 flags = kFlagPool;
    if (opts.compress) {
        std::vector<u8> packed = lz77Compress(plain, 2);
        if (packed.size() < plain.size()) {
            // 写入前自检：压缩流必须能无损解回原文，否则回退到未压缩
            std::vector<u8> verify;
            if (lz77Decompress(packed, verify) && verify == plain) {
                payload = std::move(packed);
                flags |= kFlagLz77;
            }
        }
    }
    if (!opts.encrypt) flags |= kFlagPlainPayload;

    SaveHeaderInfo info;
    info.formatMajor = static_cast<u8>(kFormatMajor);
    info.formatMinor = static_cast<u8>(kFormatMinor);
    info.flags = flags;
    info.schemaVersion = static_cast<u32>(kSchemaVersion);
    info.createdTick = st.tick;
    info.plainLen = static_cast<u32>(plain.size());
    info.cipherLen = static_cast<u32>(payload.size());
    info.lastCommitTick = st.lastCommitTick;
    info.rollbackCount = static_cast<u8>(st.rollbackCount > 255 ? 255 : st.rollbackCount);
    info.chronicleHeadHash = st.chronicleHead;

    u8 salt[16];
    randomSalt(salt);
    DerivedKeys keys = deriveKeys(salt, passphrase);
    u8 nonce[12];
    deriveNonce(salt, keys, nonce);

    if (opts.encrypt) {
        chacha20Xor(keys.encKey, nonce, 1, payload.data(), payload.size());
    }

    std::vector<u8> out;
    out.resize(kSaveHeaderSize);

    // fastReject = HMAC(macKey, header)[0..16)：解密前即可判定 header 与密钥是否匹配
    u8 zeroFr[16] = {};
    writeHeader(out.data(), info, salt, nonce, zeroFr);
    HmacDigest hdrMac = hmacSha256(
        std::string_view(reinterpret_cast<const char*>(keys.macKey), 32),
        std::string_view(reinterpret_cast<const char*>(out.data()), kSaveHeaderSize));
    std::memcpy(out.data() + 84, hdrMac.data(), 16);

    // 完整 HMAC 覆盖 header || ciphertext
    HmacDigest mac2 = hmacSha256Two(std::string_view(reinterpret_cast<const char*>(keys.macKey), 32),
                                    std::string_view(reinterpret_cast<const char*>(out.data()), kSaveHeaderSize),
                                    std::string_view(reinterpret_cast<const char*>(payload.data()), payload.size()));

    out.insert(out.end(), payload.begin(), payload.end());
    out.insert(out.end(), mac2.data(), mac2.data() + 32);
    return out;
}

DecodeStatus decodeSave(const std::vector<u8>& bytes, GameState& st, std::string_view passphrase,
                        std::string* detail) {
    auto set = [&](std::string_view s) {
        if (detail) *detail = std::string(s);
    };
    if (bytes.size() < kSaveHeaderSize + 32) {
        set("文件长度不足");
        return DecodeStatus::TooShort;
    }
    const u8* h = bytes.data();
    if (std::memcmp(h, kMagic, 4) != 0) {
        set("magic 不匹配");
        return DecodeStatus::BadMagic;
    }
    if (h[4] != static_cast<u8>(kFormatMajor)) {
        set("不支持的格式主版本 " + std::to_string(h[4]));
        return DecodeStatus::UnsupportedMajor;
    }
    u32 expectCrc = headerCrcOf(h);
    if (expectCrc != readLE32(h + 12)) {
        set("头部 CRC 不匹配");
        return DecodeStatus::HeaderCrcFail;
    }
    u32 cipherLen = readLE32(h + 56);
    if (bytes.size() < kSaveHeaderSize + cipherLen + 32) {
        set("载荷被截断");
        return DecodeStatus::FastReject;
    }
    u32 schema = readLE32(h + 8);
    if (schema > static_cast<u32>(kSchemaVersion)) {
        set("存档 schemaVersion=" + std::to_string(schema) + " 高于本程序 " + std::to_string(kSchemaVersion));
        return DecodeStatus::VersionTooHigh;
    }

    u8 salt[16];
    std::memcpy(salt, h + 16, 16);
    u8 nonce[12];
    std::memcpy(nonce, h + 32, 12);
    DerivedKeys keys = deriveKeys(salt, passphrase);

    const u8* ct = h + kSaveHeaderSize;
    const u8* macStored = ct + cipherLen;
    // 第一步：fastReject —— header 与密钥是否匹配（无需解密载荷）
    // 计算时把 84..100 清零，保证编解码两侧的输入一致
    u8 hdrCopy[kSaveHeaderSize];
    std::memcpy(hdrCopy, h, kSaveHeaderSize);
    std::memset(hdrCopy + 84, 0, 16);
    HmacDigest hdrMac = hmacSha256(std::string_view(reinterpret_cast<const char*>(keys.macKey), 32),
                                   std::string_view(reinterpret_cast<const char*>(hdrCopy), kSaveHeaderSize));
    if (!constantTimeEquals(h + 84, hdrMac.data(), 16)) {
        set("fastReject 不匹配（口令错误、header 被篡改或存档损坏）");
        return DecodeStatus::FastReject;
    }
    // 第二步：完整 HMAC —— header || ciphertext
    HmacDigest mac = hmacSha256Two(std::string_view(reinterpret_cast<const char*>(keys.macKey), 32),
                                   std::string_view(reinterpret_cast<const char*>(h), kSaveHeaderSize),
                                   std::string_view(reinterpret_cast<const char*>(ct), cipherLen));
    if (!constantTimeEquals(macStored, mac.data(), 32)) {
        set("HMAC 校验失败（口令错误或存档被篡改）");
        return DecodeStatus::MacFail;
    }

    std::vector<u8> payload(ct, ct + cipherLen);
    if ((h[6] & kFlagPlainPayload) == 0) {
        chacha20Xor(keys.encKey, nonce, 1, payload.data(), payload.size());
    }

    std::vector<u8> plain;
    if (h[6] & kFlagLz77) {
        if (!lz77Decompress(payload, plain)) {
            set("LZ77 解压失败");
            return DecodeStatus::PayloadCorrupt;
        }
    } else {
        plain = std::move(payload);
    }
    if (plain.size() != readLE32(h + 52)) {
        set("明文长度与头部不符");
        return DecodeStatus::PayloadCorrupt;
    }
    if (!tryDeserializeState(plain, st)) {
        set("状态反序列化失败");
        return DecodeStatus::PayloadCorrupt;
    }
    return DecodeStatus::Ok;
}

}  // namespace gf
