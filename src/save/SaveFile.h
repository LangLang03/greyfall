#pragma once
// 存档文件编解码：magic + 头部 + ChaCha20 + HMAC-SHA256 + LZ77
//
//   off  size 字段                          off  size 字段
//   0    4    magic "GFAV"                  44   8    createdTick(u64)
//   4    1    formatMajor=1                 52   4    plainLen
//   5    1    formatMinor                   56   4    cipherLen
//   6    1    flags bit0 LZ77|bit1 池|bit2 明文元数据|bit3 明文载荷
//   7    1    reserved                      60   8    lastCommitTick
//   8    4    schemaVersion                 68   1    rollbackCount
//   12   4    headerCrc32                   72   7    reserved
//   16   16   kdfSalt                       76   8    chronicleHeadHash
//   32   12   nonce(由 salt 派生)            84   16   fastReject(HMAC[0..16))
//                                                      100  N    ciphertext
//                                                      100+N 32  HMAC-SHA256(header||ct)
#include <string>
#include <string_view>
#include <vector>

#include "util/Fixed.h"

namespace gf {

struct GameState;

inline constexpr std::size_t kSaveHeaderSize = 100;
inline constexpr u8 kFlagLz77 = 0x01;
inline constexpr u8 kFlagPool = 0x02;
inline constexpr u8 kFlagPlainMeta = 0x04;
inline constexpr u8 kFlagPlainPayload = 0x08;

enum class DecodeStatus {
    Ok = 0,
    TooShort,
    BadMagic,
    HeaderCrcFail,
    FastReject,       // fastReject 不匹配 ⇒ 存档被截断/损坏
    MacFail,          // 密钥或口令错误 / 正文被篡改
    UnsupportedMajor,
    VersionTooHigh,   // 更高版本 ⇒ 退出码 6
    PayloadCorrupt,
};

[[nodiscard]] std::string_view decodeStatusName(DecodeStatus s);

/// 存档头部（明文可读，用于 slots / verify）
struct SaveHeaderInfo {
    u8 formatMajor = 0;
    u8 formatMinor = 0;
    u8 flags = 0;
    u32 schemaVersion = 0;
    u64 createdTick = 0;
    u32 plainLen = 0;
    u32 cipherLen = 0;
    u64 lastCommitTick = 0;
    u8 rollbackCount = 0;
    u64 chronicleHeadHash = 0;
    u32 headerCrc32 = 0;
    u64 fileSize = 0;
};

[[nodiscard]] SaveHeaderInfo peekSave(const std::vector<u8>& bytes);

struct SaveOptions {
    bool compress = true;
    bool encrypt = true;   // --no-encrypt 开发模式
};

/// 编码为完整存档字节
[[nodiscard]] std::vector<u8> encodeSave(const GameState& st, std::string_view passphrase,
                                         const SaveOptions& opts = {});

/// 解码；失败时 detail 给出人类可读原因
[[nodiscard]] DecodeStatus decodeSave(const std::vector<u8>& bytes, GameState& st,
                                      std::string_view passphrase, std::string* detail = nullptr);

}  // namespace gf
