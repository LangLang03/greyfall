#pragma once
// 确定性字节读取器（越界即失败，绝不 UB）
#include <string>
#include <string_view>
#include <vector>

#include "save/VarInt.h"
#include "util/Fixed.h"

namespace gf {

class ByteReader {
public:
    ByteReader(const u8* data, std::size_t len) : data_(data), len_(len) {}
    explicit ByteReader(const std::vector<u8>& v) : data_(v.data()), len_(v.size()) {}

    [[nodiscard]] bool ok() const { return !failed_; }
    [[nodiscard]] bool failed() const { return failed_; }
    [[nodiscard]] std::size_t pos() const { return pos_; }
    [[nodiscard]] std::size_t remaining() const { return failed_ ? 0 : len_ - pos_; }
    [[nodiscard]] bool eof() const { return pos_ >= len_; }

    u8 u8v() {
        if (!need(1)) return 0;
        return data_[pos_++];
    }
    bool boolv() { return u8v() != 0; }
    u16 u16v() {
        if (!need(2)) return 0;
        u16 v = static_cast<u16>(data_[pos_]) | static_cast<u16>(static_cast<u16>(data_[pos_ + 1]) << 8);
        pos_ += 2;
        return v;
    }
    u32 u32v() {
        if (!need(4)) return 0;
        u32 v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<u32>(data_[pos_ + static_cast<std::size_t>(i)]) << (8 * i);
        pos_ += 4;
        return v;
    }
    u64 u64v() {
        if (!need(8)) return 0;
        u64 v = 0;
        for (int i = 0; i < 8; ++i) v |= static_cast<u64>(data_[pos_ + static_cast<std::size_t>(i)]) << (8 * i);
        pos_ += 8;
        return v;
    }
    u64 varint() {
        u64 result = 0;
        int shift = 0;
        while (true) {
            if (!need(1)) return 0;
            u8 b = data_[pos_++];
            result |= static_cast<u64>(b & 0x7F) << shift;
            if ((b & 0x80) == 0) break;
            shift += 7;
            if (shift > 63) {
                failed_ = true;
                return 0;
            }
        }
        return result;
    }
    i64 svarint() { return zigzagDecode(varint()); }
    Fixed fx() { return Fixed::raw(svarint()); }
    std::string rawStr() {
        u64 n = varint();
        if (n > remaining()) {
            failed_ = true;
            return {};
        }
        std::string s(reinterpret_cast<const char*>(data_ + pos_), static_cast<std::size_t>(n));
        pos_ += static_cast<std::size_t>(n);
        return s;
    }
    std::string_view rawView() {
        u64 n = varint();
        if (n > remaining()) {
            failed_ = true;
            return {};
        }
        std::string_view s(reinterpret_cast<const char*>(data_ + pos_), static_cast<std::size_t>(n));
        pos_ += static_cast<std::size_t>(n);
        return s;
    }
    bool raw(void* out, std::size_t n) {
        if (!need(n)) return false;
        std::memcpy(out, data_ + pos_, n);
        pos_ += n;
        return true;
    }
    [[nodiscard]] const u8* cursor() const { return data_ + pos_; }
    /// 外部显式标记失败（如字符串池索引越界）
    void fail() { failed_ = true; }

private:
    bool need(std::size_t n) {
        if (failed_ || len_ - pos_ < n) {
            failed_ = true;
            return false;
        }
        return true;
    }

    const u8* data_ = nullptr;
    std::size_t len_ = 0;
    std::size_t pos_ = 0;
    bool failed_ = false;
};

}  // namespace gf
