#include <cstdio>
#include <cstring>
#include <exception>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "cli/Commands.h"
#include "core/Errors.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {
}  // namespace gf

int main(int argc, char** argv) {
#if defined(_WIN32)
    // Windows 控制台默认使用本地代码页（简中为 GBK/936），而游戏全程输出 UTF-8。
    // 不切到 UTF-8 代码页时，所有中文（含表格边框与命令名）都会显示为乱码。
    // 输入侧同理：SetConsoleCP 让玩家输入的中文参数也能被正确解析。
    ::SetConsoleOutputCP(CP_UTF8);
    ::SetConsoleCP(CP_UTF8);
#endif
    try {
        return gf::runCli(argc, argv);
    } catch (const gf::GameError& e) {
        std::string msg = std::string("greyfall: ") + e.what() + "\n";
        std::fwrite(msg.data(), 1, msg.size(), stdout);
        std::fflush(stdout);
        return static_cast<int>(e.code());
    } catch (const std::exception& e) {
        std::string msg = std::string("greyfall: 内部错误: ") + e.what() + "\n";
        std::fwrite(msg.data(), 1, msg.size(), stdout);
        std::fflush(stdout);
        return static_cast<int>(gf::ExitCode::Internal);
    }
}
