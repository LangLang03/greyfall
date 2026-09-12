#pragma once
// 退出码与错误（方案 §4）
#include <stdexcept>
#include <string>
#include <string_view>

namespace gf {

enum class ExitCode : int {
    Ok = 0,
    BadArgs = 1,
    NoSave = 2,
    Integrity = 3,
    IllegalAction = 4,
    PendingChoice = 5,
    VersionMismatch = 6,
    NoFill = 7,
    Internal = 70,
};

[[nodiscard]] const char* exitCodeName(ExitCode c);

/// 带退出码的异常：main 捕获后打印 message 并返回 code
class GameError : public std::runtime_error {
public:
    GameError(ExitCode code, std::string msg)
        : std::runtime_error(std::move(msg)), code_(code) {}
    [[nodiscard]] ExitCode code() const noexcept { return code_; }

private:
    ExitCode code_;
};

[[noreturn]] inline void fail(ExitCode code, std::string msg) { throw GameError(code, std::move(msg)); }

inline constexpr int kSchemaVersion = 5;
inline constexpr int kFormatMajor = 1;
inline constexpr int kFormatMinor = 0;

}  // namespace gf
