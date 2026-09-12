# MinGW-w64 交叉编译工具链（Linux → Windows x86_64）
#
# 用法：
#   cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake
#   cmake --build build-win -j 24
#
# 设计要点：
#   * 静态链接 libgcc/libstdc++/winpthread —— 玩家侧无需安装任何 DLL，
#     直接双击即用（这是 CLI 小工具最重要的分发属性）。
#   * 不生成控制台之外的子系统设置，保持默认 console 子系统。
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)
set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++)
set(CMAKE_RC_COMPILER  ${TOOLCHAIN_PREFIX}-windres)
set(CMAKE_AR           ${TOOLCHAIN_PREFIX}-ar)
set(CMAKE_RANLIB       ${TOOLCHAIN_PREFIX}-ranlib)
set(CMAKE_STRIP        ${TOOLCHAIN_PREFIX}-strip)

# 只在 sysroot 中查找头文件与库，避免误用宿主机的 Linux 版本
set(CMAKE_FIND_ROOT_PATH /usr/${TOOLCHAIN_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# 产物后缀
set(CMAKE_EXECUTABLE_SUFFIX ".exe")

# ---- 完全静态链接 ----
# 不静态链接时产物依赖 libgcc_s_seh-1.dll / libstdc++-6.dll，
# 玩家必须额外安装运行时。CLI 小工具应当「一个 exe 直接跑」。
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")
