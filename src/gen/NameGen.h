#pragma once
// 名称生成：帝国 / 星系 / 行星 / 领袖
#include <string>
#include <string_view>

#include "rng/SplitMix.h"

namespace gf {

class NameGen {
public:
    explicit NameGen(u64 seed) : rng_(seed) {}

    [[nodiscard]] std::string empire();
    [[nodiscard]] std::string empireAdj();
    [[nodiscard]] std::string ruler();
    [[nodiscard]] std::string system();
    [[nodiscard]] std::string planet(int index);
    [[nodiscard]] std::string sector();
    [[nodiscard]] std::string fleet();
    [[nodiscard]] std::string shipDesign();
    [[nodiscard]] std::string federation();
    [[nodiscard]] std::string epoch();

private:
    [[nodiscard]] std::string pick(const char* const* arr, std::size_t n);
    SplitMix64 rng_;
};

}  // namespace gf
