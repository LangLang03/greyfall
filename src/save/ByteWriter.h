#pragma once
// 确定性字节写入器（全部小端 / LEB128，无平台相关行为）
#include <string_view>
#include <vector>

#include "save/VarInt.h"
#include "util/Fixed.h"

namespace gf {

class ByteWriter {
public:
    void reserve(std::size_t n) { buf_.reserve(n); }

    void u8v(u8 v) { buf_.push_back(v); }
    void boolv(bool v) { buf_.push_back(v ? 1 : 0); }
    void u16v(u16 v) {
        buf_.push_back(static_cast<u8>(v & 0xFF));
        buf_.push_back(static_cast<u8>((v >> 8) & 0xFF));
    }
    void u32v(u32 v) {
        for (int i = 0; i < 4; ++i) buf_.push_back(static_cast<u8>((v >> (8 * i)) & 0xFF));
    }
    void u64v(u64 v) {
        for (int i = 0; i < 8; ++i) buf_.push_back(static_cast<u8>((v >> (8 * i)) & 0xFF));
    }
    void varint(u64 v) {
        while (v >= 0x80) {
            buf_.push_back(static_cast<u8>((v & 0x7F) | 0x80));
            v >>= 7;
        }
        buf_.push_back(static_cast<u8>(v));
    }
    void svarint(i64 v) { varint(zigzagEncode(v)); }
    void fx(Fixed v) { svarint(v.rawValue()); }
    /// 未池化字符串（长度 + 原始字节），用于池本身与元数据
    void rawStr(std::string_view s) {
        varint(s.size());
        buf_.insert(buf_.end(), s.begin(), s.end());
    }
    void raw(const void* p, std::size_t n) {
        const u8* b = static_cast<const u8*>(p);
        buf_.insert(buf_.end(), b, b + n);
    }

    [[nodiscard]] const std::vector<u8>& data() const { return buf_; }
    [[nodiscard]] std::vector<u8>& data() { return buf_; }
    [[nodiscard]] std::size_t size() const { return buf_.size(); }
    [[nodiscard]] bool empty() const { return buf_.empty(); }
    void clear() { buf_.clear(); }

private:
    std::vector<u8> buf_;
};

}  // namespace gf
