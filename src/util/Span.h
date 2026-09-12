#pragma once
// 轻量只读视图（零依赖，替代 std::span 在旧工具链上的缺失场景）
#include <cstddef>
#include <stdexcept>

namespace gf {

template <typename T>
class Span {
public:
    constexpr Span() noexcept = default;
    constexpr Span(T* data, std::size_t size) noexcept : data_(data), size_(size) {}
    template <std::size_t N>
    constexpr Span(T (&arr)[N]) noexcept : data_(arr), size_(N) {}

    [[nodiscard]] constexpr T* data() const noexcept { return data_; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] constexpr T& operator[](std::size_t i) const { return data_[i]; }
    [[nodiscard]] constexpr T* begin() const noexcept { return data_; }
    [[nodiscard]] constexpr T* end() const noexcept { return data_ + size_; }
    [[nodiscard]] constexpr T& front() const { return data_[0]; }
    [[nodiscard]] constexpr T& back() const { return data_[size_ - 1]; }
    [[nodiscard]] constexpr Span subspan(std::size_t off, std::size_t n = static_cast<std::size_t>(-1)) const {
        if (off > size_) throw std::out_of_range("Span::subspan");
        std::size_t avail = size_ - off;
        std::size_t take = n < avail ? n : avail;
        return Span(data_ + off, take);
    }

private:
    T* data_ = nullptr;
    std::size_t size_ = 0;
};

}  // namespace gf
