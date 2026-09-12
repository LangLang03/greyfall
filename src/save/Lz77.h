#pragma once
// LZ77 压缩（自实现，滑动窗口 + 哈希链，纯字节流）
#include <cstddef>
#include <vector>

#include "util/Fixed.h"

namespace gf {

/// 压缩。level: 1 快速 / 2 默认 / 3 高压缩
[[nodiscard]] std::vector<u8> lz77Compress(const std::vector<u8>& in, int level = 2);
/// 解压。失败（数据损坏）返回 false 并清空 out
[[nodiscard]] bool lz77Decompress(const u8* in, std::size_t inLen, std::vector<u8>& out);
[[nodiscard]] bool lz77Decompress(const std::vector<u8>& in, std::vector<u8>& out);

/// 便捷：压缩率 = out/in（in 为空返回 1.0 定点）
[[nodiscard]] Fixed lz77Ratio(std::size_t inSize, std::size_t outSize);

}  // namespace gf
