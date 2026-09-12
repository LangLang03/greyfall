#pragma once
// 命令行解析：双写法等价
//   greyfall advance --ticks=3   ≡   greyfall --action=advance --ticks=3
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "util/Fixed.h"

namespace gf {

class Args {
public:
    static Args parse(int argc, char** argv);

    [[nodiscard]] const std::string& action() const { return action_; }
    void setAction(std::string a) { action_ = std::move(a); }

    [[nodiscard]] bool has(std::string_view name) const;
    [[nodiscard]] std::string get(std::string_view name, std::string def = {}) const;
    [[nodiscard]] i64 getInt(std::string_view name, i64 def = 0) const;
    [[nodiscard]] Fixed getFixed(std::string_view name, Fixed def = Fixed(0)) const;
    [[nodiscard]] bool getBool(std::string_view name) const;

    [[nodiscard]] const std::vector<std::string>& positional() const { return positional_; }
    [[nodiscard]] std::string pos(std::size_t i, std::string def = {}) const;
    [[nodiscard]] i64 posInt(std::size_t i, i64 def = 0) const;
    [[nodiscard]] Fixed posFixed(std::size_t i, Fixed def = Fixed(0)) const;
    [[nodiscard]] std::size_t posCount() const { return positional_.size(); }

    [[nodiscard]] const std::map<std::string, std::string, std::less<>>& options() const { return options_; }
    [[nodiscard]] const std::vector<std::string>& raw() const { return raw_; }

    /// 是否为布尔型选项名（其后不接值）
    [[nodiscard]] static bool isBoolOption(std::string_view name);

private:
    std::string action_;
    std::vector<std::string> positional_;
    std::map<std::string, std::string, std::less<>> options_;
    std::vector<std::string> raw_;
};

}  // namespace gf
