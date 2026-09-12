#pragma once
// 确定性编码原语：LEB128 varint + zigzag + 全局字符串池
#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "util/Fixed.h"

namespace gf {

/// LEB128 无符号 varint 的字节长度
[[nodiscard]] inline std::size_t varintSize(u64 v) noexcept {
    std::size_t n = 1;
    while (v >= 0x80) {
        v >>= 7;
        ++n;
    }
    return n;
}

[[nodiscard]] inline u64 zigzagEncode(i64 v) noexcept {
    return (static_cast<u64>(v) << 1) ^ static_cast<u64>(v >> 63);
}
[[nodiscard]] inline i64 zigzagDecode(u64 v) noexcept {
    return static_cast<i64>((v >> 1) ^ (0ull - (v & 1ull)));
}

/// 全局字符串池：所有字符串去重后按字典序排列 ⇒ 同一状态字节唯一
class StringPool {
public:
    /// 收集阶段：登记字符串（幂等）
    u32 intern(std::string_view s) {
        auto it = lookup_.find(s);
        if (it != lookup_.end()) return it->second;
        u32 idx = static_cast<u32>(strings_.size());
        strings_.emplace_back(s);
        lookup_.emplace(std::string(s), idx);
        return idx;
    }

    /// 按字典序重建索引（必须在写 body 之前、收集完成之后调用）
    void finalize() {
        std::vector<std::string> sorted = strings_;
        std::sort(sorted.begin(), sorted.end());
        sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
        strings_ = std::move(sorted);
        lookup_.clear();
        for (u32 i = 0; i < strings_.size(); ++i) lookup_.emplace(strings_[i], i);
    }

    [[nodiscard]] u32 indexOf(std::string_view s) const {
        auto it = lookup_.find(s);
        if (it == lookup_.end()) return 0xFFFFFFFFu;
        return it->second;
    }
    [[nodiscard]] const std::vector<std::string>& strings() const { return strings_; }
    [[nodiscard]] std::size_t size() const { return strings_.size(); }
    void clear() {
        strings_.clear();
        lookup_.clear();
    }

private:
    std::vector<std::string> strings_;
    std::map<std::string, u32, std::less<>> lookup_;
};

}  // namespace gf
